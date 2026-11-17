// Teste sem janela da maquina de estados: app + fila + leitor simulado +
// celular simulado + Wi-Fi/API simulados, com botoes, tela e relogio falsos.
//
// Compilar e rodar (terminal MSYS2 UCRT64, na pasta lv_port_pc_vscode):
//   g++ -std=gnu++17 -I. -o tests/app_test.exe tests/app_test.cpp src/state/app.cpp src/storage/storage.cpp src/rfid/rfid.cpp src/rfid/rfid_sim.cpp src/ble/ble.cpp src/net/net.cpp
//   ./tests/app_test.exe                               # leitura, Bluetooth, duplo clique, troca de modo
//   SIM_KEYBOARD_ONLY=1 ./tests/app_test.exe keyboard   # celular so com o teclado
//   ./tests/app_test.exe api                           # Wi-Fi e API (botao C)
// Cada modo termina com "ALL TESTS PASSED" ou "TESTS FAILED" (e codigo de saida 1).
// O teste apaga e recria os arquivos collector_* da pasta atual (fila do simulador).
#include "main.h"
#include "src/ble/ble.h"
#include "src/buttons/buttons.h"
#include "src/display/display.h"
#include "src/feedback/feedback.h"
#include "src/power/power.h"
#include "src/state/app.h"
#include "src/storage/storage.h"
#include "src/net/net.h"
#include <cstring>

lv_obj_t *screen, *__no_click;

static int64_t now = 1000;
static bool phone = false;
static std::vector<STRING> range;
static int typed = 0;

int64_t millis() { return now; }
void delay(int64_t ms) { now += ms; }
float get_battery_percent() { return 80; }
bool ble_is_connected() { return phone; }
std::vector<STRING> antenna_read(bool, STRING) { return range; }
bool send_data_via_ble(STRING data, int, bool send_endl) {
  if (send_endl && phone) { typed++; printf("    KEYBOARD: %s\n", data.c_str()); }
  return phone;
}

static display_screen_t shown = DISPLAY_READY;
static STRING shown_text;
static int shown_count = 0;
static bool saw_preparing = false, saw_sending = false;
void display_show(display_screen_t which, const char *text, int count) {
  shown = which; shown_text = text ? text : ""; shown_count = count;
  for (char &c : shown_text) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);   // compara sem diferenciar maiusculas
  if (shown_text == "preparando\nenvio...") saw_preparing = true;
  if (which == DISPLAY_SENDING) saw_sending = true;
}
display_screen_t display_current() { return shown; }

static bool down[3], ignoring = false;
bool button_is_down(buttons_t b) { return !ignoring && down[b]; }
bool button_was_pressed(buttons_t) { return false; }
bool button_was_released(buttons_t) { return false; }
bool buttons_any_down() { return down[0] || down[1] || down[2]; }
void buttons_ignore_until_release() { ignoring = true; }

void feedback_init() {}
void feedback_read_ok() {}
void feedback_error() {}
void power_activity(uint64_t) {}

// Fila do Bluetooth
static uint16_t storage_count_batch() { return storage_count_where(STORAGE_FLAG_BLE_DONE | STORAGE_FLAG_BATCH, STORAGE_FLAG_BATCH); }
static int ble_pending() { return storage_count_where(STORAGE_FLAG_BLE_DONE, 0); }
static int api_pending() { return storage_count_where(STORAGE_FLAG_API_DONE, 0); }

// Nos testes so de Bluetooth, a fila da API e dada como entregue na hora, para
// a leitura sair da memoria assim que o celular confirma (como antes da API).
static bool api_auto = true;
static void api_drain() {
  storage_record_t record;
  if (!api_auto) return;
  for (size_t i = 0; storage_get(i, record); i++) {
    if (!(record.flags & (STORAGE_FLAG_API_DONE | STORAGE_FLAG_BLE_DONE))) storage_complete(record.seq, STORAGE_FLAG_API_DONE);
  }
}

static int failures = 0;
#define CHECK(cond) do { if (cond) printf("  ok    %s\n", #cond); else { printf("  FAIL  %s   [pending %u (batch %u) history %u screen %d '%s' state %d]\n", #cond, storage_count(), storage_count_batch(), history_count(), (int)shown, shown_text.c_str(), (int)app_state()); failures++; } } while (0)

