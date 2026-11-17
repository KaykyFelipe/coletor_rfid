#ifndef NET_H
#define NET_H

#include "../../main.h"
#include "../config.h"

// Wi-Fi e envio de leituras para a API da Ativa (botao C).
//
// - O Wi-Fi fica desligado. Ele so liga durante um envio (net_on) e desliga no
//   fim (net_off): a bateria e medida pelo ADC2, que o ESP32 nao deixa ler com
//   o Wi-Fi ligado, e o radio desligado tambem gasta menos.
// - O horario vem da internet (NTP) logo depois de conectar. Sem horario
//   valido nada e enviado, para nao gravar datas erradas na API.
// - Cada leitura e um POST HTTPS, feito em uma tarefa separada: o loop
//   principal so pede o envio e consulta o resultado.
//
// No simulador de PC nao ha rede: o resultado e controlado por variaveis de
// ambiente (SIM_WIFI=off, SIM_API=fail, SIM_API_FAIL_AFTER=n).

typedef enum {
  NET_POST_IDLE,  // nenhum envio em andamento
  NET_POST_BUSY,  // envio em andamento
  NET_POST_OK,    // a API respondeu 2xx
  NET_POST_FAIL   // sem resposta, erro de conexao ou resposta diferente de 2xx
} net_post_t;

void net_init();

// Liga o Wi-Fi e comeca a conectar na rede de config.h, sem esperar.
void net_on();

// Desliga o Wi-Fi.
void net_off();

// O radio Wi-Fi ja esta desligado de fato (o desligamento leva um instante).
bool net_radio_off();

// Acompanha a conexao Wi-Fi. Chamar a cada volta do loop.
void net_loop(uint64_t now_ms);

bool net_wifi_connected();

// O relogio ja foi acertado pela internet.
bool net_time_valid();

// Comeca o envio de uma tag para a API. Devolve false se ja ha um envio em andamento.
bool net_post_start(const char *epc);

// Resultado do envio. NET_POST_OK e NET_POST_FAIL sao devolvidos uma unica vez.
net_post_t net_post_result();

// Monta o corpo JSON no formato exigido pela API. `timestamp_us` em
// microssegundos desde 1970 (UTC). Devolve o tamanho do texto.
size_t net_build_body(char *out, size_t size, const char *epc, uint64_t timestamp_us, const char *mac);

#ifndef ESP32
  // Simulador: ultimo corpo "enviado" e quantos envios foram feitos.
  const char *net_sim_last_body();
  int net_sim_posts();
  bool net_sim_wifi_on();
#endif

#endif // NET_H
