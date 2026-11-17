#include "net.h"
#include <string.h>

// Corpo no formato do contrato da API: uma leitura por POST, dentro de
// "tag_reads". Mudam o epc (a identificacao da tag, em texto), o horario
// (microssegundos desde 1970, UTC) e o mac (o do proprio coletor). Os outros
// campos sao fixos, copiados do modelo.
size_t net_build_body(char *out, size_t size, const char *epc, uint64_t timestamp_us, const char *mac) {
  int len = snprintf(out, size,
    "{\"readerName\":\"%s\","
    "\"mac\":\"%s\","
    "\"site\":\"%s\","
    "\"tag_reads\":[{"
    "\"epc\":\"%s\","
    "\"isHeartBeat\":false,"
    "\"isInventoryStatus\":false,"
    "\"firstSeenTimestamp\":%lu%06lu,"
    "\"antennaPort\":%d,"
    "\"peakRssi\":-45.5,"
    "\"txPower\":30}]}",
    API_READER_NAME, mac, API_SITE, epc,
    (unsigned long)(timestamp_us / 1000000ULL), (unsigned long)(timestamp_us % 1000000ULL), API_ANTENNA_PORT);

  return len < 0 ? 0 : (size_t)len < size ? (size_t)len : size - 1;
}

#ifdef ESP32

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

// Memoria IRAM: com Bluetooth e Wi-Fi juntos o ESP32 fica no limite dessa
// regiao e o firmware deixa de linkar ("iram0_0_seg overflowed"). Por isso
// este arquivo NAO usa a biblioteca HTTPClient (puxa strptime e mktime, 3 KB
// de IRAM) nem configTime() (puxa tzset): o POST e escrito direto sobre a
// conexao TLS e o horario e pedido direto ao SNTP.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Raiz da Let's Encrypt que assina o certificado de api-app.ativalocacao.com.br
// (ISRG Root X2, ECDSA, valida ate 2040). So ela, e nao tambem a X1 (RSA 4096):
// cada raiz ocupa memoria durante a conexao, e com Bluetooth e Wi-Fi ligados a
// memoria livre mal chega para o HTTPS. Se a API trocar de autoridade
// certificadora, o envio passa a falhar ("API com problemas") ate esta raiz ser
// atualizada.
static const char API_ROOT_CA[] =
  "-----BEGIN CERTIFICATE-----\n"
  "MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw\n"
  "CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg\n"
  "R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00\n"
  "MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT\n"
  "ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw\n"
  "EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW\n"
  "+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9\n"
  "ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T\n"
  "AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI\n"
  "zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW\n"
  "tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1\n"
  "/q4AaOeMSQ+2b1tbFfLn\n"
  "-----END CERTIFICATE-----\n";

static TaskHandle_t task = NULL;
static volatile net_post_t post_state = NET_POST_IDLE;
static char post_epc[40];

// Wi-Fi sob demanda: fica desligado e so liga durante um envio (net_on/net_off).
// Motivo: a bateria e medida pelo ADC2, que o ESP32 nao deixa ler com o Wi-Fi
// ligado. Quem liga e desliga o radio e a tarefa net_api; o loop principal so
// pede (wifi_wanted).
static volatile bool wifi_wanted = false, wifi_on = false;
static bool wifi_up = false;

// Horario UTC em microssegundos desde 1970 (a maior precisao do relogio).
static uint64_t now_us() {
  struct timeval now;

  gettimeofday(&now, NULL);
  return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_usec;
}