static void run(int ms) {
  for (int64_t end = now + ms; now < end;) {
    now += 10;
    api_drain();
    app_loop((uint64_t)now);
    if (ignoring && !buttons_any_down()) ignoring = false;
  }
}
static void hold(buttons_t b, int ms) { down[b] = true; run(ms); down[b] = false; run(30); }
static void click(buttons_t b) { hold(b, 90); }
static void dclick(buttons_t b) { click(b); run(100); click(b); }

template <typename F> static bool until(F cond, int max_ms) {
  for (int64_t end = now + max_ms; now < end;) {
    if (cond()) return true;
    run(10);
  }
  return cond();
}

static std::vector<STRING> tags(int first, int count) {
  std::vector<STRING> list;
  char epc[16];
  for (int i = 0; i < count; i++) { snprintf(epc, sizeof(epc), "TP.01.%06d", first + i); list.push_back(epc); }
  return list;
}

static void wipe() {
  const char *files[] = {"collector_queue.bin", "collector_queue.tmp", "collector_queue.head", "collector_seq.bin",
                         "collector_session.bin", "collector_history.bin", "collector_history.tmp"};
  for (const char *f : files) remove(f);
}

static bool done() { return shown == DISPLAY_MESSAGE && shown_text.rfind("sincroniza", 0) == 0; }
static int single_pending() { return storage_count() - storage_count_batch(); }

