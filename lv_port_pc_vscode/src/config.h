#ifndef CONFIG_H
#define CONFIG_H

// Escolha do leitor RFID, feita na compilacao:
//   1 = leitor simulado (tags ficticias, sem o modulo conectado)
//   0 = leitor real (modulo UHF na Serial2) - entra na ETAPA 5
// Para trocar sem editar este arquivo, passe -DRFID_SIMULATED=0 ao compilador.
#ifndef RFID_SIMULATED
  #define RFID_SIMULATED 1
#endif

// O simulador de PC nao tem o modulo: sempre usa o leitor simulado.
#ifndef ESP32
  #undef RFID_SIMULATED
  #define RFID_SIMULATED 1
#endif

// Somente tags que casam com este filtro sao aceitas (o mesmo do firmware anterior).
#define RFID_TAG_FILTER "^([A-Z]{2}\\.\\d{2}\\.\\d{6}|AtivaLocacao|0123456789AB)$"

// Retorno sonoro e vibracao. O aparelho atual nao tem esses componentes ligados:
// -1 deixa a funcao inativa (so registra no log). Informe o GPIO para ativar.
#define BUZZER_PIN -1
#define VIBRATION_PIN -1

#endif // CONFIG_H
