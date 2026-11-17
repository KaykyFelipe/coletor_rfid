# Guia de manutenção do firmware do Coletor RFID

Para quem for mexer no firmware daqui a alguns meses: onde fica cada coisa, como compilar e testar, por que algumas decisões foram tomadas e o que fazer quando algo quebrar.

Leitura complementar:

- [README](../README.md): uso do aparelho, hardware e preparação do ambiente.
- [Protocolo Bluetooth](PROTOCOLO_BLE.md): o formato dos dados trocados com o aplicativo do celular.
- [Melhorias](MELHORIAS.md): o que mudou em relação ao firmware original e o que já foi validado na placa.

## Antes de mudar qualquer coisa

1. **Faça backup da flash da placa.** As leituras guardadas ficam nela.
   ```powershell
   $esptool = "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe"
   & $esptool --port COM10 --baud 921600 read-flash 0 0x1000000 'c:\GitHub\coletor_rfid\backup_placa_AAAAMMDD.bin'
   ```
2. **Grave sempre com `EraseFlash=none`.** Com `EraseFlash=all`, as leituras guardadas e o número sequencial são apagados.
3. **Desconecte o fio preto da antena (GPIO 2) para gravar.** Ligado, ele causa o erro `Wrong boot mode detected (0xb)`.
4. **Teste no simulador e na placa.** O simulador não tem Wi-Fi, Bluetooth, bateria nem memória limitada, que é onde ficaram os problemas mais difíceis.

## Ambiente e versões

Tudo foi testado com estas versões. Atualizar qualquer uma pode quebrar o build ou estourar a memória (ver "Decisões que não devem ser desfeitas").

| Item | Versão |
| --- | --- |
| Placa | LilyGO TTGO T-Display (ESP32, 16 MB de flash, sem PSRAM) |
| arduino-cli | 1.6.0 |
| Core esp32 | 3.3.2 (ESP-IDF 5.5) |
| LVGL | 9.4.0 |
| NimBLE-Arduino | 2.3.6 |
| ESP32-BLE-Keyboard | cópia modificada, na pasta de bibliotecas do Arduino |
| Simulador | MSYS2 UCRT64, CMake, Ninja e SDL2 |

As bibliotecas do Arduino ficam em `Documentos\Arduino\libraries` (no OneDrive desta máquina). A biblioteca do teclado foi alterada à mão para funcionar com o NimBLE; o comentário no topo de [main.cpp](../lv_port_pc_vscode/main.cpp) descreve as alterações.

## Compilar, gravar e simular

No PowerShell:

```powershell
$cli  = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
$fqbn = 'esp32:esp32:lilygo_t_display:PSRAM=disabled,LoopCore=1,EventsCore=1,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=16M,UploadSpeed=921600,DebugLevel=none,EraseFlash=none'
$src  = 'c:\GitHub\coletor_rfid\lv_port_pc_vscode'
$out  = 'c:\GitHub\coletor_rfid\build\esp32_cli'

# Compilar
& $cli compile --fqbn $fqbn --build-property "build.partitions=default_16MB" --build-property "upload.maximum_size=6553600" --build-path $out --jobs 4 $src

# Gravar (fio preto da antena desconectado)
& $cli upload -p COM10 --fqbn $fqbn --input-dir $out

# Ver as mensagens da placa (feche antes de gravar de novo)
& $cli monitor -p COM10 --config baudrate=115200
```

- Se o upload reclamar de `partitions.bin` ou de "valid build artifact", a compilação falhou. Leia as linhas `error:` da compilação.
- Se a compilação falhar de forma estranha depois de trocar de versão, apague a pasta `build\esp32_cli` e compile de novo.

Simulador, no terminal MSYS2 UCRT64:

```bash
cd /c/GitHub/coletor_rfid/lv_port_pc_vscode
cmake -S . -B build_sim -G Ninja -DCMAKE_BUILD_TYPE=Debug   # só na primeira vez
cmake --build build_sim --parallel 4
./bin/main.exe
```

