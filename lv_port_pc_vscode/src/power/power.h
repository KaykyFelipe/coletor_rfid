#ifndef POWER_H
#define POWER_H

#include "../../main.h"
#include "../config.h"

// Economia de energia da tela: apaga a luz de fundo depois de SCREEN_OFF_MS sem
// uso e acende de novo em qualquer botao. O aperto que acende a tela nao faz
// mais nada (nao comeca uma leitura).
//
// O desligamento do aparelho (deep sleep apos 10 minutos) continua em main.cpp.

// Chamar a cada volta do loop, depois de buttons_update() e antes de app_loop().
void power_loop(uint64_t now_ms);

// Registra atividade que nao veio de botao (ex.: texto enviado pelo celular) e acende a tela.
void power_activity(uint64_t now_ms);

bool power_screen_on();

#endif // POWER_H
