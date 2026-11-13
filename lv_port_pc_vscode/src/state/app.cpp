#include "app.h"
#include "../ble/ble.h"
#include "../buttons/buttons.h"
#include "../display/display.h"
#include "../feedback/feedback.h"
#include "../power/power.h"
#include "../rfid/rfid.h"
#include "../storage/storage.h"
#include <string.h>

static app_state_t state = APP_READY;
static uint64_t state_since_ms = 0;

// Sessao de leitura: muda toda vez que o operador troca de modo (A <-> B)
static uint8_t mode = APP_MODE_NONE, session = 0;
static buttons_t read_button = BUTTON_SINGLE;

// Modo em lote: o que ja foi registrado neste aperto do botao
static size_t batch_seen = 0;
static int batch_count = 0, batch_hint = -1;

// Sincronizacao (duplo clique): transmite as pendentes de um modo, uma por vez,
// da mais antiga para a mais nova; a que fica sem confirmacao e tentada de novo
// na volta seguinte.
static bool sync_batch = false, sync_started = false, sync_waiting = false;
static uint16_t sync_seq = 0;
static size_t sync_index = 0;
static uint64_t sync_sent_ms = 0;
static int sync_round_acks = 0, sync_shown = -1;

// Relogio acertado pelo celular (comando BLE_CMD_CLOCK)
static bool clock_set = false;
static uint32_t clock_unix = 0;
static uint64_t clock_set_ms = 0;

static uint64_t status_ms = 0;

// O que a tela "Pronto" esta mostrando sobre a fila (para redesenhar so quando muda)
static int ready_hint = -1;

static void enter(app_state_t next, uint64_t now_ms) {
  state = next;
  state_since_ms = now_ms;
}

/**********************
 *  Acionamento dos botoes A e B
 **********************
 * - Pressao longa: o botao continua pressionado por APP_LONG_PRESS_MS.
 * - Clique curto: o botao foi solto antes disso.
 * - Duplo clique: dois cliques curtos do mesmo botao, com no maximo
 *   APP_DOUBLE_CLICK_MS entre soltar o primeiro e apertar o segundo.
 *
 * Um aperto que vira pressao longa nunca conta como clique. Um clique curto
 * sozinho nao faz nada. Nada aqui bloqueia: so compara horarios.
 */

typedef enum {
  GESTURE_NONE,
  GESTURE_LONG,    // pressao longa: comecar a leitura
  GESTURE_DOUBLE   // duplo clique: sincronizar
} gesture_t;

static const buttons_t gesture_buttons[2] = {BUTTON_SINGLE, BUTTON_BATCH};
static const uint8_t gesture_modes[2] = {APP_MODE_SINGLE, APP_MODE_BATCH};
static bool gesture_down[2], gesture_long[2], gesture_second[2];
static uint64_t gesture_since_ms[2];
static int click_button = -1;      // botao do ultimo clique curto, esperando o segundo
static uint64_t click_ms = 0;

static gesture_t poll_gesture(uint64_t now_ms, uint8_t &gesture_mode) {
  gesture_t result = GESTURE_NONE;

  for (int i = 0; i < 2; i++) {
    bool down = button_is_down(gesture_buttons[i]);

    if (down && !gesture_down[i]) {
      // Apertou: e o segundo clique se veio logo depois de um clique curto do mesmo botao.
      gesture_down[i] = true;
      gesture_long[i] = false;
      gesture_since_ms[i] = now_ms;
      gesture_second[i] = click_button == i && now_ms - click_ms <= APP_DOUBLE_CLICK_MS;
      click_button = -1;
    }
    else if (down && !gesture_long[i] && now_ms - gesture_since_ms[i] >= APP_LONG_PRESS_MS) {
      gesture_long[i] = true;
      if (result == GESTURE_NONE) {
        result = GESTURE_LONG;
        gesture_mode = gesture_modes[i];
      }
    }
    else if (!down && gesture_down[i]) {
      gesture_down[i] = false;
      if (gesture_long[i]) continue;  // fim de uma pressao longa

      if (gesture_second[i]) {
        result = GESTURE_DOUBLE;
        gesture_mode = gesture_modes[i];
      }
      else {
        click_button = i;
        click_ms = now_ms;
      }
    }
  }
  return result;
}

/**********************
 *  Utilitarios
 **********************/