No simulador, o Wi-Fi e a API são controlados por variáveis de ambiente: `SIM_WIFI=off`, `SIM_API=fail`, `SIM_API_FAIL_AFTER=n` e `SIM_TIME=off`. `SIM_KEYBOARD_ONLY=1` simula um celular que só usa o teclado.

## Onde fica cada coisa

Tudo em `lv_port_pc_vscode/`. O mesmo código compila para a placa e para o PC; o que é só da placa fica dentro de `#ifdef ESP32`, e o resto é a versão simulada.

| Arquivo | Responsabilidade |
| --- | --- |
| [main.cpp](../lv_port_pc_vscode/main.cpp) | Código original: inicialização da placa, tela (LVGL), bateria, deep sleep e digitação do teclado. Chama `app_init()` e `app_loop()`. |
| [src/config.h](../lv_port_pc_vscode/src/config.h) | Configurações: rede Wi-Fi, endereço e campos da API, filtro de tags, pinos de buzzer e vibração. |
| [src/state/app.cpp](../lv_port_pc_vscode/src/state/app.cpp) | Máquina de estados: botões, leitura, envio pelo Bluetooth, envio pela API e limpeza com A + B. É o arquivo que mais muda. |
| [src/state/app.h](../lv_port_pc_vscode/src/state/app.h) | Tempos dos botões e das telas. |
| [src/storage/](../lv_port_pc_vscode/src/storage/) | Leituras guardadas na flash (LittleFS), com uma fila para o Bluetooth e outra para a API. |
| [src/net/](../lv_port_pc_vscode/src/net/) | Wi-Fi, horário pela internet (NTP), JSON e POST HTTPS para a API. |
| [src/ble/](../lv_port_pc_vscode/src/ble/) | Teclado Bluetooth, serviço próprio para o aplicativo e desligar/religar o Bluetooth durante o envio pela API. |
| [src/rfid/](../lv_port_pc_vscode/src/rfid/) | Leitor UHF na Serial2 e leitor simulado. |
| [src/display/](../lv_port_pc_vscode/src/display/) | Textos e cores de cada tela. |
| [src/buttons/](../lv_port_pc_vscode/src/buttons/), [src/power/](../lv_port_pc_vscode/src/power/), [src/feedback/](../lv_port_pc_vscode/src/feedback/) | Botões com debounce, tela apagada e deep sleep, buzzer e vibração (ainda sem pinos). |
| `legacy/` | Código antigo do menu, guardado só para consulta. |

## Mudanças comuns

| Quero mudar | Onde |
| --- | --- |
| Rede Wi-Fi ou senha | `WIFI_SSID` e `WIFI_PASSWORD` em [config.h](../lv_port_pc_vscode/src/config.h) |
| Endereço da API | `API_HOST`, `API_PORT` e `API_PATH` em [config.h](../lv_port_pc_vscode/src/config.h) |
| `readerName`, `site` ou `antennaPort` | `API_READER_NAME`, `API_SITE` e `API_ANTENNA_PORT` em [config.h](../lv_port_pc_vscode/src/config.h) |
| Outros campos do JSON | `net_build_body()` no começo de [net.cpp](../lv_port_pc_vscode/src/net/net.cpp) |
| Formato aceito das tags | `RFID_TAG_FILTER` em [config.h](../lv_port_pc_vscode/src/config.h) |
| Tempo de pressão longa e de duplo clique | `APP_LONG_PRESS_MS` e `APP_DOUBLE_CLICK_MS` em [app.h](../lv_port_pc_vscode/src/state/app.h) |
| Limite de leituras guardadas | `STORAGE_CAPACITY` (250) em [storage.h](../lv_port_pc_vscode/src/storage/storage.h) |
| Textos das telas | [display.cpp](../lv_port_pc_vscode/src/display/display.cpp) e as chamadas a `show_message()` em [app.cpp](../lv_port_pc_vscode/src/state/app.cpp) |
| Ligar buzzer ou vibração | `BUZZER_PIN` e `VIBRATION_PIN` em [config.h](../lv_port_pc_vscode/src/config.h) (hoje `-1`, desligados) |
| Certificado da API | `API_ROOT_CA` em [net.cpp](../lv_port_pc_vscode/src/net/net.cpp) (ver abaixo) |

