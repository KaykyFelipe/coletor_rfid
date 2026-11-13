#ifndef DISPLAY_H
#define DISPLAY_H

#include "../../main.h"

typedef enum {
  DISPLAY_READY,     // "A: uma tag / B: lote"
  DISPLAY_READING,   // "Lendo..."
  DISPLAY_READ_OK,   // nome do equipamento + "Leitura feita"
  DISPLAY_NO_TAG,    // "Nenhuma tag encontrada"
  DISPLAY_OFFLINE,   // "N guardadas" (sem uso desde a sincronizacao por duplo clique)
  DISPLAY_SENDING,   // "Enviando leituras..." + `count` pendentes
  DISPLAY_MESSAGE,   // texto livre enviado pelo celular
  DISPLAY_DUPLICATE, // tag ja registrada (so repetidas ao alcance); `text` = final da tag
  DISPLAY_BATCH,     // modo em lote: `count` tags lidas neste aperto; `text` = aviso abaixo
  DISPLAY_CONFIRM    // pergunta em `text` (sem uso: o botao C nao tem mais funcao)
} display_screen_t;

// Monta a barra superior fixa e a area central sobre o objeto `screen`.
void display_init();

// Barra superior, lado esquerdo: "Celular ligado" (verde) ou "Sem celular" (amarelo).
void display_set_connected(bool connected);

// Barra superior, lado direito: percentual + icone de bateria (simbolo ja escolhido em main.cpp).
void display_set_battery(float percent, const char *symbol);

// Troca a tela central. `text` e usado em DISPLAY_READ_OK (nome do equipamento),
// DISPLAY_MESSAGE e DISPLAY_READY (aviso opcional abaixo do titulo); `count` em
// DISPLAY_OFFLINE (leituras guardadas), DISPLAY_READ_OK (numero sequencial da
// leitura, 0 = nao mostrar) e DISPLAY_SENDING (guardadas que faltam, 0 = nao mostrar).
void display_show(display_screen_t which, const char *text = NULL, int count = 0);

display_screen_t display_current();

// Liga ou desliga a luz de fundo. A tela continua sendo desenhada enquanto apagada.
void display_set_backlight(bool on);

#endif // DISPLAY_H