// Segundos desde 1970 se o celular ja acertou o relogio; senao, segundos desde que ligou.
static uint32_t timestamp_now(uint64_t now_ms) {
  if (clock_set) return clock_unix + (uint32_t)((now_ms - clock_set_ms) / 1000);
  return (uint32_t)(now_ms / 1000);
}

static const char *last_chars(const char *epc, size_t count) {
  size_t len = strlen(epc);
  return len > count ? epc + len - count : epc;
}

// 0 = sem celular, 1 = so o teclado, 2 = aplicativo ouvindo
static int link_mode() {
  return !ble_phone_connected() ? 0 : ble_app_listening() ? 2 : 1;
}

static bool is_batch(const storage_record_t &record) {
  return (record.flags & STORAGE_FLAG_BATCH) != 0;
}

// Leituras pendentes de um modo (individual ou lote).
static int pending_of(bool batch) {
  int batch_pending = storage_count_batch();
  return batch ? batch_pending : storage_count() - batch_pending;
}

// Modo planilha, igual ao firmware anterior: espaco + apagar acorda o campo de
// texto do celular, depois a tag e digitada com Enter. Bloqueia por ~0,7 s.
static bool type_on_keyboard(const char *epc) {
  send_data_via_ble(" ", 15, false);
  delay(250);
  send_data_via_ble("\b", 15, false);
  return send_data_via_ble(epc, 15);
}

// Transmite a leitura ao aplicativo e marca como transmitida. Ela continua na
// fila ate a confirmacao.
static bool send_record(const storage_record_t &record) {
  uint8_t flags = (uint8_t)((record.flags & (STORAGE_FLAG_CLOCK_SET | STORAGE_FLAG_BATCH)) | (record.session << BLE_READING_SESSION_SHIFT));

  if (!ble_send_reading(flags, record.seq, record.epc, record.rssi, record.timestamp)) return false;
  storage_mark_sent(record.seq);
  return true;
}

// O celular recebeu a leitura: ela sai da fila. A partir daqui a mesma tag pode
// ser lida de novo (o bloqueio de repetidas vale so para as pendentes).
static void confirm_record(const storage_record_t &record) {
  int index = storage_find(record.seq);

  if (index < 0) return;
  storage_remove(record.seq);

  if (sync_waiting && sync_seq == record.seq) {
    sync_waiting = false;
    sync_round_acks++;
  }
  if ((size_t)index < sync_index) sync_index--;
}

static storage_record_t make_record(const rfid_tag_t &tag, uint64_t now_ms) {
  storage_record_t record;

  memset(&record, 0, sizeof(record));
  record.seq = storage_next_seq();
  record.rssi = (int8_t)(tag.rssi == RFID_NO_RSSI || tag.rssi < -127 || tag.rssi > 127 ? BLE_RSSI_UNKNOWN : tag.rssi);
  record.flags = (uint8_t)((clock_set ? STORAGE_FLAG_CLOCK_SET : 0) | (mode == APP_MODE_BATCH ? STORAGE_FLAG_BATCH : 0));
  record.timestamp = timestamp_now(now_ms);
  record.session = session;
  strncpy(record.epc, tag.epc.c_str(), STORAGE_EPC_MAX);
  return record;
}

/**********************
 *  Telas
 **********************/

// Tela "Pronto", com o aviso das leituras pendentes de cada modo quando houver.
static void show_ready(uint64_t now_ms) {
  char hint[80];
  int single = pending_of(false), batch = pending_of(true);

  if (single + batch == 0) {
    display_show(DISPLAY_READY);
  }
  else {
    snprintf(hint, sizeof(hint), "Pendentes  A: %d  B: %d\n2 cliques para enviar", single, batch);
    display_show(DISPLAY_READY, hint);
  }

  ready_hint = single * (STORAGE_CAPACITY + 1) + batch;
  enter(APP_READY, now_ms);
}

static void show_result(const char *text, uint16_t seq, uint64_t now_ms) {
  display_show(DISPLAY_READ_OK, text, seq);
  enter(APP_READ_OK, now_ms);
}

static void show_message(const char *text, uint64_t now_ms) {
  display_show(DISPLAY_MESSAGE, text);
  enter(APP_MESSAGE, now_ms);
}

