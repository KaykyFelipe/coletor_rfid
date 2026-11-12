#include "app.h"
#include "../ble/ble.h"
#include "../buttons/buttons.h"
#include "../display/display.h"
#include "../feedback/feedback.h"
#include "../rfid/rfid.h"

static app_state_t state = APP_READY;
static uint64_t state_since_ms = 0;

// Numero sequencial das leituras. ETAPA 4: passa a ser guardado na memoria permanente.
static uint16_t seq_counter = 0;

// Leituras feitas sem celular. ETAPA 4: vira a fila gravada na memoria permanente;
// por enquanto e so um contador em RAM e essas leituras NAO sao reenviadas.
static uint16_t pending_count = 0;

// Leitura que esta esperando a confirmacao do celular
static uint16_t wait_seq = 0;
static STRING wait_epc;
static bool wait_typed = false;

// Relogio acertado pelo celular (comando BLE_CMD_CLOCK)
static bool clock_set = false;
static uint32_t clock_unix = 0;
static uint64_t clock_set_ms = 0;

static uint64_t status_ms = 0;

static void enter(app_state_t next, uint64_t now_ms) {
  state = next;
  state_since_ms = now_ms;
}

static uint16_t next_seq() {
  if (++seq_counter == 0) seq_counter = 1;  // 0 fica reservado
  return seq_counter;
}

// Segundos desde 1970 se o celular ja acertou o relogio; senao, segundos desde que ligou.
static uint32_t timestamp_now(uint64_t now_ms) {
  if (clock_set) return clock_unix + (uint32_t)((now_ms - clock_set_ms) / 1000);
  return (uint32_t)(now_ms / 1000);
}

static STRING last_chars(const STRING &epc, size_t count) {
  return epc.length() > count ? epc.SUBSTRING(epc.length() - count) : epc;
}

// Modo planilha, igual ao firmware anterior: espaco + apagar acorda o campo de
// texto do celular, depois a tag e digitada com Enter. Bloqueia por ~0,7 s.
static void type_on_keyboard(const STRING &epc) {
  send_data_via_ble(" ", 15, false);
  delay(250);
  send_data_via_ble("\b", 15, false);
  send_data_via_ble(epc, 15);
}

static void show_result(const char *text, uint16_t seq, uint64_t now_ms) {
  display_show(DISPLAY_READ_OK, text, seq);
  enter(APP_READ_OK, now_ms);
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
  feedback_read_ok();

  wait_seq = next_seq();
  wait_epc = tag.epc;
  wait_typed = false;

  if (!ble_phone_connected()) {
    pending_count++;
    PRINTF("BLE: no phone, reading %u kept (pending: %u)\n", wait_seq, pending_count);
    display_show(DISPLAY_OFFLINE, NULL, pending_count);
    enter(APP_READ_OK, now_ms);
    return;
  }

  ble_send_reading(wait_seq, tag.epc.c_str(), tag.rssi == RFID_NO_RSSI ? BLE_RSSI_UNKNOWN : tag.rssi, timestamp_now(now_ms), clock_set);
  display_show(DISPLAY_SENDING);
  enter(APP_SENDING, now_ms);
}

// Comandos que o celular escreveu na caracteristica de comando.
static void handle_ble_events(uint64_t now_ms) {
  ble_event_t event;

  while (ble_poll_event(event)) {
    switch (event.type) {
      case BLE_EVENT_ACK:
        PRINTF("BLE: ack seq %u | name: %s\n", event.seq, event.text);
        if (state == APP_SENDING && event.seq == wait_seq) {
          show_result(event.text[0] ? event.text : last_chars(wait_epc, 8).c_str(), wait_seq, now_ms);
        }
        break;

      case BLE_EVENT_TEXT:
        PRINTF("BLE: text: %s\n", event.text);
        if (state != APP_READING && state != APP_SENDING) {
          display_show(DISPLAY_MESSAGE, event.text);
          enter(APP_MESSAGE, now_ms);
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

void app_init() {
  rfid_init();
  feedback_init();
  display_show(DISPLAY_READY);
  state = APP_READY;
}

void app_loop(uint64_t now_ms) {
  handle_ble_events(now_ms);

  if (now_ms - status_ms >= APP_STATUS_MS) {
    status_ms = now_ms;
    ble_set_status((uint8_t)(get_battery_percent() + .5), pending_count);
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
          type_on_keyboard(wait_epc);
        }
      }
      else if (!ble_app_listening() || now_ms - state_since_ms >= APP_ACK_TIMEOUT_MS) {
        // Sem aplicativo ouvindo ou sem resposta a tempo: mostra o final da tag.
        PRINTF("BLE: no ack for seq %u\n", wait_seq);
        show_result(last_chars(wait_epc, 8).c_str(), wait_seq, now_ms);
      }
      break;

    case APP_READ_OK:
    case APP_NO_TAG:
    case APP_MESSAGE:
      // Um novo aperto durante o resultado ja comeca outra leitura.
      if (button_was_pressed(BUTTON_READ)) {
        start_reading(now_ms);
      }
      else if (now_ms - state_since_ms >= APP_RESULT_MS) {
        display_show(DISPLAY_READY);
        enter(APP_READY, now_ms);
      }
      break;

    case APP_READY:
      if (button_was_pressed(BUTTON_READ)) start_reading(now_ms);
      break;
  }
}

app_state_t app_state() {
  return state;
}
