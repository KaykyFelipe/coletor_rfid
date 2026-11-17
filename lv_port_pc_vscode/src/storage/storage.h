#ifndef STORAGE_H
#define STORAGE_H

#include "../../main.h"

// Leituras pendentes, gravadas na memoria permanente (sobrevivem a desligar o
// aparelho). Ha duas filas independentes sobre os mesmos registros:
//
// - Bluetooth: a leitura esta pendente enquanto STORAGE_FLAG_BLE_DONE = 0.
// - API (Wi-Fi): a leitura esta pendente enquanto STORAGE_FLAG_API_DONE = 0.
//
// Confirmar por um canal nao mexe no outro. O registro so sai da memoria
// quando os dois canais confirmaram.
//
// - ESP32: arquivos no LittleFS (particao "spiffs" da tabela default_16MB).
// - Simulador de PC: arquivos na pasta de onde o simulador foi aberto.
//
// Atencao: gravar o firmware com "EraseFlash=all" apaga a fila.

#define STORAGE_CAPACITY 250
#define STORAGE_EPC_MAX 14
#define STORAGE_SESSION_MAX 63

// Bits de `flags`. Os dois primeiros vao no pacote Bluetooth (docs/PROTOCOLO_BLE.md).
#define STORAGE_FLAG_CLOCK_SET 0x01  // timestamp veio do relogio acertado pelo celular
#define STORAGE_FLAG_BATCH     0x02  // leitura feita no modo em lote (botao B)
#define STORAGE_FLAG_SENT      0x04  // ja transmitida ao aplicativo, ainda sem confirmacao
#define STORAGE_FLAG_BLE_DONE  0x08  // fila do Bluetooth: confirmada pelo celular (ou cancelada)
#define STORAGE_FLAG_API_DONE  0x10  // fila da API: confirmada pela API (HTTP 2xx)

typedef struct {
  uint16_t seq;
  int8_t rssi;         // -128 = nao disponivel
  uint8_t flags;
  uint32_t timestamp;
  char epc[STORAGE_EPC_MAX + 1];
  uint8_t session;     // sessao de leitura (1 a 63); 0 = gravada por firmware anterior
} storage_record_t;    // 24 bytes, mesmo tamanho do firmware anterior

// Abre a memoria e carrega a fila. Devolve false se a memoria nao esta disponivel
// (a fila funciona so em RAM nesse caso).
bool storage_init();

// Proximo numero sequencial (1 a 65535), ja gravado.
uint16_t storage_next_seq();

// Guarda uma leitura no fim da fila. Devolve false se a fila esta cheia.
bool storage_push(const storage_record_t &record);

// Leitura na posicao `index` (0 = a mais antiga), sem retirar.
bool storage_get(size_t index, storage_record_t &record);

// Posicao da leitura com este numero sequencial, ou -1.
int storage_find(uint16_t seq);

// Ha uma leitura pendente com este EPC?
bool storage_has_epc(const char *epc);

// Retira a leitura (chamar so depois de confirmada). Devolve false se nao estava na fila.
bool storage_remove(uint16_t seq);

// Marca a leitura como transmitida (STORAGE_FLAG_SENT), tambem no arquivo.
void storage_mark_sent(uint16_t seq);

// Um canal confirmou a leitura (`done_flag` = STORAGE_FLAG_BLE_DONE ou
// STORAGE_FLAG_API_DONE). Devolve true se, com isso, a leitura saiu da memoria
// (os dois canais ja confirmaram).
bool storage_complete(uint16_t seq, uint8_t done_flag);

// Apaga todas as leituras pendentes, confirmadas ou nao. O numero sequencial continua.
void storage_clear();

// Cancela, na fila do Bluetooth, as leituras da sessao que ainda nao foram
// transmitidas. Elas continuam pendentes para a API. Devolve quantas.
uint16_t storage_cancel_session(uint8_t session);

// Sessao atual e o modo dela (0 = nenhuma ainda).
void storage_session_load(uint8_t &session, uint8_t &mode);
void storage_session_save(uint8_t session, uint8_t mode);

uint16_t storage_count();

// Quantas leituras tem (flags & mask) == value. Exemplo: pendentes da API =
// storage_count_where(STORAGE_FLAG_API_DONE, 0).
uint16_t storage_count_where(uint8_t mask, uint8_t value);

// A memoria permanente esta funcionando.
bool storage_persistent();

/**********************
 *  Historico de EPCs (controle de duplicidade)
 **********************
 * SEM USO nesta versao: a regra atual e que a tag descarregada no celular pode
 * ser lida de novo, entao so as pendentes bloqueiam (storage_has_epc()) e nada
 * e gravado aqui. O modulo fica pronto para o caso de a regra voltar a ser
 * "tag ja confirmada nao pode ser lida ate limpar o historico".
 */

#ifndef HISTORY_CAPACITY
  #define HISTORY_CAPACITY 5000
#endif
#ifndef HISTORY_TRIM
  #define HISTORY_TRIM 500
#endif

// O EPC completo ja foi confirmado?
bool history_contains(const char *epc);

// Registra o EPC. Devolve false se ele ja estava no historico.
bool history_add(const char *epc);

// Limpa o historico. Nao mexe na fila de pendencias.
void history_clear();

uint16_t history_count();

#endif // STORAGE_H
