#ifndef DISPLAY_H
#define DISPLAY_H

#include "../../main.h"

typedef enum {
  DISPLAY_READY,     // "Segure o botao para ler"
  DISPLAY_READING,   // "Lendo..."
  DISPLAY_READ_OK,   // nome do equipamento + "Leitura feita"
  DISPLAY_NO_TAG,    // "Nenhuma tag encontrada"
  DISPLAY_OFFLINE,   // "N guardadas" + aviso de envio automatico
  DISPLAY_SENDING,   // "Enviando..." (aguardando a confirmacao do celular)
  DISPLAY_MESSAGE    // texto livre enviado pelo celular
} display_screen_t;

// Monta a barra superior fixa e a area central sobre o objeto `screen`.
void display_init();

// Barra superior, lado esquerdo: "Celular ligado" (verde) ou "Sem celular" (amarelo).
void display_set_connected(bool connected);

// Barra superior, lado direito: percentual + icone de bateria (simbolo ja escolhido em main.cpp).
void display_set_battery(float percent, const char *symbol);

// Troca a tela central. `text` e usado em DISPLAY_READ_OK (nome do equipamento)
// e DISPLAY_MESSAGE; `count` em DISPLAY_OFFLINE (leituras guardadas) e em
// DISPLAY_READ_OK (numero sequencial da leitura, 0 = nao mostrar).
void display_show(display_screen_t which, const char *text = NULL, int count = 0);

display_screen_t display_current();

#endif // DISPLAY_H