// Tela do modo em lote: tags lidas neste aperto e quantas estao pendentes no lote.
static void show_batch(bool force) {
  char hint[40];
  int pending = pending_of(true);
  bool full = storage_count() >= STORAGE_CAPACITY;
  int key = batch_count * 1024 + pending * 2 + (full ? 1 : 0);

  if (!force && key == batch_hint) return;
  batch_hint = key;

  if (full) snprintf(hint, sizeof(hint), "Memória cheia");
  else if (pending == 0) snprintf(hint, sizeof(hint), "Procurando...");
  else snprintf(hint, sizeof(hint), pending == 1 ? "%d pendente" : "%d pendentes", pending);
  display_show(DISPLAY_BATCH, hint, batch_count);
}

/**********************
 *  Leitura
 **********************/

// Troca de modo (A <-> B): a sessao anterior termina. As leituras dela que ainda
// nao foram transmitidas sao canceladas; as transmitidas sem confirmacao
// continuam pendentes; as confirmadas ja sairam da fila.
static void begin_session(uint8_t new_mode) {
  if (mode == new_mode) return;

  if (mode != APP_MODE_NONE) {
    uint16_t cancelled = storage_cancel_session(session);
    PRINTF("SESSION: %u (%s) closed | cancelled: %u | still pending: %u\n", session, mode == APP_MODE_BATCH ? "batch" : "single", cancelled, storage_count());
  }

  session = session >= STORAGE_SESSION_MAX ? 1 : session + 1;
  mode = new_mode;
  storage_session_save(session, mode);
  PRINTF("SESSION: %u (%s) started\n", session, mode == APP_MODE_BATCH ? "batch" : "single");
}

static void start_reading(uint8_t new_mode, uint64_t now_ms) {
  begin_session(new_mode);
  read_button = new_mode == APP_MODE_BATCH ? BUTTON_BATCH : BUTTON_SINGLE;
  batch_seen = 0;
  batch_count = 0;

  rfid_start(now_ms);
  if (mode == APP_MODE_BATCH) show_batch(true);
  else display_show(DISPLAY_READING);
  enter(APP_READING, now_ms);
}

// Nenhuma tag inedita no inventario: so repetidas ao alcance, ou nada.
static void show_nothing_read(uint64_t now_ms) {
  feedback_error();
  if (!rfid_duplicates().empty()) {
    const STRING &epc = rfid_duplicates()[0];
    PRINTF("TAG: only duplicates (%u), first: %s\n", (unsigned)rfid_duplicates().size(), epc.c_str());
    display_show(DISPLAY_DUPLICATE, last_chars(epc.c_str(), 8));
  }
  else {
    PRINTF("TAG: none\n");
    display_show(DISPLAY_NO_TAG);
  }
  enter(APP_NO_TAG, now_ms);
}

// Modo em lote, botao pressionado: toda tag inedita que aparece vira uma leitura pendente.
static void batch_collect(uint64_t now_ms) {
  const std::vector<rfid_tag_t> &tags = rfid_tags();

  for (; batch_seen < tags.size(); batch_seen++) {
    if (storage_count() >= STORAGE_CAPACITY) break;

    storage_record_t record = make_record(tags[batch_seen], now_ms);
    if (!storage_push(record)) break;
    batch_count++;
    feedback_read_ok();
    PRINTF("TAG: %s | batch reading %u | session %u | pending: %u\n", record.epc, record.seq, record.session, storage_count());
  }
  show_batch(false);
}

