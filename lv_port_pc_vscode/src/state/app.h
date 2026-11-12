#ifndef APP_H
#define APP_H

#include "../../main.h"

// Tempo que a tela de resultado fica visivel antes de voltar para "Pronto".
#define APP_RESULT_MS 3000

void app_init();

// Reage aos botoes e troca as telas. Chamar depois de buttons_update().
void app_loop(uint64_t now_ms);

#endif // APP_H
