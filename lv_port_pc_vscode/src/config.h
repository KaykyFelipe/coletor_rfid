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

/**********************
 *  Wi-Fi e API da Ativa (botao C)
 **********************/

// Rede usada so para falar com a API. Fixa no firmware. O Wi-Fi so liga durante o envio (botao C).
#define WIFI_SSID "Ativa"
#define WIFI_PASSWORD "Ativa1234"

// Tempo maximo para conectar na rede e receber o horario, a cada envio.
#define WIFI_CONNECT_TIMEOUT_MS 15000UL

// Cada leitura e enviada em um POST para https://API_HOST/API_PATH (sem autenticacao).
#define API_HOST "api-app.ativalocacao.com.br"
#define API_PORT 443
#define API_PATH "/rfid"

// Tempo maximo para conectar e para a API responder, por leitura.
#define API_TIMEOUT_MS 10000

// Valores fixos enviados em cada leitura.
#define API_READER_NAME "ColetorTI"
#define API_SITE "RibeiraoPreto"
#define API_ANTENNA_PORT 1

#endif // CONFIG_H