// O botao foi solto: o inventario para. Toda leitura fica pendente na fila ate
// o operador pedir a sincronizacao (duplo clique) e o celular confirmar.
static void finish_reading(uint64_t now_ms) {
  rfid_tag_t tag;
  storage_record_t record;

  rfid_stop();

  if (mode == APP_MODE_BATCH) {
    PRINTF("BATCH: %d read | duplicates ignored: %u | pending: %u\n", batch_count, (unsigned)rfid_duplicates().size(), storage_count());
    if (batch_count == 0 && storage_count() >= STORAGE_CAPACITY && !rfid_tags().empty()) {
      feedback_error();
      show_message("Memória cheia", now_ms);
    }
    else if (batch_count == 0) {
      show_nothing_read(now_ms);
    }
    else {
      show_batch(true);
      enter(APP_READ_OK, now_ms);
    }
    return;
  }

  // Modo individual: a melhor tag inedita, se houver, e a leitura.
  if (!rfid_best_tag(tag)) {
    show_nothing_read(now_ms);
    return;
  }

  PRINTF("TAG: %s | rssi: %d | reads: %u | new tags in range: %u | duplicates ignored: %u\n", tag.epc.c_str(), tag.rssi, tag.reads, (unsigned)rfid_tags().size(), (unsigned)rfid_duplicates().size());

  if (storage_count() >= STORAGE_CAPACITY) {
    PRINTF("STORAGE: queue full, reading refused\n");
    feedback_error();
    show_message("Memória cheia", now_ms);
    return;
  }

  record = make_record(tag, now_ms);
  if (!storage_push(record)) {
    feedback_error();
    show_message("Falha na memória", now_ms);
    return;
  }
  feedback_read_ok();
  PRINTF("STORAGE: reading %u stored | session %u | pending: %u\n", record.seq, record.session, storage_count());
  show_result(last_chars(record.epc, 8), record.seq, now_ms);
}

/**********************
 *  Sincronizacao
 **********************/

// Duplo clique em A (individuais) ou em B (lote).
static void start_sync(bool batch, uint64_t now_ms) {
  PRINTF("SYNC: requested for %s | pending: %d | link: %d\n", batch ? "batch" : "single", pending_of(batch), link_mode());

  if (!ble_phone_connected()) {
    feedback_error();
    show_message("Celular\nDesconectado", now_ms);
    return;
  }
  if (pending_of(batch) == 0) {
    show_message("Nenhuma Leitura\nPendente", now_ms);
    return;
  }

  sync_batch = batch;
  sync_started = false;
  sync_waiting = false;
  sync_index = 0;
  sync_round_acks = 0;
  sync_shown = -1;
  display_show(DISPLAY_MESSAGE, "Preparando\nenvio...");
  enter(APP_SYNC, now_ms);
}

// Posiciona sync_index na proxima leitura do modo que esta sendo sincronizado.
static bool sync_next(storage_record_t &record) {
  while (storage_get(sync_index, record)) {
    if (is_batch(record) == sync_batch) return true;
    sync_index++;
  }
  return false;
}

static void sync_loop(uint64_t now_ms) {
  storage_record_t record;
  char text[48];
  int left;

  if (!ble_phone_connected()) {
    PRINTF("SYNC: phone disconnected | still pending: %d\n", pending_of(sync_batch));
    feedback_error();
    show_message("Celular\nDesconectado", now_ms);
    return;
  }

  if (!sync_started) {
    if (now_ms - state_since_ms < APP_SYNC_PREPARE_MS) return;
    sync_started = true;
  }

  left = pending_of(sync_batch);
  if (left == 0) {
    PRINTF("SYNC: done\n");
    show_message("Sincronização\nConcluida", now_ms);
    return;
  }
  if (left != sync_shown) {
    sync_shown = left;
    display_show(DISPLAY_SENDING, NULL, left);
    enter(APP_SYNC, now_ms);  // da uma volta do loop para a tela ser desenhada
    return;
  }

  if (!ble_app_listening()) {
    // So o teclado: a tag digitada conta como confirmada e sai da fila.
    if (now_ms - state_since_ms < 60) return;
    sync_index = 0;
    if (sync_next(record) && type_on_keyboard(record.epc)) {
      confirm_record(record);
      PRINTF("SYNC: reading %u typed | pending: %d\n", record.seq, pending_of(sync_batch));
    }
    else {
      feedback_error();
      show_message("Falha no envio", now_ms);
    }
    return;
  }

  if (sync_waiting) {
    if (now_ms - sync_sent_ms < APP_ACK_TIMEOUT_MS) return;
    PRINTF("SYNC: no ack for seq %u, stays pending\n", sync_seq);
    sync_waiting = false;
    sync_index++;
  }

  if (!sync_next(record)) {
    // Fim da volta. Recomeca enquanto alguma leitura estiver sendo confirmada.
    if (sync_round_acks == 0) {
      PRINTF("SYNC: stopped, %d not confirmed\n", left);
      feedback_error();
      snprintf(text, sizeof(text), left == 1 ? "Falta %d leitura" : "Faltam %d leituras", left);
      show_message(text, now_ms);
      return;
    }
    sync_round_acks = 0;
    sync_index = 0;
    return;
  }

  if (send_record(record)) {
    sync_waiting = true;
    sync_seq = record.seq;
    sync_sent_ms = now_ms;
  }
}

