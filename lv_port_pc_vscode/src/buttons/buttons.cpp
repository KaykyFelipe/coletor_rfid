#include "buttons.h"

typedef struct {
  bool raw_down;        // ultima leitura bruta
  bool down;            // estado estavel
  bool pressed;         // evento: acabou de ser pressionado
  bool released;        // evento: acabou de ser solto
  uint64_t changed_ms;  // quando a leitura bruta mudou pela ultima vez
} button_t;

static button_t buttons[BUTTONS_COUNT];
static bool ignoring = false;  // ver buttons_ignore_until_release()

void buttons_init() {
  for (int b = 0; b < BUTTONS_COUNT; b++) {
    buttons[b] = {false, false, false, false, 0};
  }
}

void buttons_update(uint64_t now_ms) {
  for (int b = 0; b < BUTTONS_COUNT; b++) {
    button_t &button = buttons[b];
    bool raw_down = get_button_state(b) == LOW;  // pull-up: pressionado = LOW

    button.pressed = false;
    button.released = false;

    if (raw_down != button.raw_down) {
      button.raw_down = raw_down;
      button.changed_ms = now_ms;
    }
    else if (raw_down != button.down && now_ms - button.changed_ms >= BUTTONS_DEBOUNCE_MS) {
      button.down = raw_down;
      button.pressed = raw_down;
      button.released = !raw_down;
    }
  }

  if (ignoring) {
    bool any = false;
    for (int b = 0; b < BUTTONS_COUNT; b++) {
      buttons[b].pressed = false;
      buttons[b].released = false;
      any = any || buttons[b].down || buttons[b].raw_down;
    }
    if (!any) ignoring = false;
  }
}

void buttons_ignore_until_release() {
  ignoring = true;
  for (int b = 0; b < BUTTONS_COUNT; b++) {
    buttons[b].pressed = false;
    buttons[b].released = false;
  }
}

bool button_is_down(buttons_t button) {
  return !ignoring && buttons[button].down;
}

bool button_was_pressed(buttons_t button) {
  return buttons[button].pressed;
}

bool button_was_released(buttons_t button) {
  return buttons[button].released;
}

bool buttons_any_down() {
  for (int b = 0; b < BUTTONS_COUNT; b++) {
    if (buttons[b].down) return true;
  }
  return false;
}