static void test_app() {
  printf("\n== 1. Segurar A: leitura individual; com celular conectado nada e enviado sozinho\n");
  phone = true; range = tags(200, 1);
  hold(BUTTON_A, 1000);                       // seq 1
  CHECK(shown == DISPLAY_READ_OK && single_pending() == 1);
  run(8000);
  CHECK(storage_count() == 1 && app_state() == APP_READY);

  printf("\n== 2. Um clique curto em A ou em B: nada acontece\n");
  click(BUTTON_A); run(1500);
  CHECK(app_state() == APP_READY && storage_count() == 1);
  click(BUTTON_B); run(1500);
  CHECK(app_state() == APP_READY && storage_count() == 1);

  printf("\n== 3. Clique em A + clique em B, e dois cliques lentos: nao e duplo clique\n");
  click(BUTTON_A); run(100); click(BUTTON_B); run(1500);
  CHECK(app_state() == APP_READY);
  click(BUTTON_A); run(APP_DOUBLE_CLICK_MS + 100); click(BUTTON_A); run(1500);
  CHECK(app_state() == APP_READY);

  printf("\n== 4. Clique curto seguido de aperto longo: e leitura, nunca duplo clique\n");
  range = tags(201, 1);
  click(BUTTON_A); run(100);
  down[BUTTON_A] = true; run(APP_LONG_PRESS_MS - 100);
  CHECK(app_state() == APP_READY);            // ainda nao e pressao longa
  run(200);
  CHECK(app_state() == APP_READING);
  run(800); down[BUTTON_A] = false; run(30);  // seq 2
  CHECK(storage_count() == 2 && !saw_preparing);

  printf("\n== 5. Segurar C: nenhuma acao\n");
  run(4000);
  hold(BUTTON_C, 4000); run(500);             // aperto longo em C nao e clique
  CHECK(app_state() == APP_READY && shown == DISPLAY_READY && storage_count() == 2);

  printf("\n== 6. Dois cliques em B sem leituras de lote: nada a enviar\n");
  dclick(BUTTON_B);
  CHECK(shown_text == "nenhuma leitura\npendente" && storage_count() == 2);

  printf("\n== 7. Dois cliques em A: sincroniza as individuais\n");
  run(4000);
  dclick(BUTTON_A);
  CHECK(app_state() == APP_SYNC && shown_text == "preparando\nenvio...");
  CHECK(until(done, 20000));
  CHECK(saw_sending && storage_count() == 0);

  printf("\n== 8. Segurar B: leitura em lote, fica pendente\n");
  run(4000);
  range = tags(100, 10);
  hold(BUTTON_B, 3000);                       // seq 3..12
  CHECK(shown == DISPLAY_BATCH && shown_count == 10 && storage_count_batch() == 10);
  run(6000);
  CHECK(storage_count() == 10);

  printf("\n== 9. Dois cliques em B: sincroniza o lote; duplo clique durante o envio e ignorado\n");
  dclick(BUTTON_B);
  run(1500);
  dclick(BUTTON_A); dclick(BUTTON_B);
  CHECK(app_state() == APP_SYNC);
  // O celular simulado nao confirma seq 3, 6, 9 e 12 na primeira vez: confirmacao individual.
  CHECK(until([] { return storage_count() == 4; }, 30000));
  printf("  (10 enviadas, 6 confirmadas: ficam 4 pendentes)\n");
  CHECK(until(done, 30000));
  CHECK(storage_count() == 0);

  printf("\n== 10. Tags ja descarregadas podem ser lidas de novo; pendentes continuam bloqueadas\n");
  run(4000);
  hold(BUTTON_B, 3000);                       // as mesmas 10, ja descarregadas
  CHECK(shown == DISPLAY_BATCH && shown_count == 10 && storage_count_batch() == 10);
  run(4000);
  hold(BUTTON_B, 1500);                       // agora estao pendentes
  CHECK(shown == DISPLAY_DUPLICATE && storage_count() == 10);
  CHECK(history_count() == 0);

  printf("\n== 11. Individual: tag ja descarregada e lida de novo\n");
  run(4000);
  range = tags(200, 1);                       // descarregada no teste 7
  hold(BUTTON_A, 1000);                       // troca B -> A: as 10 nao enviadas saem
  CHECK(shown == DISPLAY_READ_OK && storage_count() == 1 && storage_has_epc("TP.01.000200"));
  run(4000);
  dclick(BUTTON_A);
  CHECK(until(done, 20000));
  run(4000);
  range = tags(100, 1);
  hold(BUTTON_A, 1000);
  CHECK(shown == DISPLAY_READ_OK && storage_count() == 1 && storage_has_epc("TP.01.000100"));

  printf("\n== 12. Dois cliques sem celular: nao transmite, leituras continuam guardadas\n");
  run(4000);
  phone = false;
  dclick(BUTTON_A);
  CHECK(shown_text == "celular\ndesconectado" && storage_count() == 1);
  run(4000);
  dclick(BUTTON_B);
  CHECK(shown_text == "celular\ndesconectado" && storage_count() == 1);

  printf("\n== 13. Desconectar durante o envio: ficam so as nao confirmadas\n");
  run(4000);
  range = tags(101, 1); hold(BUTTON_A, 1000); run(4000);   // seq 14
  range = tags(102, 1); hold(BUTTON_A, 1000); run(4000);
  phone = true;
  run(3000);
  CHECK(storage_count() == 3);     // conectar nao transmite
  dclick(BUTTON_A);
  CHECK(until([] { return storage_count() == 1; }, 20000));
  run(100);                                   // a terceira ja foi transmitida, sem confirmacao
  phone = false;
  run(200);
  CHECK(shown_text == "celular\ndesconectado" && storage_count() == 1);

  printf("\n== 14. Grupos separados: duplo clique em B nao envia as individuais\n");
  run(4000);
  range = tags(400, 3);
  hold(BUTTON_B, 2000);                       // troca A -> B: a transmitida sem confirmacao fica
  storage_record_t kept; storage_get(0, kept);     // a individual transmitida, sem confirmacao
  CHECK(storage_count() == 4 && storage_count_batch() == 3);
  phone = true;
  run(4000);
  dclick(BUTTON_B);
  CHECK(until(done, 30000));
  CHECK(storage_count() == 1 && storage_count_batch() == 0 && storage_find(kept.seq) == 0);
  run(4000);
  dclick(BUTTON_A);
  CHECK(until(done, 30000));
  CHECK(storage_count() == 0);

  printf("\n== 15. Troca de modo apaga as nao transmitidas da sessao anterior\n");
  run(4000);
  phone = false;
  range = tags(500, 4);
  hold(BUTTON_B, 2000);
  CHECK(storage_count_batch() == 4);
  run(1000);
  range = tags(600, 1);
  hold(BUTTON_A, 1000);
  CHECK(storage_count() == 1 && storage_has_epc("TP.01.000600") && !storage_has_epc("TP.01.000500"));

  printf("\n== 15b. A + B por 3 s apaga as pendentes; menos que isso nao faz nada\n");
  run(4000);
  down[BUTTON_A] = true; run(100); down[BUTTON_B] = true; run(2500);
  down[BUTTON_A] = false; down[BUTTON_B] = false; run(1000);
  CHECK(storage_count() == 1 && app_state() == APP_READY);      // soltou antes: nada, nem leitura nem envio
  down[BUTTON_B] = true; run(100); down[BUTTON_A] = true; run(3200);
  CHECK(storage_count() == 0 && shown_text == "memória limpa");
  down[BUTTON_A] = false; down[BUTTON_B] = false; run(4000);
  CHECK(app_state() == APP_READY && storage_count() == 0);
  app_init();
  CHECK(storage_count() == 0);                                  // continua apagado depois de reiniciar
  hold(BUTTON_A, 1000);                                         // a mesma tag pode ser lida de novo
  CHECK(shown == DISPLAY_READ_OK && storage_count() == 1 && storage_has_epc("TP.01.000600"));
  run(4000);
  down[BUTTON_A] = true; run(1000); down[BUTTON_B] = true; run(4000);   // B depois de a leitura comecar: ignorado
  CHECK(app_state() == APP_READING && storage_count() == 1);
  down[BUTTON_A] = false; down[BUTTON_B] = false; run(100);

  printf("\n== 16. Reiniciar: fila, historico e numeros sequenciais preservados\n");
  storage_record_t before, after;
  storage_get(0, before);
  app_init();
  storage_get(0, after);
  CHECK(storage_count() == 1 && after.seq == before.seq && after.session == before.session);
  hold(BUTTON_A, 1000);
  CHECK(shown == DISPLAY_DUPLICATE && storage_count() == 1);   // pendente continua bloqueada
  phone = true;
  run(4000);
  dclick(BUTTON_A);
  CHECK(until(done, 30000));
  CHECK(storage_count() == 0);
}

