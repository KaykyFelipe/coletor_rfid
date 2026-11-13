#include "rfid.h"

#if !RFID_SIMULATED

// Leitor real: modulo RFID UHF na Serial2, com o mesmo protocolo ASCII do
// firmware anterior. A porta, os pinos, a potencia e a faixa de frequencia
// continuam sendo configurados em init_antenna() (main.cpp), sem alteracao.
//
// Comando de inventario:  "\nU\r"
// Resposta: uma linha por tag, "\nU" + PC (4 hex) + EPC (hex) + CRC (4 hex) + "\r",
// e uma linha so com "U" (ou iniciada por "X") no fim da rodada.
//
// O modulo nao informa RSSI: a melhor tag e a que respondeu mais vezes.
// Nada aqui bloqueia: os bytes sao consumidos conforme chegam, a cada rfid_poll().

#define RFID_LINE_MAX 96
#define RFID_ROUND_GAP_MS 20       // pausa entre o fim de uma rodada e o proximo comando
#define RFID_ROUND_TIMEOUT_MS 500  // rodada sem fim: manda o comando de novo

static char line[RFID_LINE_MAX + 1];
static size_t line_len = 0;
static uint64_t command_ms = 0, round_end_ms = 0;
static bool round_open = false;

static void send_inventory(uint64_t now_ms) {
  Serial2.write("\nU\r");
  command_ms = now_ms;
  round_open = true;
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Linha completa, sem "\r" nem "\n". Devolve true se a rodada terminou.
static bool parse_line(std::vector<rfid_tag_t> &reads) {
  if (line_len == 0) return false;
  if (line[0] == 'X') return true;   // resposta de erro / sem tag
  if (line[0] != 'U') return false;
  if (line_len == 1) return true;    // "U" sozinho: fim da rodada

  // U + PC (4) + EPC (>= 2) + CRC (4)
  if (line_len < 1 + 4 + 2 + 4) return false;

  rfid_tag_t read;
  for (size_t i = 5; i + 1 < line_len - 4; i += 2) {
    int high = hex_value(line[i]), low = hex_value(line[i + 1]);
    if (high < 0 || low < 0) return false;  // linha corrompida
    char chr = (char)((high << 4) | low);
    read.epc += (chr < ' ' || chr > '~') ? '.' : chr;
  }
  read.rssi = RFID_NO_RSSI;
  read.reads = 1;
  reads.push_back(read);
  return false;
}

void rfid_backend_init() {
  PRINTF("RFID: real reader (Serial2)\n");
}

void rfid_backend_start(uint64_t now_ms) {
  while (Serial2.available()) Serial2.read();  // descarta restos de leituras antigas
  line_len = 0;
  send_inventory(now_ms);
}

void rfid_backend_poll(uint64_t now_ms, std::vector<rfid_tag_t> &reads) {
  while (Serial2.available()) {
    char chr = (char)Serial2.read();

    if (chr == '\n' || chr == '\r') {
      line[line_len] = '\0';
      if (parse_line(reads)) {
        round_open = false;
        round_end_ms = now_ms;
      }
      line_len = 0;
    }
    else if (line_len < RFID_LINE_MAX) {
      line[line_len++] = chr;
    }
  }

  if (round_open) {
    if (now_ms - command_ms >= RFID_ROUND_TIMEOUT_MS) send_inventory(now_ms);
  }
  else if (now_ms - round_end_ms >= RFID_ROUND_GAP_MS) {
    send_inventory(now_ms);
  }
}

void rfid_backend_stop() {
  round_open = false;
}

#endif // !RFID_SIMULATED
