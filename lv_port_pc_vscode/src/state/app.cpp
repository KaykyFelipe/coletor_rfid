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

// Leitura que esta esperando a confirmacao do celular
static storage_record_t wait_record;
static bool wait_typed = false;

// Reenvio da fila pelo servico do coletor: uma leitura por vez, a mais antiga primeiro
static bool flush_waiting = false;
static uint64_t flush_sent_ms = 0, flush_retry_ms = 0;

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

// Segundos desde 1970 se o celular ja acertou o relogio; senao, segundos desde que ligou.
static uint32_t timestamp_now(uint64_t now_ms) {
  if (clock_set) return clock_unix + (uint32_t)((now_ms - clock_set_ms) / 1000);
  return (uint32_t)(now_ms / 1000);
}

static const char *last_chars(const char *epc, size_t count) {
  size_t len = strlen(epc);
  return len > count ? epc + len - count : epc;
}

// Modo planilha, igual ao firmware anterior: espaco + apagar acorda o campo de
// texto do celular, depois a tag e digitada com Enter. Bloqueia por ~0,7 s.
static bool type_on_keyboard(const char *epc) {
  send_data_via_ble(" ", 15, false);
  delay(250);
  send_data_via_ble("\b", 15, false);
  return send_data_via_ble(epc, 15);
}

static bool send_record(const storage_record_t &record) {
  return ble_send_reading(record.seq, record.epc, record.rssi, record.timestamp, record.flags & BLE_READING_FLAG_CLOCK_SET);
}

