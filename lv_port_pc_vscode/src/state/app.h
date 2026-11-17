#ifndef APP_H
#define APP_H

#include "../../main.h"

// Tempo que a tela de resultado fica visivel antes de voltar para "Pronto".
#define APP_RESULT_MS 3000

// Tempo de espera pela confirmacao do celular para cada leitura transmitida.
#define APP_ACK_TIMEOUT_MS 2000

// Intervalo de atualizacao da caracteristica de status (bateria e pendentes).
#define APP_STATUS_MS 5000

// Botoes A e B: segurar por pelo menos este tempo e uma pressao longa (leitura).
// Soltar antes disso e um clique curto.
#ifndef APP_LONG_PRESS_MS
  #define APP_LONG_PRESS_MS 400
#endif

// Tempo maximo entre soltar o primeiro clique e apertar o segundo para valer
// como duplo clique (sincronizacao).
#ifndef APP_DOUBLE_CLICK_MS
  #define APP_DOUBLE_CLICK_MS 350
#endif

// Segurar A e B juntos por este tempo apaga todas as leituras pendentes.
#ifndef APP_CLEAR_HOLD_MS
  #define APP_CLEAR_HOLD_MS 3000
#endif

// Botao C: um aperto de ate este tempo e um clique (envio para a API).
#ifndef APP_API_CLICK_MAX_MS
  #define APP_API_CLICK_MAX_MS 1000
#endif

// Tempo da tela "Preparando envio..." antes de comecar a transmitir.
#define APP_SYNC_PREPARE_MS 800

// Modos de leitura. Trocar de um para o outro abre uma sessao nova.
#define APP_MODE_NONE   0
#define APP_MODE_SINGLE 1  // botao A: uma tag por aperto
#define APP_MODE_BATCH  2  // botao B: todas as tags ineditas enquanto pressionado

typedef enum {
  APP_READY,     // aguardando o botao A ou B
  APP_READING,   // botao pressionado, inventario em andamento
  APP_SYNC,      // duplo clique: transmitindo as leituras pendentes de um modo
  APP_UPLOAD,    // botao C: enviando as leituras pendentes para a API pelo Wi-Fi
  APP_READ_OK,   // leitura feita, mostrando o resultado
  APP_NO_TAG,    // botao solto sem nenhuma tag inedita
  APP_MESSAGE    // mostrando um aviso ou um texto enviado pelo celular
} app_state_t;

void app_init();

// Maquina de estados central. Chamar depois de buttons_update().
void app_loop(uint64_t now_ms);

app_state_t app_state();

#endif // APP_H
