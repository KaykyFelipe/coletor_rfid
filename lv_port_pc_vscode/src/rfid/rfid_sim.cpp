#include "rfid.h"

#if RFID_SIMULATED

#include <cstdlib>

// Leitor simulado: gera leituras ficticias para testar sem o modulo RFID.
//
// - No simulador de PC, as tags "ao alcance" sao as linhas da caixa "Antena".
// - No ESP32, e uma lista fixa.
// - A primeira leitura so aparece depois de SIM_FIRST_READ_MS: um toque curto
//   no botao termina sem tag, o que permite testar a tela de erro.
// - A primeira tag da lista e sempre a de sinal mais forte.

#define SIM_FIRST_READ_MS 300
#define SIM_ROUND_MS 100

static std::vector<STRING> in_range;
static uint64_t started_ms = 0, last_round_ms = 0;

void rfid_backend_init() {
  PRINTF("RFID: simulated reader\n");
}

void rfid_backend_start(uint64_t now_ms) {
  #ifdef ESP32
    in_range = {"TP.01.123456", "TP.01.004321", "TP.02.000015"};
  #else
    in_range = antenna_read(true, RFID_TAG_FILTER);
  #endif // ESP32 / HAL

  started_ms = now_ms;
  last_round_ms = now_ms;
}

void rfid_backend_poll(uint64_t now_ms, std::vector<rfid_tag_t> &reads) {
  if (now_ms - started_ms < SIM_FIRST_READ_MS || now_ms - last_round_ms < SIM_ROUND_MS) return;
  last_round_ms = now_ms;

  for (size_t i = 0; i < in_range.size(); i++) {
    if (rand() % 100 >= 70) continue;  // nem toda tag responde em toda rodada

    rfid_tag_t read;
    read.epc = in_range[i];
    read.rssi = -45 - 7 * (int16_t)i + (rand() % 7 - 3);  // faixas que nao se sobrepoem
    read.reads = 1;
    reads.push_back(read);
  }
}

void rfid_backend_stop() {
}

#endif // RFID_SIMULATED
