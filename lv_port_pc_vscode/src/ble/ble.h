#ifndef BLE_H
#define BLE_H

#include "../../main.h"

// Bluetooth do coletor: o teclado HID (modo planilha) continua existindo e, na
// mesma conexao, ha um servico GATT proprio para o aplicativo do celular.
// O formato dos dados esta em docs/PROTOCOLO_BLE.md.

#define BLE_SERVICE_UUID      "a71a0001-6c0f-4e6b-9d2a-3f5b8c1e7a10"
#define BLE_READING_UUID      "a71a0002-6c0f-4e6b-9d2a-3f5b8c1e7a10"  // Notify
#define BLE_COMMAND_UUID      "a71a0003-6c0f-4e6b-9d2a-3f5b8c1e7a10"  // Write
#define BLE_STATUS_UUID       "a71a0004-6c0f-4e6b-9d2a-3f5b8c1e7a10"  // Read + Notify

// Codigos do primeiro byte da caracteristica de comando
#define BLE_CMD_ACK   0x01  // seq (u16) + sessao (u8) + nome do equipamento (UTF-8, opcional)
#define BLE_CMD_TEXT  0x02  // texto (UTF-8) para mostrar na tela
#define BLE_CMD_CLOCK 0x03  // relogio: segundos desde 1970 (u32)
#define BLE_CMD_CLEAR_HISTORY 0x04  // limpa o historico de tags ja registradas

// Primeiro byte da leitura:
//   bit 0      o horario veio do relogio acertado pelo celular
//   bit 1      leitura feita no modo em lote (botao B)
//   bits 2-7   sessao de leitura (1 a 63; 0 = leitura de firmware anterior)
#define BLE_READING_FLAG_CLOCK_SET 0x01
#define BLE_READING_FLAG_BATCH     0x02
#define BLE_READING_SESSION_SHIFT  2

#define BLE_RSSI_UNKNOWN (-128)
#define BLE_TEXT_MAX 48

typedef enum {
  BLE_EVENT_ACK,
  BLE_EVENT_TEXT,
  BLE_EVENT_CLOCK,
  BLE_EVENT_CLEAR_HISTORY
} ble_event_type_t;

typedef struct {
  ble_event_type_t type;
  uint16_t seq;                  // BLE_EVENT_ACK
  uint8_t session;               // BLE_EVENT_ACK
  uint32_t clock;                // BLE_EVENT_CLOCK
  char text[BLE_TEXT_MAX + 1];   // BLE_EVENT_ACK (nome) e BLE_EVENT_TEXT
} ble_event_t;

#ifdef ESP32
  // O objeto do teclado pertence a este modulo; main.cpp usa por referencia.
  class BleKeyboard;
  BleKeyboard &ble_keyboard_instance();
#endif // ESP32

// Ha um celular conectado (teclado e/ou aplicativo).
bool ble_phone_connected();

// O aplicativo esta inscrito para receber as leituras (Notify ligado).
bool ble_app_listening();

// Envia uma leitura ao aplicativo. `flags` e o primeiro byte do pacote (ver acima).
// Retorna false se ninguem estava ouvindo.
bool ble_send_reading(uint8_t flags, uint16_t seq, const char *epc, int rssi, uint32_t timestamp);

// Retira o proximo comando recebido do celular, se houver.
bool ble_poll_event(ble_event_t &event);

// Atualiza a caracteristica de status (bateria %, leituras pendentes e tags no historico).
void ble_set_status(uint8_t battery_percent, uint16_t pending, uint16_t history);

// Desliga o Bluetooth por inteiro (o celular desconecta) para liberar memoria:
// com Bluetooth e Wi-Fi juntos nao sobra memoria para o HTTPS da API.
void ble_suspend();

// Religa o Bluetooth e volta a anunciar; o celular pareado reconecta sozinho.
void ble_resume();

bool ble_suspended();

#endif // BLE_H