/**********************
 *  Bluetooth
 **********************/

// Comandos que o celular escreveu na caracteristica de comando.
static void handle_ble_events(uint64_t now_ms) {
  ble_event_t event;
  storage_record_t record;

  while (ble_poll_event(event)) {
    switch (event.type) {
      case BLE_EVENT_ACK:
        PRINTF("BLE: ack seq %u | session %u | name: %s\n", event.seq, event.session, event.text);
        if (!storage_get((size_t)storage_find(event.seq), record)) {
          PRINTF("BLE: ack ignored, reading %u is not pending\n", event.seq);
          break;
        }
        if (record.session != event.session) {
          PRINTF("BLE: ack ignored, reading %u belongs to session %u\n", event.seq, record.session);
          break;
        }
        // Confirmacao individual: so agora a leitura sai da fila.
        confirm_record(record);
        PRINTF("STORAGE: reading %u confirmed (pending: %u)\n", event.seq, storage_count());
        break;

      case BLE_EVENT_TEXT:
        PRINTF("BLE: text: %s\n", event.text);
        power_activity(now_ms);  // acende a tela para o texto ser visto
        if (state == APP_READY || state == APP_READ_OK || state == APP_NO_TAG || state == APP_MESSAGE) {
          show_message(event.text, now_ms);
        }
        break;

      case BLE_EVENT_CLOCK:
        PRINTF("BLE: clock: %u\n", (unsigned)event.clock);
        clock_set = true;
        clock_unix = event.clock;
        clock_set_ms = now_ms;
        break;

      case BLE_EVENT_CLEAR_HISTORY:
        // Sem efeito pratico nesta versao (o historico fica sempre vazio); mantido
        // para o aplicativo que ja envia o comando.
        PRINTF("BLE: clear history\n");
        history_clear();
        power_activity(now_ms);
        if (state == APP_READY || state == APP_READ_OK || state == APP_NO_TAG || state == APP_MESSAGE) {
          show_message("Histórico limpo", now_ms);
        }
        break;
    }
  }
}

void app_init() {
  storage_init();
  // O historico de tags descarregadas nao bloqueia mais a leitura: o que um
  // firmware anterior gravou e descartado.
  if (history_count() > 0) history_clear();
  storage_session_load(session, mode);
  PRINTF("SESSION: %u | mode %u\n", session, mode);
  rfid_init();
  feedback_init();
  show_ready(0);
}

void app_loop(uint64_t now_ms) {
  uint8_t gesture_mode = APP_MODE_NONE;
  gesture_t gesture;

  handle_ble_events(now_ms);

  if (now_ms - status_ms >= APP_STATUS_MS) {
    status_ms = now_ms;
    ble_set_status((uint8_t)(get_battery_percent() + .5), storage_count(), history_count());
  }

  // Acompanha os botoes em todos os estados; so alguns estados usam o resultado.
  gesture = poll_gesture(now_ms, gesture_mode);

  switch (state) {
    case APP_READING:
      rfid_poll(now_ms);
      if (mode == APP_MODE_BATCH) batch_collect(now_ms);
      if (!button_is_down(read_button)) finish_reading(now_ms);
      break;

    case APP_SYNC:
      // Duplo clique ou aperto longo durante a sincronizacao: ignorado.
      sync_loop(now_ms);
      break;

    case APP_READ_OK:
    case APP_NO_TAG:
    case APP_MESSAGE:
    case APP_READY:
      if (gesture == GESTURE_LONG) {
        start_reading(gesture_mode, now_ms);
        break;
      }
      if (gesture == GESTURE_DOUBLE) {
        start_sync(gesture_mode == APP_MODE_BATCH, now_ms);
        break;
      }

      if (state != APP_READY) {
        if (now_ms - state_since_ms >= APP_RESULT_MS) show_ready(now_ms);
        break;
      }

      // Mantem o aviso das pendentes atualizado (confirmacao atrasada, por exemplo)
      if (ready_hint != pending_of(false) * (STORAGE_CAPACITY + 1) + pending_of(true)) show_ready(now_ms);
      break;
  }
}

app_state_t app_state() {
  return state;
}
