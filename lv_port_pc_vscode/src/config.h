#ifndef CONFIG_H
#define CONFIG_H

// Escolha do leitor RFID, feita na compilacao:
//   0 = leitor real (modulo UHF na Serial2) - padrao na placa
//   1 = leitor simulado (tags ficticias, sem o modulo conectado)
// Para gravar a placa com o leitor simulado sem editar este arquivo, acrescente
// ao comando de compilacao:
//   --build-property "compiler.cpp.extra_flags=-DRFID_SIMULATED=1"
#ifndef RFID_SIMULATED
  #define RFID_SIMULATED 0
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

// Tela: a luz de fundo apaga depois deste tempo sem apertar botao.
#ifndef SCREEN_OFF_MS
  #define SCREEN_OFF_MS 300000UL  // 5 minutos
#endif

// GPIO da luz de fundo da tela (TFT_BL da placa TTGO T-Display).
#define DISPLAY_BACKLIGHT_PIN 4

#endif // CONFIG_H
