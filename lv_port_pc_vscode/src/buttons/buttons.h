#ifndef BUTTONS_H
#define BUTTONS_H

#include "../../main.h"

// A: leitura individual (uma tag por aperto). B: leitura em lote (todas as tags
// ineditas enquanto estiver pressionado). Nos dois, segurar le e dois cliques
// rapidos sincronizam as pendentes daquele modo (ver src/state/app.cpp).
#define BUTTON_SINGLE BUTTON_A
#define BUTTON_BATCH BUTTON_B

// C (o antigo "Ok", laranja, GPIO 33): um clique envia as leituras pendentes
// para a API da Ativa pelo Wi-Fi. Nao le tags. Continua acordando o aparelho
// do deep sleep.
#define BUTTON_API BUTTON_C

#define BUTTONS_COUNT 3
#define BUTTONS_DEBOUNCE_MS 30

void buttons_init();

// Le os tres botoes e atualiza os eventos. Chamar uma vez por volta do loop.
void buttons_update(uint64_t now_ms);

// Estado estavel (ja com debounce).
bool button_is_down(buttons_t button);

// Eventos validos somente ate a proxima chamada de buttons_update().
bool button_was_pressed(buttons_t button);
bool button_was_released(buttons_t button);

// Estado fisico: nao e afetado por buttons_ignore_until_release().
bool buttons_any_down();

// Descarta o aperto atual: ate todos os botoes serem soltos, nenhum evento e
// gerado e button_is_down() devolve false. Usado quando o aperto so acende a tela.
void buttons_ignore_until_release();

#endif // BUTTONS_H
