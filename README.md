# Coletor RFID portátil

Firmware do coletor RFID UHF portátil da Ativa Locação. O operador segura um botão perto do equipamento, o coletor lê a tag e envia o código ao celular por Bluetooth.

O projeto original é de Junior Polegato (estado de 23/10/2025, guardado em [coletor/](coletor/) sem alterações). A versão em uso fica em [lv_port_pc_vscode/](lv_port_pc_vscode/) e compila para dois alvos a partir do mesmo código: a placa ESP32 e um simulador de PC.

## Estado

| Parte | Situação |
| --- | --- |
| Tela nova, botões | Validado na placa |
| Máquina de estados, leitor simulado | Validado na placa |
| Bluetooth: teclado + envio da leitura | Validado na placa |
| Leitor RFID real | Validado na placa |
| Fila offline (memória permanente) | Validado na placa |
| Tela apaga após 5 min, acordar do deep sleep pelo botão | Validado na placa |
| Buzzer e vibração | Preparado, **inativo**: o aparelho não tem esses componentes ligados |
| Modo em lote (várias tags por leitura) | Não implementado; `rfid_tags()` já devolve todas as tags vistas |

## Como o aparelho funciona

- **Ler:** segure o botão C. Enquanto está pressionado, o coletor lê as tags ao alcance. Ao soltar, fica com a que respondeu mais vezes.
- **Com celular conectado:** a tag é digitada no celular como um teclado (modo planilha) e também enviada ao aplicativo. Se o aplicativo devolver o nome do equipamento em até 2 s, a tela mostra o nome; senão, mostra os 8 últimos caracteres da tag.
- **Sem celular:** a leitura fica guardada (até 250, mesmo desligando). Com aplicativo, o reenvio é automático ao reconectar. Só com teclado, aperte A ou B na tela "Pronto" para o coletor digitar as guardadas.
- **Sem tag:** "Nenhuma tag encontrada".
- **Tela:** apaga após 5 minutos sem uso. Qualquer botão acende; esse aperto não inicia leitura.
- **Desligamento:** após 10 minutos sem uso o ESP32 entra em deep sleep e acorda com o botão C. Ligar e desligar de verdade é feito pelo módulo carregador (1 clique liga, 2 cliques desligam).
- Só são aceitas tags no formato do filtro em [src/config.h](lv_port_pc_vscode/src/config.h) (`RFID_TAG_FILTER`), por exemplo `TP.01.123456`.

O protocolo Bluetooth para o aplicativo está em [docs/PROTOCOLO_BLE.md](docs/PROTOCOLO_BLE.md).

## Hardware

Placa LilyGO TTGO T-Display (ESP32, 16 MB de flash, sem PSRAM, tela ST7789 135x240 usada deitada).

| Função | GPIO | Observação |
| --- | --- | --- |
| Botão A | 27 | livre (envia as guardadas) |
| Botão B | 32 | livre (envia as guardadas) |
| Botão C | 33 | leitura; acorda do deep sleep |
| Antena RFID, RX do ESP32 | 2 | fio preto, TX do módulo |
| Antena RFID, TX do ESP32 | 15 | fio amarelo, RX do módulo |
| Antena RFID, enable | 17 | fio verde |
| Bateria (ADC) | 25 | fio azul |
| Luz de fundo da tela | 4 | |

Os botões usam pull-up interno: pressionado = nível baixo. O módulo RFID conversa em ASCII a 38400 8N1 (`\nU\r` = inventário) e não informa RSSI. O modelo exato do módulo não é conhecido.

## Estrutura do código

Tudo em [lv_port_pc_vscode/](lv_port_pc_vscode/):

| Arquivo | Papel |
| --- | --- |
| `main.cpp`, `main.h` | Inicialização do hardware, loop principal, bateria, deep sleep e a janela do simulador de PC |
| `src/config.h` | Opções de compilação: leitor real ou simulado, filtro de tags, pinos de buzzer/vibração, tempo da tela |
| `src/state/app.cpp` | Máquina de estados: pronto, lendo, enviando, resultado |
| `src/buttons/` | Leitura dos botões com debounce |
| `src/display/` | Barra superior e telas |
| `src/rfid/rfid.cpp` | Inventário e escolha da melhor tag |
| `src/rfid/rfid_real.cpp` | Leitor real (módulo na `Serial2`) |
| `src/rfid/rfid_sim.cpp` | Leitor simulado |
| `src/ble/` | Teclado Bluetooth + serviço GATT do coletor |
| `src/storage/` | Fila de leituras e número sequencial na memória permanente (LittleFS) |
| `src/power/` | Apagar e acender a tela |
| `src/feedback/` | Bipe e vibração (inativos enquanto os pinos forem `-1`) |
| `legacy/` | Menu antigo, fora da compilação, guardado para consulta |

