#ifndef STORAGE_H
#define STORAGE_H

#include "../../main.h"

// Fila de leituras ainda nao confirmadas pelo celular, gravada na memoria
// permanente: sobrevive a desligar o aparelho. A mais antiga sai primeiro.
//
// - ESP32: arquivos no LittleFS (particao "spiffs" da tabela default_16MB).
// - Simulador de PC: arquivos na pasta de onde o simulador foi aberto.
//
// Atencao: gravar o firmware com "EraseFlash=all" apaga a fila.

#define STORAGE_CAPACITY 250
#define STORAGE_EPC_MAX 15

typedef struct {
  uint16_t seq;
  int8_t rssi;         // -128 = nao disponivel
  uint8_t flags;       // bit 0: timestamp veio do relogio acertado pelo celular
  uint32_t timestamp;
  char epc[STORAGE_EPC_MAX + 1];
} storage_record_t;    // 24 bytes

// Abre a memoria e carrega a fila. Devolve false se a memoria nao esta disponivel
// (a fila funciona so em RAM nesse caso).
bool storage_init();

// Proximo numero sequencial (1 a 65535), ja gravado.
uint16_t storage_next_seq();

// Guarda uma leitura no fim da fila. Devolve false se a fila esta cheia.
bool storage_push(const storage_record_t &record);

// Leitura mais antiga, sem retirar. Devolve false se a fila esta vazia.
bool storage_peek(storage_record_t &record);

// Retira a leitura mais antiga (chamar so depois de confirmada).
void storage_pop();

uint16_t storage_count();

// A memoria permanente esta funcionando.
bool storage_persistent();

#endif // STORAGE_H