**Ao mudar o JSON**, confira o corpo real na serial do simulador (linha `API SIM: {...}`) antes de gravar na placa.

**Se a API trocar de certificado.** Hoje o firmware confia só na raiz ISRG Root X2, da Let's Encrypt, válida até 2040. Se a API passar a usar outra autoridade certificadora, todo envio dá "API com Problemas" e a serial mostra `API: connect failed | X509 ...`. Para descobrir a raiz nova:

```bash
openssl s_client -connect api-app.ativalocacao.com.br:443 -servername api-app.ativalocacao.com.br -showcerts
```

A última linha `i:` da cadeia é a raiz. Baixe o PEM dela no site da autoridade e troque o conteúdo de `API_ROOT_CA`. Use só uma raiz, de preferência ECDSA, que é menor (ver o motivo na seção seguinte).

## Decisões que não devem ser desfeitas

Cada item abaixo corrigiu um defeito real na placa. Desfazer qualquer um traz o defeito de volta.

**1. O Wi-Fi só liga durante o envio (botão C).** A bateria é medida no GPIO 25, que pertence ao ADC2. O ESP32 não deixa ler o ADC2 com o Wi-Fi ligado. Com o Wi-Fi sempre ligado, o aparelho reiniciava em laço (`ESP_ERR_TIMEOUT` em `adc_oneshot_read`) e o percentual da bateria parava. A leitura da bateria em `get_battery_voltage()` também não aborta mais em caso de erro: mantém o último valor. Durante o envio a serial mostra `adc oneshot read fail`, o que é esperado. A solução definitiva seria levar o divisor da bateria para um pino do ADC1 (GPIO 32 a 39), mas o 32 e o 33 já são os botões B e C.

**2. O Bluetooth desliga durante o envio para a API.** Com Bluetooth e Wi-Fi ligados juntos, não sobra memória para abrir o HTTPS: a serial mostrava `SSL - Memory allocation failed` e depois `X509 - Allocation of memory failed`. A placa não tem PSRAM. No clique em C, `ble_suspend()` desconecta o celular e desliga o NimBLE; no fim, `ble_resume()` monta o teclado de novo e volta a anunciar, e o celular pareado reconecta sozinho. Detalhes que já causaram falhas:
- antes de `NimBLEDevice::deinit(true)`, é obrigatório chamar `server->setCallbacks(nullptr)`. Sem isso, a biblioteca tenta apagar o objeto do teclado, que é estático, e o ESP32 aborta (`free() target pointer is outside heap areas`), com a tela piscando;
- se o celular não desconectar em 2 s, o Bluetooth fica ligado, para o teclado não tentar digitar em objetos já apagados;
- a biblioteca do teclado não apaga o objeto HID antigo. Cada envio perde uns 200 bytes, que voltam quando o aparelho reinicia ou entra em deep sleep.

**3. O POST HTTPS é escrito à mão, sem a biblioteca HTTPClient.** Com Bluetooth e Wi-Fi no mesmo firmware, a região IRAM do ESP32 fica no limite (sobram cerca de 3 KB). A HTTPClient puxa funções de data que ocupam IRAM, e o link falhava com `iram0_0_seg overflowed`. Pelo mesmo motivo, o horário é pedido direto ao SNTP (`esp_sntp_*`) em vez de `configTime()`, e o `timestamp` é montado com `snprintf`, sem `strftime`. Antes de acrescentar uma biblioteca, compile e veja se o link passa.