// Le uma linha da resposta (sem o fim de linha). Devolve false se o prazo
// acabou ou a conexao caiu antes de a linha chegar inteira.
static bool read_line(WiFiClientSecure &client, char *line, size_t size, uint32_t deadline) {
  size_t len = 0;

  for (;;) {
    int c = client.read();
    if (c < 0) {
      if (!client.connected() || (int32_t)(millis() - deadline) >= 0) return false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (c == '\n') break;
    if (c != '\r' && len < size - 1) line[len++] = (char)c;
  }
  line[len] = '\0';
  return true;
}

// Descarta `count` bytes do corpo da resposta, para a conexao poder ser reaproveitada.
static bool skip_body(WiFiClientSecure &client, long count, uint32_t deadline) {
  while (count > 0) {
    if (client.read() >= 0) {
      count--;
    }
    else {
      if (!client.connected() || (int32_t)(millis() - deadline) >= 0) return false;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  return true;
}

// Um POST HTTPS. Devolve o codigo HTTP da resposta, ou -1 se nao houve resposta.
static int http_post(WiFiClientSecure &client, const char *body, size_t len) {
  char line[160];

  for (int attempt = 0; attempt < 2; attempt++) {
    // A conexao aberta pelo envio anterior e reaproveitada (evita refazer o TLS a cada tag).
    bool reused = client.connected();
    uint32_t deadline = millis() + API_TIMEOUT_MS;
    long content_length = -1;

    if (!reused && !client.connect(API_HOST, API_PORT, API_TIMEOUT_MS)) {
      // Diagnostico: motivo da falha do TLS e memoria livre (total e maior bloco).
      client.lastError(line, sizeof(line));
      PRINTF("API: connect failed | %s | heap %u | largest block %u\n", line, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
      return -1;
    }

    client.printf("POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: keep-alive\r\n\r\n",
                  API_PATH, API_HOST, (unsigned)len);
    client.write((const uint8_t *)body, len);

    bool got_line = read_line(client, line, sizeof(line), deadline);
    if (!got_line || strncmp(line, "HTTP/1.", 7) != 0 || strlen(line) < 12) {
      PRINTF("API: bad response | reused %d | %s\n", reused, got_line ? line : "(nothing)");
      client.stop();
      // A conexao reaproveitada pode ter sido fechada pelo servidor: tenta uma vez com conexao nova.
      if (reused) continue;
      return -1;
    }
    int code = atoi(line + 9);
    PRINTF("API: response %s | heap %u\n", line, (unsigned)ESP.getFreeHeap());

    // Cabecalhos, ate a linha vazia
    bool headers_ok = false;
    while (read_line(client, line, sizeof(line), deadline)) {
      if (line[0] == '\0') {
        headers_ok = true;
        break;
      }
      if (strncasecmp(line, "Content-Length:", 15) == 0) content_length = atol(line + 15);
    }

    // So mantem a conexao se o corpo da resposta foi lido ate o fim.
    if (!headers_ok || content_length < 0 || !skip_body(client, content_length, deadline)) client.stop();
    return code;
  }
  return -1;
}

static void wifi_power(WiFiClientSecure &client, bool on) {
  if (on) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // nao espera: a conexao acontece em segundo plano
    PRINTF("WIFI: connecting to %s\n", WIFI_SSID);
  }
  else {
    client.stop();
    if (esp_sntp_enabled()) esp_sntp_stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // radio desligado: o ADC2 (bateria) volta a funcionar
    PRINTF("WIFI: off\n");
  }
  wifi_on = on;
}

// Tarefa separada: ligar o Wi-Fi e fazer o POST HTTPS leva de 1 a varios
// segundos e nao pode parar a tela, os botoes nem o leitor RFID.
static void net_task(void *) {
  static WiFiClientSecure client;
  char body[480];

  client.setCACert(API_ROOT_CA);  // HTTPS com validacao do certificado

  for (;;) {
    // Enquanto espera a conexao, olha a cada 200 ms; parado, a cada 5 s.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wifi_on && !esp_sntp_enabled() ? 200 : 5000));

    if (wifi_wanted != wifi_on) wifi_power(client, wifi_wanted);

    if (wifi_on && !esp_sntp_enabled() && WiFi.status() == WL_CONNECTED) {
      // Horario pela internet (UTC). Sem ele, nada e enviado.
      esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
      esp_sntp_setservername(0, "a.st1.ntp.br");
      esp_sntp_setservername(1, "pool.ntp.org");
      esp_sntp_init();
    }

    if (post_state != NET_POST_BUSY) {
      // Sem pedido por 5 s: fecha a conexao para devolver a memoria do TLS.
      if (client.connected()) client.stop();
      continue;
    }

    int code = -1;
    if (wifi_on && WiFi.status() == WL_CONNECTED && net_time_valid()) {
      size_t len = net_build_body(body, sizeof(body), post_epc, now_us(), WiFi.macAddress().c_str());
      code = http_post(client, body, len);
      if (code < 200 || code >= 300) client.stop();
    }

    PRINTF("API: %s -> %d\n", post_epc, code);
    post_state = code >= 200 && code < 300 ? NET_POST_OK : NET_POST_FAIL;
  }
}

void net_init() {
  // So cria a tarefa. O Wi-Fi fica desligado ate o primeiro envio (net_on).
  xTaskCreatePinnedToCore(net_task, "net_api", 12288, NULL, 1, &task, 0);
}

void net_on() {
  wifi_wanted = true;
  if (task != NULL) xTaskNotifyGive(task);
}

void net_off() {
  wifi_wanted = false;
  if (task != NULL) xTaskNotifyGive(task);
}

bool net_radio_off() {
  return !wifi_wanted && !wifi_on;
}

void net_loop(uint64_t now_ms) {
  (void)now_ms;
  bool up = net_wifi_connected();

  if (up != wifi_up) {
    wifi_up = up;
    if (up) PRINTF("WIFI: connected, ip %s\n", WiFi.localIP().toString().c_str());
    else PRINTF("WIFI: disconnected\n");
  }
}

bool net_wifi_connected() {
  return wifi_wanted && wifi_on && WiFi.status() == WL_CONNECTED;
}

bool net_time_valid() {
  return time(NULL) > 1700000000;  // depois de 2023: o relogio ja foi acertado
}

bool net_post_start(const char *epc) {
  if (task == NULL || post_state == NET_POST_BUSY) return false;

  strncpy(post_epc, epc, sizeof(post_epc) - 1);
  post_epc[sizeof(post_epc) - 1] = '\0';
  post_state = NET_POST_BUSY;
  xTaskNotifyGive(task);
  return true;
}

net_post_t net_post_result() {
  net_post_t result = post_state;

  if (result == NET_POST_OK || result == NET_POST_FAIL) post_state = NET_POST_IDLE;
  return result;
}

#else // HAL

#include <cstdlib>
#include <ctime>

#define SIM_POST_MS 300

static net_post_t post_state = NET_POST_IDLE;
static uint64_t post_done_ms = 0;
static bool post_ok = false;
static int posts = 0;
static char last_body[480] = "";
static bool wifi_on = false;

static bool env_is(const char *name, const char *value) {
  const char *found = getenv(name);
  return found != NULL && strcmp(found, value) == 0;
}

void net_init() {
}

void net_on() {
  wifi_on = true;
  PRINTF("WIFI SIM: on\n");
}

void net_off() {
  if (wifi_on) PRINTF("WIFI SIM: off\n");
  wifi_on = false;
}

bool net_radio_off() {
  return !wifi_on;
}

bool net_sim_wifi_on() {
  return wifi_on;
}

void net_loop(uint64_t now_ms) {
  (void)now_ms;
}

bool net_wifi_connected() {
  return wifi_on && !env_is("SIM_WIFI", "off");
}

bool net_time_valid() {
  return !env_is("SIM_TIME", "off");
}

bool net_post_start(const char *epc) {
  const char *fail_after = getenv("SIM_API_FAIL_AFTER");
  time_t seconds = time(NULL);

  if (post_state == NET_POST_BUSY) return false;

  net_build_body(last_body, sizeof(last_body), epc, (uint64_t)seconds * 1000000ULL, "00:00:00:00:00:00");

  post_ok = net_wifi_connected() && !env_is("SIM_API", "fail") && !(fail_after != NULL && posts >= atoi(fail_after));
  if (post_ok) posts++;
  PRINTF("API SIM: %s -> %s\n", last_body, post_ok ? "200" : "fail");

  post_state = NET_POST_BUSY;
  post_done_ms = (uint64_t)millis() + SIM_POST_MS;
  return true;
}

net_post_t net_post_result() {
  if (post_state != NET_POST_BUSY) return NET_POST_IDLE;
  if ((uint64_t)millis() < post_done_ms) return NET_POST_BUSY;

  post_state = NET_POST_IDLE;
  return post_ok && net_wifi_connected() ? NET_POST_OK : NET_POST_FAIL;
}

const char *net_sim_last_body() {
  return last_body;
}

int net_sim_posts() {
  return posts;
}

#endif // ESP32 / HAL
