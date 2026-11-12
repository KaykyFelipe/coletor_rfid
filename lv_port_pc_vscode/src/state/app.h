#ifndef APP_H
#define APP_H

#include "../../main.h"

// Tempo que a tela de resultado fica visivel antes de voltar para "Pronto".
#define APP_RESULT_MS 3000

// Tempo de espera pela confirmacao do celular antes de mostrar o final da tag.
#define APP_ACK_TIMEOUT_MS 2000

// Intervalo de atualizacao da caracteristica de status (bateria e pendentes).
#define APP_STATUS_MS 5000

typedef enum {
  APP_READY,     // aguardando o botao de leitura
  APP_READING,   // botao pressionado, inventario em andamento
  APP_SENDING,   // tag enviada, aguardando a confirmacao do celular
  APP_READ_OK,   // tag lida, mostrando o resultado
  APP_NO_TAG,    // botao solto sem nenhuma tag
  APP_MESSAGE    // mostrando um texto enviado pelo celular
} app_state_t;

void app_init();

// Maquina de estados central. Chamar depois de buttons_update().
void app_loop(uint64_t now_ms);

app_state_t app_state();

#endif // APP_H