**4. Uma só raiz de certificado, e ECDSA.** Cada raiz ocupa memória durante a conexão. Com duas raízes (uma delas RSA 4096), o HTTPS ficava sem memória.

**5. O buffer de desenho da tela é `uint8_t`.** No original era `uint32_t`, 4 vezes maior que o necessário (26 KB em vez de 6,5 KB). A memória recuperada fez falta para o HTTPS.

**6. O Wi-Fi só liga na primeira volta do loop, nunca no `setup()`.** A calibração da bateria (`cal_vref()`) usa o ADC2 e roda depois de `app_init()`.

## Regras de funcionamento

Estas regras foram pedidas pela operação. Mudar qualquer uma é decisão de negócio, não ajuste técnico.

- Segurar A lê uma tag; segurar B lê em lote; dois cliques no mesmo botão enviam aquele grupo ao celular; um clique em C envia tudo para a API; A + B por 3 s apaga tudo, sem pergunta.
- Nada é enviado sozinho.
- Trocar de A para B (ou o contrário) abre uma sessão nova e cancela, no Bluetooth, o que a sessão anterior não transmitiu. Na fila da API, essas leituras continuam pendentes.
- Cada leitura fica em duas filas independentes, a do Bluetooth e a da API. Ela só sai da memória quando os dois canais confirmaram, ou com A + B.
- Uma tag que ainda está na memória é recusada como "Já registrada".
- Na API vai uma leitura por POST. Ela só sai da fila quando a API responde 2xx, e o envio para no primeiro erro.

## Diagnóstico pela serial

O aparelho não guarda log. As mensagens só existem na serial (115200), no momento em que acontecem. Ligue o monitor **antes** de reproduzir o problema.

| Mensagem | Significado |
| --- | --- |
| `API: upload requested \| pending: N` | Clique em C. |
| `BLE: off for API upload \| heap before N` e `BLE: off \| heap after N` | Bluetooth desligado; memória livre antes e depois. |
| `BLE: phone did not disconnect, Bluetooth stays on` | O celular não soltou a conexão; o envio deve falhar por memória. |
| `WIFI: connecting to Ativa` e `WIFI: connected, ip ...` | Conexão Wi-Fi. |
| `API: connect failed \| <motivo> \| heap N \| largest block N` | O HTTPS não abriu. `Memory allocation` indica falta de memória; `X509` indica certificado ou memória. |
| `API: bad response \| ...` | A API não respondeu em HTTP. |
| `API: response HTTP/1.1 200 ...` e `API: <tag> -> <código>` | Resposta da API. `-1` significa que não houve resposta. |
| `API: upload stopped \| still pending: N` e `API: upload done` | Fim do envio, com ou sem erro. |
| `WIFI: off` e `BLE: on` | Wi-Fi desligado e Bluetooth religado. |
| `assert failed`, `Backtrace:`, `Rebooting...` | O firmware abortou. |

Para traduzir um `Backtrace:` em arquivo e linha, use o `.elf` da mesma compilação:

```bash
"$LOCALAPPDATA"/Arduino15/packages/esp32/tools/esp-x32/*/bin/xtensa-esp32-elf-addr2line.exe -pfiaC -e /c/GitHub/coletor_rfid/build/esp32_cli/lv_port_pc_vscode.ino.elf <endereços do Backtrace>
```

Sintomas já vistos:

| Sintoma | Causa provável |
| --- | --- |
| Tela piscando e botões sem resposta | O firmware reinicia em laço. Veja o `Backtrace` na serial. |
| "API com Problemas" | Leia a linha `API: connect failed` ou o código depois de `->`. |
| "Aguardando Horário" | O Wi-Fi conectou, mas o NTP não respondeu em 15 s. A rede pode estar bloqueando NTP (porta UDP 123). |
| "WiFi Desconectado" | Rede fora de alcance, nome ou senha errados, ou mais de 15 s para conectar. |
| "Memória Cheia" | 250 leituras esperando um dos canais. Envie pelos dois canais ou limpe com A + B. |