static void test_keyboard() {
  printf("\n== K1. So teclado: nada e digitado sem duplo clique\n");
  phone = true; range = tags(100, 4);
  hold(BUTTON_B, 2000);
  run(8000);
  CHECK(storage_count() == 4 && typed == 0);

  printf("\n== K2. Duplo clique em A sem individuais: nada\n");
  dclick(BUTTON_A);
  CHECK(shown_text == "nenhuma leitura\npendente" && typed == 0);

  printf("\n== K3. Duplo clique em B: lote digitado tag a tag, esvaziando a fila\n");
  run(4000);
  dclick(BUTTON_B);
  CHECK(until([] { return storage_count() == 2; }, 20000));
  CHECK(typed == 2);
  CHECK(until(done, 20000));
  CHECK(typed == 4 && storage_count() == 0);

  printf("\n== K4. Individual: digitada so depois do duplo clique em A\n");
  run(4000);
  range = tags(200, 1);
  hold(BUTTON_A, 1000);
  run(5000);
  CHECK(typed == 4 && storage_count() == 1);
  dclick(BUTTON_A);
  CHECK(until(done, 20000));
  CHECK(typed == 5 && storage_count() == 0);

  printf("\n== K5. Tag ja digitada pode ser lida e digitada de novo\n");
  run(4000);
  hold(BUTTON_A, 1000);
  CHECK(shown == DISPLAY_READ_OK && storage_count() == 1);
  dclick(BUTTON_A);
  CHECK(until(done, 20000));
  CHECK(typed == 6 && storage_count() == 0);
}

static bool msg(const char *prefix) { return shown == DISPLAY_MESSAGE && shown_text.rfind(prefix, 0) == 0; }
static void env(const char *name, const char *value) { char buf[64]; snprintf(buf, sizeof(buf), "%s=%s", name, value); _putenv(buf); }