// Tela "Pronto", com o aviso das leituras guardadas quando houver.
static void show_ready(uint64_t now_ms) {
  char hint[80];
  int count = storage_count();
  int mode = !ble_phone_connected() ? 0 : ble_app_listening() ? 2 : 1;

  if (count == 0) {
    display_show(DISPLAY_READY);
  }
  else {
    const char *noun = count == 1 ? "guardada" : "guardadas";
    if (mode == 0) snprintf(hint, sizeof(hint), "%d %s", count, noun);
    else if (mode == 1) snprintf(hint, sizeof(hint), "%d %s\nAperte A ou B para enviar", count, noun);
    else snprintf(hint, sizeof(hint), "Enviando %d %s...", count, noun);
    display_show(DISPLAY_READY, hint);
  }

  ready_hint = count * 4 + mode;
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

static void start_reading(uint64_t now_ms) {
  rfid_start(now_ms);
  display_show(DISPLAY_READING);
  enter(APP_READING, now_ms);
}

// O botao foi solto: o inventario para e a melhor tag, se houver, e a leitura.
static void finish_reading(uint64_t now_ms) {
  rfid_tag_t tag;

  rfid_stop();

  if (!rfid_best_tag(tag)) {
    PRINTF("TAG: none\n");
    feedback_error();
    display_show(DISPLAY_NO_TAG);
    enter(APP_NO_TAG, now_ms);
    return;
  }

  PRINTF("TAG: %s | rssi: %d | reads: %u | tags in range: %u\n", tag.epc.c_str(), tag.rssi, tag.reads, (unsigned)rfid_tags().size());

  // Sem celular e sem espaco para guardar: a leitura nao pode ser aceita.
  if (!ble_phone_connected() && storage_count() >= STORAGE_CAPACITY) {
    PRINTF("STORAGE: queue full, reading refused\n");
    feedback_error();
    show_message("Memória cheia", now_ms);
    return;
  }

  feedback_read_ok();

  memset(&wait_record, 0, sizeof(wait_record));
  wait_record.seq = storage_next_seq();
  wait_record.rssi = (int8_t)(tag.rssi == RFID_NO_RSSI || tag.rssi < -127 || tag.rssi > 127 ? BLE_RSSI_UNKNOWN : tag.rssi);
  wait_record.flags = clock_set ? BLE_READING_FLAG_CLOCK_SET : 0;
  wait_record.timestamp = timestamp_now(now_ms);
  strncpy(wait_record.epc, tag.epc.c_str(), STORAGE_EPC_MAX);
  wait_typed = false;

  if (!ble_phone_connected()) {
    storage_push(wait_record);
    PRINTF("BLE: no phone, reading %u stored (pending: %u)\n", wait_record.seq, storage_count());
    display_show(DISPLAY_OFFLINE, NULL, storage_count());
    enter(APP_READ_OK, now_ms);
    return;
  }

  send_record(wait_record);
  display_show(DISPLAY_SENDING);
  enter(APP_SENDING, now_ms);
}

// Comandos que o celular escreveu na caracteristica de comando.
static void handle_ble_events(uint64_t now_ms) {
  ble_event_t event;
  storage_record_t oldest;

  while (ble_poll_event(event)) {
    switch (event.type) {
      case BLE_EVENT_ACK:
        PRINTF("BLE: ack seq %u | name: %s\n", event.seq, event.text);
        if (state == APP_SENDING && event.seq == wait_record.seq) {
          show_result(event.text[0] ? event.text : last_chars(wait_record.epc, 8), wait_record.seq, now_ms);
        }
        else if (storage_peek(oldest) && oldest.seq == event.seq) {
          // Confirmacao de uma leitura guardada: so agora ela sai da fila.
          storage_pop();
          flush_waiting = false;
          PRINTF("STORAGE: reading %u delivered (pending: %u)\n", event.seq, storage_count());
        }
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
    }
  }
}

// Com o aplicativo ouvindo, reenvia a leitura mais antiga da fila e espera a
// confirmacao dela antes de mandar a proxima. Sem resposta, tenta de novo depois.
static void flush_queue(uint64_t now_ms) {
  storage_record_t oldest;

  if (!ble_app_listening()) {
    flush_waiting = false;
    return;
  }

  if (flush_waiting) {
    if (now_ms - flush_sent_ms >= APP_ACK_TIMEOUT_MS) {
      flush_waiting = false;
      flush_retry_ms = now_ms + APP_FLUSH_RETRY_MS;
    }
    return;
  }

  if (now_ms < flush_retry_ms || !storage_peek(oldest)) return;

  if (send_record(oldest)) {
    PRINTF("STORAGE: resending reading %u\n", oldest.seq);
    flush_waiting = true;
    flush_sent_ms = now_ms;
  }
}

void app_init() {
  storage_init();
  rfid_init();
  feedback_init();
  show_ready(0);
}

void app_loop(uint64_t now_ms) {
  storage_record_t oldest;

  handle_ble_events(now_ms);

  if (now_ms - status_ms >= APP_STATUS_MS) {
    status_ms = now_ms;
    ble_set_status((uint8_t)(get_battery_percent() + .5), storage_count());
  }

  switch (state) {
    case APP_READING:
      rfid_poll(now_ms);
      if (!button_is_down(BUTTON_READ)) finish_reading(now_ms);
      break;

    case APP_SENDING:
      if (!wait_typed) {
        // Espera a tela "Enviando..." ser desenhada antes de digitar (a digitacao bloqueia).
        if (now_ms - state_since_ms >= 60) {
          wait_typed = true;
          type_on_keyboard(wait_record.epc);
        }
      }
      else if (!ble_app_listening()) {
        // So o teclado esta em uso: a tag ja foi digitada, a leitura esta entregue.
        show_result(last_chars(wait_record.epc, 8), wait_record.seq, now_ms);
      }
      else if (now_ms - state_since_ms >= APP_ACK_TIMEOUT_MS) {
        // O aplicativo nao confirmou a tempo: a leitura fica na fila para reenvio.
        PRINTF("BLE: no ack for seq %u, stored for resend\n", wait_record.seq);
        if (!storage_push(wait_record)) PRINTF("STORAGE: queue full, reading %u not stored\n", wait_record.seq);
        show_result(last_chars(wait_record.epc, 8), wait_record.seq, now_ms);
      }
      break;

    case APP_FLUSH_KEYS:
      // Uma leitura guardada por volta do loop, para a tela continuar sendo desenhada.
      if (now_ms - state_since_ms < 60) break;
      if (storage_peek(oldest) && ble_phone_connected() && !ble_app_listening() && type_on_keyboard(oldest.epc)) {
        storage_pop();
        PRINTF("STORAGE: reading %u typed (pending: %u)\n", oldest.seq, storage_count());
        if (storage_count() > 0) {
          display_show(DISPLAY_SENDING, NULL, storage_count());
          enter(APP_FLUSH_KEYS, now_ms);
          break;
        }
      }
      show_ready(now_ms);
      break;

    case APP_READ_OK:
    case APP_NO_TAG:
    case APP_MESSAGE:
      // Um novo aperto durante o resultado ja comeca outra leitura.
      if (button_was_pressed(BUTTON_READ)) {
        start_reading(now_ms);
      }
      else if (now_ms - state_since_ms >= APP_RESULT_MS) {
        show_ready(now_ms);
      }
      break;

    case APP_READY:
      if (button_was_pressed(BUTTON_READ)) {
        start_reading(now_ms);
        break;
      }

      flush_queue(now_ms);

      // So com o teclado: o operador decide quando digitar as guardadas,
      // para elas nao cairem em um campo errado do celular.
      if (storage_count() > 0 && ble_phone_connected() && !ble_app_listening() &&
          (button_was_pressed(BUTTON_SPARE_1) || button_was_pressed(BUTTON_SPARE_2))) {
        display_show(DISPLAY_SENDING, NULL, storage_count());
        enter(APP_FLUSH_KEYS, now_ms);
        break;
      }

      // Mantem o aviso da fila atualizado (conexao mudou, leitura entregue...)
      if (ready_hint != storage_count() * 4 + (!ble_phone_connected() ? 0 : ble_app_listening() ? 2 : 1)) {
        show_ready(now_ms);
      }
      break;
  }
}

app_state_t app_state() {
  return state;
}
