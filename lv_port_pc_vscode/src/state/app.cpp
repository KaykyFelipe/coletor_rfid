#include "app.h"
#include "../buttons/buttons.h"
#include "../display/display.h"
#include "../feedback/feedback.h"
#include "../rfid/rfid.h"

static app_state_t state = APP_READY;
static uint64_t state_since_ms = 0;

static void enter(app_state_t next, uint64_t now_ms) {
  state = next;
  state_since_ms = now_ms;
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

  if (rfid_best_tag(tag)) {
    PRINTF("TAG: %s | rssi: %d | reads: %u | tags in range: %u\n", tag.epc.c_str(), tag.rssi, tag.reads, (unsigned)rfid_tags().size());
    feedback_read_ok();
    // ETAPA 3: enviar ao celular e mostrar o nome que ele devolver.
    display_show(DISPLAY_READ_OK, tag.epc.c_str());
    enter(APP_READ_OK, now_ms);
  }
  else {
    PRINTF("TAG: none\n");
    feedback_error();
    display_show(DISPLAY_NO_TAG);
    enter(APP_NO_TAG, now_ms);
  }
}

void app_init() {
  rfid_init();
  feedback_init();
  display_show(DISPLAY_READY);
  state = APP_READY;
}

void app_loop(uint64_t now_ms) {
  switch (state) {
    case APP_READING:
      rfid_poll(now_ms);
      if (!button_is_down(BUTTON_READ)) finish_reading(now_ms);
      break;

    case APP_READ_OK:
    case APP_NO_TAG:
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