static void test_api() {
  api_auto = false;
  printf("\n== W1. Ler uma tag com A e enviar pela API com um clique em C\n");
  range = tags(100, 1);
  hold(BUTTON_A, 1000);
  CHECK(storage_count() == 1 && api_pending() == 1 && ble_pending() == 1);
  run(4000);
  click(BUTTON_C);
  CHECK(app_state() == APP_UPLOAD);
  CHECK(until([] { return msg("envio"); }, 10000));
  CHECK(api_pending() == 0 && net_sim_posts() == 1);
  CHECK(storage_count() == 1 && ble_pending() == 1);          // continua disponivel para o Bluetooth
  STRING body = net_sim_last_body();
  printf("  %s\n", body.c_str());
  CHECK(body.find("{\"readerName\":\"ColetorTI\",\"mac\":\"") == 0 && body.find("\"site\":\"RibeiraoPreto\",\"tag_reads\":[{\"epc\":\"TP.01.000100\",") != STRING::npos);
  CHECK(body.find("\"isHeartBeat\":false,\"isInventoryStatus\":false,\"firstSeenTimestamp\":1") != STRING::npos && body.find(",\"antennaPort\":1,\"peakRssi\":-45.5,\"txPower\":30}]}") != STRING::npos);
  { size_t at = body.find("\"firstSeenTimestamp\":") + 21; CHECK(body.find(",", at) - at == 16 && body.compare(at + 10, 6, "000000") == 0); }

  printf("\n== W2. C sem tags pendentes na API\n");
  run(4000);
  click(BUTTON_C); run(100);
  CHECK(msg("sem tags") && net_sim_posts() == 1);

  printf("\n== W3. Enviada pela API continua na fila do Bluetooth; depois dos dois canais sai da memoria\n");
  run(4000);
  hold(BUTTON_A, 1000);
  CHECK(shown == DISPLAY_DUPLICATE);                            // ainda pendente no Bluetooth
  phone = true; run(4000);
  dclick(BUTTON_A);
  CHECK(until(done, 20000));
  CHECK(storage_count() == 0);
  run(4000);
  hold(BUTTON_A, 1000);                                         // saiu da memoria: pode ler de novo
  CHECK(shown == DISPLAY_READ_OK && storage_count() == 1);

  printf("\n== W4. Enviada pelo Bluetooth continua na fila da API\n");
  run(4000);
  dclick(BUTTON_A);
  CHECK(until(done, 20000));
  CHECK(storage_count() == 1 && ble_pending() == 0 && api_pending() == 1);
  run(4000);
  dclick(BUTTON_A); run(200);
  CHECK(msg("nenhuma leitura"));                                // nada mais para o Bluetooth
  run(4000);
  hold(BUTTON_A, 1000);
  CHECK(shown == DISPLAY_DUPLICATE);                            // pendente na API: bloqueada
  run(4000);
  click(BUTTON_C);
  CHECK(until([] { return msg("envio"); }, 10000));
  CHECK(storage_count() == 0 && net_sim_posts() == 2);

  printf("\n== W5. Tags de A e de B juntas pelo botao C\n");
  run(4000);
  phone = false;
  range = tags(200, 2);
  hold(BUTTON_A, 1000); run(4000);
  range = tags(201, 1);
  hold(BUTTON_A, 1000); run(4000);                             // 2 individuais
  range = tags(300, 5);
  hold(BUTTON_B, 2500); run(4000);                             // troca A -> B: cancela so o lado Bluetooth de A
  CHECK(api_pending() == 7 && storage_count_batch() == 5 && ble_pending() == 5);
  click(BUTTON_C);
  run(500);
  click(BUTTON_C); dclick(BUTTON_A); hold(BUTTON_B, 600);       // durante o envio: tudo ignorado
  CHECK(app_state() == APP_UPLOAD);
  CHECK(until([] { return msg("envio"); }, 20000));
  CHECK(api_pending() == 0 && net_sim_posts() == 9 && storage_count() == 5);   // as 5 do lote esperam o Bluetooth

  printf("\n== W6. C sem Wi-Fi: nada e enviado nem apagado\n");
  run(4000);
  range = tags(400, 4);
  hold(BUTTON_B, 2500); run(4000);
  CHECK(api_pending() == 4);
  env("SIM_WIFI", "off");
  click(BUTTON_C); run(100);
  CHECK(msg("conectando") && net_sim_wifi_on());                  // liga o Wi-Fi e espera
  CHECK(until([] { return msg("wifi"); }, 20000));
  CHECK(api_pending() == 4 && net_sim_posts() == 9 && !net_sim_wifi_on());

  printf("\n== W7. Wi-Fi conectado, API fora do ar\n");
  run(4000);
  env("SIM_WIFI", "on"); env("SIM_API", "fail");
  click(BUTTON_C);
  CHECK(until([] { return msg("api com problemas"); }, 10000));
  CHECK(api_pending() == 4 && net_sim_posts() == 9);

  printf("\n== W8. Envio parcial: so as confirmadas saem da fila da API\n");
  run(4000);
  env("SIM_API", "ok"); env("SIM_API_FAIL_AFTER", "11");
  click(BUTTON_C);
  CHECK(until([] { return msg("api com problemas"); }, 20000));
  CHECK(api_pending() == 2 && net_sim_posts() == 11);

  printf("\n== W9. Reiniciar no meio: as pendentes continuam\n");
  app_init();
  CHECK(api_pending() == 2 && ble_pending() == 9 && storage_count() == 9);

  printf("\n== W10. Wi-Fi cai durante o envio\n");
  env("SIM_API_FAIL_AFTER", "999");
  click(BUTTON_C);
  CHECK(until([] { return net_sim_posts() == 12; }, 10000));
  env("SIM_WIFI", "off");
  CHECK(until([] { return msg("wifi"); }, 10000));
  CHECK(api_pending() >= 1 && api_pending() <= 2);
  env("SIM_WIFI", "on");
  run(4000);
  click(BUTTON_C);
  CHECK(until([] { return msg("envio"); }, 10000));
  CHECK(api_pending() == 0 && storage_count() == 9);

  printf("\n== W11. Leitura RFID continua funcionando; Bluetooth nao foi afetado\n");
  run(4000);
  range = tags(500, 1);
  hold(BUTTON_B, 1500);
  CHECK(shown == DISPLAY_BATCH && storage_count() == 10);
  phone = true; run(4000);
  dclick(BUTTON_B);
  CHECK(until(done, 60000));
  CHECK(ble_pending() == 0 && storage_count() == 1 && api_pending() == 1);

  printf("\n== W12. A + B por 3 s apaga as duas filas\n");
  run(4000);
  down[BUTTON_A] = true; down[BUTTON_B] = true; run(3200); down[BUTTON_A] = false; down[BUTTON_B] = false; run(100);
  CHECK(storage_count() == 0 && api_pending() == 0);

  printf("\n== W13. Sem horario da internet: nao envia\n");
  run(4000);
  hold(BUTTON_A, 1000); run(4000);
  env("SIM_TIME", "off");
  int before = net_sim_posts();
  click(BUTTON_C);
  CHECK(until([] { return msg("aguardando"); }, 20000));
  CHECK(api_pending() == 1 && net_sim_posts() == before && !net_sim_wifi_on());
  env("SIM_TIME", "on");
  printf("\n== W14. Wi-Fi so fica ligado durante o envio\n");
  run(4000);
  CHECK(!net_sim_wifi_on());
  click(BUTTON_C); run(50);
  CHECK(net_sim_wifi_on() && ble_suspended() && !ble_phone_connected());   // Bluetooth desligado no envio
  CHECK(until([] { return msg("envio"); }, 10000));
  CHECK(!net_sim_wifi_on() && api_pending() == 0);
  run(100);
  CHECK(!ble_suspended());                                        // Bluetooth religado no fim
  env("SIM_TIME", "on");
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  wipe();
  app_init();
  if (argc > 1 && strcmp(argv[1], "api") == 0) test_api();
  else if (argc > 1 && strcmp(argv[1], "keyboard") == 0) test_keyboard();
  else test_app();
  wipe();
  printf("\n%s\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED");
  return failures;
}
