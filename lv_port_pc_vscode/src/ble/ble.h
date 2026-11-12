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
#define BLE_CMD_ACK   0x01  // seq (u16) + nome do equipamento (UTF-8, opcional)
#define BLE_CMD_TEXT  0x02  // texto (UTF-8) para mostrar na tela
#define BLE_CMD_CLOCK 0x03  // relogio: segundos desde 1970 (u32)

// Bit 0 do primeiro byte da leitura: o horario veio do relogio acertado pelo celular
#define BLE_READING_FLAG_CLOCK_SET 0x01

#define BLE_RSSI_UNKNOWN (-128)
#define BLE_TEXT_MAX 48

typedef enum {
  BLE_EVENT_ACK,
  BLE_EVENT_TEXT,
  BLE_EVENT_CLOCK
} ble_event_type_t;

typedef struct {
  ble_event_type_t type;
  uint16_t seq;                  // BLE_EVENT_ACK
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

// Envia uma leitura ao aplicativo. Retorna false se ninguem estava ouvindo.
bool ble_send_reading(uint16_t seq, const char *epc, int rssi, uint32_t timestamp, bool clock_set);

// Retira o proximo comando recebido do celular, se houver.
bool ble_poll_event(ble_event_t &event);

// Atualiza a caracteristica de status (bateria % e leituras pendentes).
void ble_set_status(uint8_t battery_percent, uint16_t pending);

#endif // BLE_H
