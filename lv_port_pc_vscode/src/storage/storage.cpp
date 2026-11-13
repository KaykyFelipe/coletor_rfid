#include "storage.h"
#include <string.h>

// Tres arquivos pequenos:
//   QUEUE_FILE  registros de 24 bytes, um atras do outro (so acrescenta no fim)
//   HEAD_FILE   quantos registros do inicio de QUEUE_FILE ja foram confirmados
//   SEQ_FILE    ultimo numero sequencial usado
//
// Retirar uma leitura so avanca HEAD_FILE; quando a fila esvazia, QUEUE_FILE e
// HEAD_FILE sao apagados. Se a energia cair entre a confirmacao e a gravacao de
// HEAD_FILE, a leitura e enviada de novo ao religar (mesmo seq): o celular deve
// ignorar a repetida. Nenhuma leitura se perde.

#ifdef ESP32

  #include <LittleFS.h>

  #define QUEUE_FILE "/queue.bin"
  #define HEAD_FILE "/queue.head"
  #define SEQ_FILE "/seq.bin"

  static bool fs_begin() {
    return LittleFS.begin(true);  // formata na primeira vez
  }

  static size_t fs_read(const char *path, uint8_t *data, size_t offset, size_t len) {
    if (!LittleFS.exists(path)) return 0;
    File file = LittleFS.open(path, "r");
    if (!file) return 0;
    size_t done = file.seek(offset) ? file.read(data, len) : 0;
    file.close();
    return done;
  }

  static bool fs_write(const char *path, const uint8_t *data, size_t len, bool append) {
    File file = LittleFS.open(path, append ? "a" : "w");
    if (!file) return false;
    size_t done = file.write(data, len);
    file.close();
    return done == len;
  }

  static void fs_remove(const char *path) {
    if (LittleFS.exists(path)) LittleFS.remove(path);
  }

#else // HAL

  #define QUEUE_FILE "collector_queue.bin"
  #define HEAD_FILE "collector_queue.head"
  #define SEQ_FILE "collector_seq.bin"

  static bool fs_begin() {
    return true;
  }

  static size_t fs_read(const char *path, uint8_t *data, size_t offset, size_t len) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t done = fseek(file, (long)offset, SEEK_SET) == 0 ? fread(data, 1, len, file) : 0;
    fclose(file);
    return done;
  }

  static bool fs_write(const char *path, const uint8_t *data, size_t len, bool append) {
    FILE *file = fopen(path, append ? "ab" : "wb");
    if (!file) return false;
    size_t done = fwrite(data, 1, len, file);
    fclose(file);
    return done == len;
  }

  static void fs_remove(const char *path) {
    remove(path);
  }

#endif // ESP32 / HAL

static std::vector<storage_record_t> queue;
static uint32_t head = 0;       // registros ja confirmados no inicio de QUEUE_FILE
static uint16_t last_seq = 0;
static bool persistent = false;

bool storage_init() {
  storage_record_t record;
  uint8_t raw[4];

  queue.clear();
  head = 0;
  last_seq = 0;

  persistent = fs_begin();
  if (!persistent) {
    PRINTF("STORAGE: permanent memory not available, queue only in RAM\n");
    return false;
  }

  if (fs_read(SEQ_FILE, raw, 0, 2) == 2) last_seq = (uint16_t)(raw[0] | (raw[1] << 8));
  if (fs_read(HEAD_FILE, raw, 0, 4) == 4) head = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);

  while (queue.size() < STORAGE_CAPACITY &&
         fs_read(QUEUE_FILE, (uint8_t *)&record, (head + queue.size()) * sizeof(record), sizeof(record)) == sizeof(record)) {
    record.epc[STORAGE_EPC_MAX] = '\0';
    queue.push_back(record);
  }

  // Fila vazia com arquivos sobrando (queda de energia no meio da limpeza)
  if (queue.empty() && head > 0) {
    fs_remove(QUEUE_FILE);
    fs_remove(HEAD_FILE);
    head = 0;
  }

  PRINTF("STORAGE: %u pending | last seq %u\n", (unsigned)queue.size(), last_seq);
  return true;
}

uint16_t storage_next_seq() {
  if (++last_seq == 0) last_seq = 1;  // 0 fica reservado

  if (persistent) {
    uint8_t raw[2] = {(uint8_t)(last_seq & 0xFF), (uint8_t)(last_seq >> 8)};
    if (!fs_write(SEQ_FILE, raw, sizeof(raw), false)) PRINTF("STORAGE: failed to save seq\n");
  }
  return last_seq;
}

bool storage_push(const storage_record_t &record) {
  if (queue.size() >= STORAGE_CAPACITY) return false;

  if (persistent && !fs_write(QUEUE_FILE, (const uint8_t *)&record, sizeof(record), true)) {
    PRINTF("STORAGE: failed to save reading %u\n", record.seq);
    return false;
  }
  queue.push_back(record);
  return true;
}

bool storage_peek(storage_record_t &record) {
  if (queue.empty()) return false;
  record = queue.front();
  return true;
}

void storage_pop() {
  if (queue.empty()) return;

  queue.erase(queue.begin());
  head++;
  if (!persistent) return;

  if (queue.empty()) {
    fs_remove(QUEUE_FILE);
    fs_remove(HEAD_FILE);
    head = 0;
  }
  else {
    uint8_t raw[4] = {(uint8_t)head, (uint8_t)(head >> 8), (uint8_t)(head >> 16), (uint8_t)(head >> 24)};
    if (!fs_write(HEAD_FILE, raw, sizeof(raw), false)) PRINTF("STORAGE: failed to save queue head\n");
  }
}

uint16_t storage_count() {
  return (uint16_t)queue.size();
}

bool storage_persistent() {
  return persistent;
}
