#include "storage.h"
#include <string.h>
#include <stddef.h>

// Arquivos pequenos:
//   QUEUE_FILE    registros de 24 bytes, um atras do outro (so acrescenta no fim)
//   HEAD_FILE     quantos registros do inicio de QUEUE_FILE ja foram confirmados
//   SEQ_FILE      ultimo numero sequencial usado
//   SESSION_FILE  sessao de leitura atual e o modo dela
//
// Retirar a leitura mais antiga so avanca HEAD_FILE; quando a fila esvazia,
// QUEUE_FILE e HEAD_FILE sao apagados. Retirar do meio da fila (confirmacao fora
// de ordem, sessao cancelada) regrava a fila inteira em QUEUE_TMP_FILE e troca
// os arquivos. Se a energia cair no meio de qualquer um dos dois, o pior caso e
// uma leitura ja confirmada voltar para a fila e ser enviada de novo (mesmo
// seq): o celular deve ignorar a repetida. Nenhuma leitura pendente se perde.

#ifdef ESP32

  #include <LittleFS.h>

  #define QUEUE_FILE "/queue.bin"
  #define QUEUE_TMP_FILE "/queue.tmp"
  #define HEAD_FILE "/queue.head"
  #define SEQ_FILE "/seq.bin"
  #define SESSION_FILE "/session.bin"

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

  static bool fs_exists(const char *path) {
    return LittleFS.exists(path);
  }

  static bool fs_rename(const char *from, const char *to) {
    return LittleFS.rename(from, to);
  }

  // Altera bytes no meio de um arquivo que ja existe.
  static bool fs_patch(const char *path, const uint8_t *data, size_t offset, size_t len) {
    if (!LittleFS.exists(path)) return false;
    File file = LittleFS.open(path, "r+");
    if (!file) return false;
    size_t done = file.seek(offset) ? file.write(data, len) : 0;
    file.close();
    return done == len;
  }

#else // HAL

  #define QUEUE_FILE "collector_queue.bin"
  #define QUEUE_TMP_FILE "collector_queue.tmp"
  #define HEAD_FILE "collector_queue.head"
  #define SEQ_FILE "collector_seq.bin"
  #define SESSION_FILE "collector_session.bin"

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

  static bool fs_exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file) fclose(file);
    return file != NULL;
  }

  static bool fs_rename(const char *from, const char *to) {
    return rename(from, to) == 0;
  }

  static bool fs_patch(const char *path, const uint8_t *data, size_t offset, size_t len) {
    FILE *file = fopen(path, "r+b");
    if (!file) return false;
    size_t done = fseek(file, (long)offset, SEEK_SET) == 0 ? fwrite(data, 1, len, file) : 0;
    fclose(file);
    return done == len;
  }

#endif // ESP32 / HAL

static std::vector<storage_record_t> queue;
static uint32_t head = 0;       // registros ja confirmados no inicio de QUEUE_FILE
static uint16_t last_seq = 0;
static bool persistent = false;

static void history_load();

bool storage_init() {
  storage_record_t record;
  uint8_t raw[4];

  queue.clear();
  head = 0;
  last_seq = 0;

  persistent = fs_begin();
  if (!persistent) {
    PRINTF("STORAGE: permanent memory not available, queue only in RAM\n");
    history_load();
    return false;
  }

  if (fs_read(SEQ_FILE, raw, 0, 2) == 2) last_seq = (uint16_t)(raw[0] | (raw[1] << 8));
  // Queda de energia no meio de queue_rewrite(): a fila nova ficou sem ser renomeada.
  if (!fs_exists(QUEUE_FILE) && fs_exists(QUEUE_TMP_FILE)) {
    fs_remove(HEAD_FILE);
    fs_rename(QUEUE_TMP_FILE, QUEUE_FILE);
  }
  fs_remove(QUEUE_TMP_FILE);

  if (fs_read(HEAD_FILE, raw, 0, 4) == 4) head = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);

  while (queue.size() < STORAGE_CAPACITY &&
         fs_read(QUEUE_FILE, (uint8_t *)&record, (head + queue.size()) * sizeof(record), sizeof(record)) == sizeof(record)) {
    record.epc[STORAGE_EPC_MAX] = '\0';
    if (record.session > STORAGE_SESSION_MAX) record.session = 0;
    queue.push_back(record);
  }

  // Fila vazia com arquivos sobrando (queda de energia no meio da limpeza)
  if (queue.empty() && head > 0) {
    fs_remove(QUEUE_FILE);
    fs_remove(HEAD_FILE);
    head = 0;
  }

  history_load();

  PRINTF("STORAGE: %u pending | last seq %u | history %u\n", (unsigned)queue.size(), last_seq, (unsigned)history_count());
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

bool storage_get(size_t index, storage_record_t &record) {
  if (index >= queue.size()) return false;
  record = queue[index];
  return true;
}

