#include "power.h"
#include "../buttons/buttons.h"
#include "../display/display.h"

static bool screen_on = true;
static bool started = false;
static uint64_t activity_ms = 0;

void power_activity(uint64_t now_ms) {
  started = true;
  activity_ms = now_ms;
  if (!screen_on) {
    screen_on = true;
    display_set_backlight(true);
    PRINTF("POWER: screen on\n");
  }
}

void power_loop(uint64_t now_ms) {
  if (!started) power_activity(now_ms);

  if (buttons_any_down()) {
    // Tela apagada: este aperto so acende, ate todos os botoes serem soltos.
    if (!screen_on) buttons_ignore_until_release();
    power_activity(now_ms);
  }
  else if (screen_on && now_ms - activity_ms >= SCREEN_OFF_MS) {
    screen_on = false;
    display_set_backlight(false);
    PRINTF("POWER: screen off\n");
  }
}

bool power_screen_on() {
  return screen_on;
}
