#ifndef APP_H
#define APP_H

#include "../../main.h"

// Tempo que a tela de resultado fica visivel antes de voltar para "Pronto".
#define APP_RESULT_MS 3000

typedef enum {
  APP_READY,     // aguardando o botao de leitura
  APP_READING,   // botao pressionado, inventario em andamento
  APP_READ_OK,   // tag lida, mostrando o resultado
  APP_NO_TAG     // botao solto sem nenhuma tag
} app_state_t;

void app_init();

// Maquina de estados central. Chamar depois de buttons_update().
void app_loop(uint64_t now_ms);

app_state_t app_state();

#endif // APP_H