int storage_find(uint16_t seq) {
  for (size_t i = 0; i < queue.size(); i++) {
    if (queue[i].seq == seq) return (int)i;
  }
  return -1;
}

bool storage_has_epc(const char *epc) {
  for (const storage_record_t &record : queue) {
    if (strcmp(record.epc, epc) == 0) return true;
  }
  return false;
}

// Retira a leitura mais antiga: so avanca HEAD_FILE.
static void queue_pop_front() {
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

// Regrava a fila inteira, depois de retirar registros do meio dela.
static void queue_rewrite() {
  if (!persistent) {
    head = 0;
    return;
  }

  fs_remove(QUEUE_TMP_FILE);
  if (!queue.empty() &&
      !fs_write(QUEUE_TMP_FILE, (const uint8_t *)queue.data(), queue.size() * sizeof(storage_record_t), false)) {
    // A fila antiga continua valida no arquivo; os registros retirados voltam ao religar.
    PRINTF("STORAGE: failed to rewrite queue\n");
    fs_remove(QUEUE_TMP_FILE);
    return;
  }

  // Nesta ordem, uma queda de energia nunca deixa HEAD_FILE apontando para a fila nova.
  fs_remove(HEAD_FILE);
  fs_remove(QUEUE_FILE);
  if (!queue.empty() && !fs_rename(QUEUE_TMP_FILE, QUEUE_FILE)) PRINTF("STORAGE: failed to replace queue\n");
  head = 0;
}

bool storage_remove(uint16_t seq) {
  int index = storage_find(seq);

  if (index < 0) return false;
  if (index == 0) {
    queue_pop_front();
  }
  else {
    queue.erase(queue.begin() + index);
    queue_rewrite();
  }
  return true;
}

void storage_clear() {
  queue.clear();
  queue_rewrite();
}

// Liga um bit de `flags` na leitura, tambem no arquivo.
static void queue_set_flag(int index, uint8_t flag) {
  if (queue[index].flags & flag) return;
  queue[index].flags |= flag;

  if (persistent && !fs_patch(QUEUE_FILE, &queue[index].flags,
                              (head + index) * sizeof(storage_record_t) + offsetof(storage_record_t, flags), 1)) {
    PRINTF("STORAGE: failed to save flags of reading %u\n", queue[index].seq);
  }
}

void storage_mark_sent(uint16_t seq) {
  int index = storage_find(seq);

  if (index >= 0) queue_set_flag(index, STORAGE_FLAG_SENT);
}

bool storage_complete(uint16_t seq, uint8_t done_flag) {
  int index = storage_find(seq);

  if (index < 0) return false;
  if ((queue[index].flags | done_flag) & STORAGE_FLAG_BLE_DONE && (queue[index].flags | done_flag) & STORAGE_FLAG_API_DONE) {
    storage_remove(seq);  // entregue pelos dois canais: sai da memoria
    return true;
  }
  queue_set_flag(index, done_flag);
  return false;
}

uint16_t storage_cancel_session(uint8_t session) {
  uint16_t cancelled = 0;

  for (size_t i = queue.size(); i-- > 0;) {
    storage_record_t &record = queue[i];
    if (record.session != session || (record.flags & (STORAGE_FLAG_SENT | STORAGE_FLAG_BLE_DONE))) continue;

    cancelled++;
    if (record.flags & STORAGE_FLAG_API_DONE) queue.erase(queue.begin() + i);
    else record.flags |= STORAGE_FLAG_BLE_DONE;  // continua pendente so para a API
  }
  if (cancelled > 0) queue_rewrite();
  return cancelled;
}

void storage_session_load(uint8_t &session, uint8_t &mode) {
  uint8_t raw[2];

  session = 0;
  mode = 0;
  if (persistent && fs_read(SESSION_FILE, raw, 0, 2) == 2 && raw[0] <= STORAGE_SESSION_MAX) {
    session = raw[0];
    mode = raw[1];
  }
}

void storage_session_save(uint8_t session, uint8_t mode) {
  uint8_t raw[2] = {session, mode};

  if (persistent && !fs_write(SESSION_FILE, raw, sizeof(raw), false)) PRINTF("STORAGE: failed to save session\n");
}

uint16_t storage_count() {
  return (uint16_t)queue.size();
}

uint16_t storage_count_where(uint8_t mask, uint8_t value) {
  uint16_t count = 0;

  for (const storage_record_t &record : queue) {
    if ((record.flags & mask) == value) count++;
  }
  return count;
}

bool storage_persistent() {
  return persistent;
}

/**********************
 *  Historico de EPCs (controle de duplicidade)
 **********************
 * HISTORY_FILE guarda um registro de 16 bytes por EPC ja registrado, do mais
 * antigo para o mais novo. Na RAM fica so um resumo de 4 bytes por EPC; o
 * arquivo e consultado apenas quando o resumo coincide, para comparar o EPC
 * completo. Assim a consulta nao atrasa o inventario.
 */

#ifdef ESP32
  #define HISTORY_FILE "/history.bin"
  #define HISTORY_TMP_FILE "/history.tmp"
#else // HAL
  #define HISTORY_FILE "collector_history.bin"
  #define HISTORY_TMP_FILE "collector_history.tmp"
#endif // ESP32 / HAL

#define HISTORY_RECORD 16
#define HISTORY_CHUNK 32  // registros lidos/gravados por vez

static std::vector<uint32_t> history_hashes;

// Registro de tamanho fixo: EPC completo, preenchido com zeros.
static void history_pack(const char *epc, uint8_t *record) {
  memset(record, 0, HISTORY_RECORD);
  strncpy((char *)record, epc, HISTORY_RECORD - 1);
}

// FNV-1a de 32 bits sobre o registro
static uint32_t history_hash(const uint8_t *record) {
  uint32_t hash = 2166136261UL;
  for (int i = 0; i < HISTORY_RECORD; i++) {
    hash ^= record[i];
    hash *= 16777619UL;
  }
  return hash;
}

static void history_load() {
  uint8_t chunk[HISTORY_RECORD * HISTORY_CHUNK];
  size_t done;

  history_hashes.clear();
  history_hashes.reserve(HISTORY_CAPACITY);
  if (!persistent) return;

  // Queda de energia no meio de history_trim(): o arquivo novo ficou sem ser renomeado.
  if (!fs_exists(HISTORY_FILE) && fs_exists(HISTORY_TMP_FILE)) fs_rename(HISTORY_TMP_FILE, HISTORY_FILE);
  fs_remove(HISTORY_TMP_FILE);

  while (history_hashes.size() < HISTORY_CAPACITY &&
         (done = fs_read(HISTORY_FILE, chunk, history_hashes.size() * HISTORY_RECORD, sizeof(chunk))) >= HISTORY_RECORD) {
    for (size_t i = 0; i + HISTORY_RECORD <= done && history_hashes.size() < HISTORY_CAPACITY; i += HISTORY_RECORD) {
      history_hashes.push_back(history_hash(chunk + i));
    }
  }
}

// Historico cheio: descarta os HISTORY_TRIM registros mais antigos.
static void history_trim() {
  uint8_t chunk[HISTORY_RECORD * HISTORY_CHUNK];
  size_t offset = HISTORY_TRIM * HISTORY_RECORD, done;
  bool first = true, ok = true;

  history_hashes.erase(history_hashes.begin(), history_hashes.begin() + HISTORY_TRIM);
  if (!persistent) return;

  while (ok && (done = fs_read(HISTORY_FILE, chunk, offset, sizeof(chunk))) > 0) {
    ok = fs_write(HISTORY_TMP_FILE, chunk, done, !first);
    first = false;
    offset += done;
  }

  if (ok) {
    fs_remove(HISTORY_FILE);
    ok = fs_rename(HISTORY_TMP_FILE, HISTORY_FILE);
  }
  if (!ok) PRINTF("STORAGE: failed to trim history\n");
  PRINTF("STORAGE: history trimmed to %u\n", (unsigned)history_hashes.size());
}

bool history_contains(const char *epc) {
  uint8_t record[HISTORY_RECORD], stored[HISTORY_RECORD];
  uint32_t hash;

  history_pack(epc, record);
  hash = history_hash(record);

  for (size_t i = 0; i < history_hashes.size(); i++) {
    if (history_hashes[i] != hash) continue;
    // Resumo igual: confere o EPC completo no arquivo. Na duvida, considera repetida.
    if (!persistent || fs_read(HISTORY_FILE, stored, i * HISTORY_RECORD, HISTORY_RECORD) != HISTORY_RECORD) return true;
    if (memcmp(stored, record, HISTORY_RECORD) == 0) return true;
  }
  return false;
}

bool history_add(const char *epc) {
  uint8_t record[HISTORY_RECORD];

  if (history_contains(epc)) return false;
  if (history_hashes.size() >= HISTORY_CAPACITY) history_trim();

  history_pack(epc, record);
  if (persistent && !fs_write(HISTORY_FILE, record, HISTORY_RECORD, true)) {
    PRINTF("STORAGE: failed to save history of %s\n", epc);
  }
  history_hashes.push_back(history_hash(record));
  return true;
}

// As leituras ainda pendentes continuam bloqueadas: quem le consulta tambem a fila.
void history_clear() {
  history_hashes.clear();
  if (persistent) {
    fs_remove(HISTORY_FILE);
    fs_remove(HISTORY_TMP_FILE);
  }
  PRINTF("STORAGE: history cleared\n");
}

uint16_t history_count() {
  return (uint16_t)history_hashes.size();
}