O código separa os dois alvos com `#ifdef ESP32`. No simulador, o leitor é sempre o simulado, o Bluetooth é um interruptor na janela e a fila é gravada em arquivos `collector_*` na pasta de onde ele foi aberto.

## Preparar o ambiente (Windows)

O guia original, com mais detalhes, está em [coletor/preparation.md](coletor/preparation.md).

**Para a placa:**

1. Instale o [Arduino CLI](https://arduino.github.io/arduino-cli/).
2. Instale o core `esp32:esp32` versão **3.3.2**.
3. Instale as bibliotecas na pasta `Documentos\Arduino\libraries`:
   - `lvgl` 9.4.0
   - `TFT_eSPI` 2.5.43 — em `User_Setup_Select.h`, deixe ativo só `Setup25_TTGO_T_Display.h`
   - `NimBLE-Arduino` **2.3.6** (a 2.5.2 não compila com a biblioteca do teclado)
   - `ESP32-BLE-Keyboard`, na versão de github.com/JuniorPolegato (já ajustada para o NimBLE)
4. Rode [tools/sync_lvgl_fonts.ps1](tools/sync_lvgl_fonts.ps1). Ele copia para a biblioteca `lvgl` as fontes em negrito com acentos; sem isso "ã", "ç" e "ê" não aparecem na tela. Repita se reinstalar a `lvgl`.

**Para o simulador:** instale o [MSYS2](https://www.msys2.org/) e, no terminal *UCRT64*, os pacotes de GCC, CMake, Ninja e SDL2.

## Compilar e gravar a placa

No PowerShell:

```powershell
$cli  = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
$fqbn = 'esp32:esp32:lilygo_t_display:PSRAM=disabled,LoopCore=1,EventsCore=1,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=16M,UploadSpeed=921600,DebugLevel=none,EraseFlash=none'
$src  = 'c:\GitHub\coletor_rfid\lv_port_pc_vscode'
$out  = 'c:\GitHub\coletor_rfid\build\esp32_cli'

& $cli compile --fqbn $fqbn `
  --build-property "build.partitions=default_16MB" `
  --build-property "upload.maximum_size=6553600" `
  --build-path $out --jobs 4 $src

& $cli upload -p COM10 --fqbn $fqbn --input-dir $out

& $cli monitor -p COM10 --config baudrate=115200
```

Pontos de atenção:

- **Desconecte o fio preto da antena (GPIO 2) antes de gravar** e reconecte depois. Com ele ligado a gravação falha com `Wrong boot mode detected (0xb)`.
- A primeira compilação leva cerca de 14 minutos; as seguintes, 1 a 2. Não interrompa.
- Se aparecer `file truncated` ou `is not an object`, apague a pasta `build\esp32_cli` e compile de novo.
- Use `--jobs 4` ou menos; com mais, o compilador pode ficar sem memória.
- `EraseFlash=none` preserva as leituras guardadas. Troque por `EraseFlash=all` só quando quiser apagar tudo, inclusive a fila e o número sequencial.
- Depois de mudar os serviços Bluetooth, remova o pareamento de `Collector keyboard` no celular e pareie de novo.
- Para gravar a placa com o leitor simulado (sem antena), acrescente ao `compile`: `--build-property "compiler.cpp.extra_flags=-DRFID_SIMULATED=1"`.

## Compilar e abrir o simulador

No terminal MSYS2 UCRT64:

```bash
cd /c/GitHub/coletor_rfid/lv_port_pc_vscode
cmake -S . -B build_sim -G Ninja -DCMAKE_BUILD_TYPE=Debug   # só na primeira vez
cmake --build build_sim --parallel 4
./bin/main.exe
```

Na janela: a caixa de texto no alto lista as tags "ao alcance" (uma por linha, a primeira é a mais forte), o interruptor BLE faz o papel do celular e os botões A, B e C são os do aparelho. Um toque muito curto em C termina sem tag. O celular simulado confirma cada leitura com o nome "Betoneira 400L", exceto as de número múltiplo de 3, que ficam sem resposta para exercitar a espera de 2 s.

## Pendências conhecidas

- Conferir a porcentagem da bateria contra um multímetro; a conversão é linear entre 3,3 V (0%) e 4,1 V (100%).
- Definir os GPIOs de buzzer e motor de vibração em `src/config.h` quando forem instalados.
- O relógio se perde ao desligar; o aplicativo precisa acertá-lo a cada conexão.
- Avisos de compilação antigos e inofensivos em `main.cpp`: `RX2`, `TX2` e `TFT_MISO` redefinidos, `ADC_ATTEN_DB_11` obsoleto.
