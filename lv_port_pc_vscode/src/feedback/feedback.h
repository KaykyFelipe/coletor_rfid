#ifndef FEEDBACK_H
#define FEEDBACK_H

#include "../../main.h"
#include "../config.h"

void feedback_init();

// Bipe curto + vibracao: tag lida.
void feedback_read_ok();

// Bipe diferente (grave e longo): nenhuma tag encontrada.
void feedback_error();

#endif // FEEDBACK_H
