#include "ble.h"
#include <string.h>

#ifdef ESP32

#include <BleKeyboard.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Os comandos chegam na tarefa do NimBLE; a fila entrega para o loop principal.
static QueueHandle_t event_queue = NULL;
static NimBLECharacteristic *reading_chr = NULL;
static NimBLECharacteristic *status_chr = NULL;
static volatile bool app_listening = false;
static uint8_t status_value[3] = {0, 0, 0};

static void copy_text(ble_event_t &event, const uint8_t *data, size_t len) {
  if (len > BLE_TEXT_MAX) len = BLE_TEXT_MAX;
  memcpy(event.text, data, len);
  event.text[len] = '\0';
}

class CollectorCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &conn_info) override {
    NimBLEAttValue value = chr->getValue();
    const uint8_t *data = value.data();
    size_t len = value.length();
    ble_event_t event;

    if (len < 1 || event_queue == NULL) return;
    memset(&event, 0, sizeof(event));

    switch (data[0]) {
      case BLE_CMD_ACK:
        if (len < 3) return;
        event.type = BLE_EVENT_ACK;
        event.seq = (uint16_t)(data[1] | (data[2] << 8));
        copy_text(event, data + 3, len - 3);
        break;

      case BLE_CMD_TEXT:
        event.type = BLE_EVENT_TEXT;
        copy_text(event, data + 1, len - 1);
        break;

      case BLE_CMD_CLOCK:
        if (len < 5) return;
        event.type = BLE_EVENT_CLOCK;
        event.clock = (uint32_t)data[1] | ((uint32_t)data[2] << 8) | ((uint32_t)data[3] << 16) | ((uint32_t)data[4] << 24);
        break;

      default:
        return;
    }

    xQueueSend(event_queue, &event, 0);
  }

  void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &conn_info, uint16_t sub_value) override {
    if (chr == reading_chr) app_listening = (sub_value & 1) != 0;
  }
};

static CollectorCallbacks collector_callbacks;

// Teclado HID da biblioteca + servico proprio, criado no gancho onStarted(),
// que a biblioteca chama depois de montar o HID e antes de anunciar.
class CollectorKeyboard : public BleKeyboard {
  protected:
    void onStarted(BLEServer *server) override {
      event_queue = xQueueCreate(8, sizeof(ble_event_t));

      NimBLEService *service = server->createService(BLE_SERVICE_UUID);
      reading_chr = service->createCharacteristic(BLE_READING_UUID, NIMBLE_PROPERTY::NOTIFY);
      NimBLECharacteristic *command_chr = service->createCharacteristic(BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE);
      status_chr = service->createCharacteristic(BLE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

      reading_chr->setCallbacks(&collector_callbacks);
      command_chr->setCallbacks(&collector_callbacks);
      status_chr->setValue(status_value, sizeof(status_value));

      service->start();
    }

    void onDisconnect(BLEServer *server, NimBLEConnInfo &conn_info, int reason) override {
      app_listening = false;
      BleKeyboard::onDisconnect(server, conn_info, reason);
    }
};

BleKeyboard &ble_keyboard_instance() {
  static CollectorKeyboard keyboard;
  return keyboard;
}

bool ble_phone_connected() {
  return ble_keyboard_instance().isConnected();
}

bool ble_app_listening() {
  return ble_phone_connected() && app_listening;
}

bool ble_send_reading(uint16_t seq, const char *epc, int rssi, uint32_t timestamp, bool clock_set) {
  // 8 bytes de cabecalho + EPC. Com EPC de ate 12 caracteres cabe em 20 bytes,
  // o limite de uma notificacao sem negociar MTU.
  uint8_t packet[8 + 32];
  size_t epc_len = strlen(epc);

  if (reading_chr == NULL || !ble_app_listening()) return false;
  if (epc_len > 32) epc_len = 32;

  packet[0] = clock_set ? BLE_READING_FLAG_CLOCK_SET : 0;
  packet[1] = seq & 0xFF;
  packet[2] = seq >> 8;
  packet[3] = (uint8_t)(int8_t)(rssi < -127 || rssi > 127 ? BLE_RSSI_UNKNOWN : rssi);
  packet[4] = timestamp & 0xFF;
  packet[5] = (timestamp >> 8) & 0xFF;
  packet[6] = (timestamp >> 16) & 0xFF;
  packet[7] = (timestamp >> 24) & 0xFF;
  memcpy(packet + 8, epc, epc_len);

  return reading_chr->notify(packet, 8 + epc_len);
}

bool ble_poll_event(ble_event_t &event) {
  if (event_queue == NULL) return false;
  return xQueueReceive(event_queue, &event, 0) == pdTRUE;
}

void ble_set_status(uint8_t battery_percent, uint16_t pending) {
  uint8_t value[3] = {battery_percent, (uint8_t)(pending & 0xFF), (uint8_t)(pending >> 8)};

  if (status_chr == NULL || memcmp(value, status_value, sizeof(value)) == 0) return;
  memcpy(status_value, value, sizeof(value));
  status_chr->setValue(status_value, sizeof(status_value));
  if (ble_phone_connected()) status_chr->notify();
}

#else // HAL

// Simulador de PC: nao ha Bluetooth. O interruptor "BLE" da janela faz o papel
// do celular, que confirma cada leitura 400 ms depois com um nome ficticio.
// Para testar a falta de resposta (2 s), toda leitura de numero multiplo de 3 fica sem confirmacao.

#define SIM_ACK_DELAY_MS 400

static bool ack_scheduled = false;
static uint64_t ack_at_ms = 0;
static uint16_t ack_seq = 0;

bool ble_phone_connected() {
  return ble_is_connected();
}

bool ble_app_listening() {
  return ble_is_connected();
}

bool ble_send_reading(uint16_t seq, const char *epc, int rssi, uint32_t timestamp, bool clock_set) {
  if (!ble_app_listening()) return false;

  PRINTF("BLE SIM: reading seq %u | %s | rssi %d | ts %u | clock %d\n", seq, epc, rssi, (unsigned)timestamp, clock_set);
  ack_scheduled = (seq % 3) != 0;
  ack_at_ms = millis() + SIM_ACK_DELAY_MS;
  ack_seq = seq;
  return true;
}

bool ble_poll_event(ble_event_t &event) {
  if (!ack_scheduled || (uint64_t)millis() < ack_at_ms) return false;

  ack_scheduled = false;
  memset(&event, 0, sizeof(event));
  event.type = BLE_EVENT_ACK;
  event.seq = ack_seq;
  strcpy(event.text, "Betoneira 400L");
  return true;
}

void ble_set_status(uint8_t battery_percent, uint16_t pending) {
  static int last = -1;
  int value = battery_percent | (pending << 8);

  if (value == last) return;
  last = value;
  PRINTF("BLE SIM: status battery %u%% | pending %u\n", battery_percent, pending);
}

#endif // ESP32 / HAL