## Testes automáticos

[tests/app_test.cpp](../lv_port_pc_vscode/tests/app_test.cpp) confere a lógica no PC, sem tela e sem placa: 108 verificações de leitura, envio, confirmação parcial, queda no meio do envio, reinício, duplo clique, troca de modo, filas do Bluetooth e da API, Wi-Fi e Bluetooth ligando e desligando. Rode depois de qualquer mudança em [app.cpp](../lv_port_pc_vscode/src/state/app.cpp), [storage.cpp](../lv_port_pc_vscode/src/storage/storage.cpp), [net.cpp](../lv_port_pc_vscode/src/net/net.cpp) ou [ble.cpp](../lv_port_pc_vscode/src/ble/ble.cpp). No terminal MSYS2 UCRT64:

```bash
cd /c/GitHub/coletor_rfid/lv_port_pc_vscode
g++ -std=gnu++17 -I. -o tests/app_test.exe tests/app_test.cpp src/state/app.cpp src/storage/storage.cpp src/rfid/rfid.cpp src/rfid/rfid_sim.cpp src/ble/ble.cpp src/net/net.cpp
./tests/app_test.exe
SIM_KEYBOARD_ONLY=1 ./tests/app_test.exe keyboard
./tests/app_test.exe api
```

Cada modo termina com `ALL TESTS PASSED`. O teste usa os mesmos arquivos de fila do simulador (`collector_*`) e os apaga.

O teste só cobre a lógica. Memória, rádios, bateria e o comportamento real da biblioteca NimBLE só aparecem na placa.

## Conferir na placa depois de qualquer mudança

- [ ] Liga sem reiniciar e o percentual da bateria aparece.
- [ ] Segurar A lê uma tag; segurar B lê em lote.
- [ ] Dois cliques em A e em B enviam ao celular (aplicativo e modo teclado).
- [ ] Clique em C: "Conectando WiFi...", "Enviando Leituras..." e "Envio Concluído!"; a API recebe.
- [ ] Depois do C, o celular reconecta sozinho e o teclado volta a digitar.
- [ ] Depois do C, o percentual da bateria volta a atualizar.
- [ ] A + B por 3 s mostra "Memória Limpa".
- [ ] Desligar e ligar mantém as leituras pendentes.
- [ ] A tela apaga em 5 min e o deep sleep entra em 10 min; o botão C acorda.

## Melhorias possíveis

Em ordem aproximada de valor:

1. **Enviar várias leituras por POST.** O campo `tag_reads` aceita uma lista. Mandar o lote inteiro deixaria o envio muito mais rápido, mas é preciso decidir o que fazer quando a API aceita parte do lote. Combinar com o time da API.
2. **Proteção contra envio repetido.** A API não tem chave de idempotência. Se a conexão cair depois de a API receber e antes de o coletor ler a resposta, a mesma tag vai de novo no próximo C.
3. **Horário da leitura.** Hoje o `firstSeenTimestamp` é o momento do envio, porque o coletor não tem relógio com bateria. Para mandar o momento da leitura, seria preciso acertar o relógio antes de ler (pelo celular ou por NTP ao ligar).
4. **Senha do Wi-Fi fora do código.** Hoje ela está em `config.h` e vai para o GitHub. Poderia ficar na NVS, gravada por um comando do Bluetooth.
5. **Bateria no ADC1.** Levar o divisor para um pino livre do ADC1 eliminaria a limitação do item 1 das decisões.
6. **Buzzer e vibração.** O código está pronto; falta ligar os componentes e definir os pinos.
7. **Aplicativo de celular.** O protocolo está em [PROTOCOLO_BLE.md](PROTOCOLO_BLE.md).
8. **Limpeza do código antigo.** O histórico de tags (`src/storage`, comando Bluetooth `0x04`) e a pasta `legacy/` não são mais usados.
