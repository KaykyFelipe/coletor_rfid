#include "app.h"
#include "../buttons/buttons.h"
#include "../display/display.h"

// ETAPA 1: somente botoes e telas. O leitor RFID entra na ETAPA 2; ate la,
// soltar o botao de leitura sempre termina em "Nenhuma tag encontrada".

static uint64_t result_since_ms = 0;
static bool showing_result = false;

static void show_result(display_screen_t which, uint64_t now_ms, const char *text = NULL, int count = 0) {
  display_show(which, text, count);
  result_since_ms = now_ms;
  showing_result = true;
}

void app_init() {
  showing_result = false;
  display_show(DISPLAY_READY);
}

void app_loop(uint64_t now_ms) {
  if (button_was_pressed(BUTTON_READ)) {
    PRINTF("READ button pressed\n");
    showing_result = false;
    display_show(DISPLAY_READING);
  }
  else if (button_was_released(BUTTON_READ)) {
    PRINTF("READ button released\n");
    show_result(DISPLAY_NO_TAG, now_ms);
  }

  #ifndef ESP32
    // Somente no simulador: o botao A percorre as telas para conferir o layout.
    // No coletor os botoes A e B nao tem funcao nesta versao.
    if (button_was_pressed(BUTTON_SPARE_1) && !button_is_down(BUTTON_READ)) {
      static int preview = 0;
      switch (preview++ % 3) {
        case 0: show_result(DISPLAY_READ_OK, now_ms, "TP.01.123456"); break;
        case 1: show_result(DISPLAY_READ_OK, now_ms, "Bomba B-104"); break;
        case 2: show_result(DISPLAY_OFFLINE, now_ms, NULL, 5); break;
      }
    }
  #endif // !ESP32 (HAL)

  if (showing_result && !button_is_down(BUTTON_READ) && now_ms - result_since_ms >= APP_RESULT_MS) {
    showing_result = false;
    display_show(DISPLAY_READY);
  }
}
