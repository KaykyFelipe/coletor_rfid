#ifndef RFID_H
#define RFID_H

#include "../../main.h"
#include "../config.h"

// Valor de `rssi` quando o leitor nao informa a intensidade do sinal.
#define RFID_NO_RSSI INT16_MIN

typedef struct {
  STRING epc;      // identificacao da tag, em ASCII
  int16_t rssi;    // dBm, ou RFID_NO_RSSI
  uint16_t reads;  // quantas vezes foi lida neste inventario
} rfid_tag_t;

void rfid_init();

// Inventario: comeca em rfid_start(), acumula a cada rfid_poll() e termina em rfid_stop().
// Nenhuma das chamadas bloqueia o loop principal.
void rfid_start(uint64_t now_ms);
void rfid_poll(uint64_t now_ms);
void rfid_stop();
bool rfid_is_reading();

// Tags distintas e ineditas vistas no inventario atual, na ordem em que apareceram.
const std::vector<rfid_tag_t> &rfid_tags();

// Tags vistas no inventario atual que foram ignoradas por ainda estarem pendentes na fila.
const std::vector<STRING> &rfid_duplicates();

// Melhor tag do inventario: maior RSSI; se o leitor nao informa RSSI, a mais vezes lida.
// Devolve false quando nenhuma tag foi vista.
bool rfid_best_tag(rfid_tag_t &tag);

/**********************
 *  Interface do leitor
 **********************
 * Cada implementacao (simulada ou real) fornece estas quatro funcoes.
 * A escolha e feita na compilacao por RFID_SIMULATED (src/config.h).
 */

void rfid_backend_init();
void rfid_backend_start(uint64_t now_ms);
// Acrescenta em `reads` as leituras novas desde a ultima chamada, sem bloquear.
void rfid_backend_poll(uint64_t now_ms, std::vector<rfid_tag_t> &reads);
void rfid_backend_stop();

#endif // RFID_H
