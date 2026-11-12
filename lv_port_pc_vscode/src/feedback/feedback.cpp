#include "feedback.h"

// Enquanto BUZZER_PIN e VIBRATION_PIN forem -1 (src/config.h), nada e acionado:
// as funcoes apenas registram no log. O acionamento abaixo ainda nao foi testado
// em hardware, porque o aparelho atual nao tem buzzer nem motor ligados.

void feedback_init() {
  #ifdef ESP32
    if (BUZZER_PIN >= 0) pinMode(BUZZER_PIN, OUTPUT);
    if (VIBRATION_PIN >= 0) {
      pinMode(VIBRATION_PIN, OUTPUT);
      digitalWrite(VIBRATION_PIN, LOW);
    }
  #endif // ESP32
}

void feedback_read_ok() {
  PRINTF("FEEDBACK: read ok\n");
  #ifdef ESP32
    if (BUZZER_PIN >= 0) tone(BUZZER_PIN, 2700, 80);
  #endif // ESP32
}

void feedback_error() {
  PRINTF("FEEDBACK: error\n");
  #ifdef ESP32
    if (BUZZER_PIN >= 0) tone(BUZZER_PIN, 400, 350);
  #endif // ESP32
}
