# Protocolo Bluetooth do Coletor RFID

Este documento descreve como um aplicativo de celular conversa com o coletor por Bluetooth Low Energy (BLE). Ele vale para o firmware a partir da ETAPA 3.

> **Estado:** o serviço foi escrito e testado apenas no simulador de PC, que não tem Bluetooth de verdade. O funcionamento na placa ainda precisa ser validado com o nRF Connect (roteiro no fim deste documento).

## Visão geral

O coletor aparece no Bluetooth com o nome **`Collector keyboard`** e oferece duas coisas na mesma conexão:

| Função | Para quê |
| --- | --- |
| Teclado HID (serviço `0x1812`) | Modo planilha: a tag lida é digitada no campo que estiver com o cursor, seguida de Enter. Funciona sem aplicativo. |
| Serviço do coletor (este documento) | O aplicativo recebe a leitura completa, confirma e devolve o nome do equipamento para a tela do coletor. |

Os dois funcionam juntos: a cada leitura, o coletor digita a tag pelo teclado **e** envia a leitura pelo serviço.

O anúncio Bluetooth contém só o nome e o serviço de teclado, porque o UUID do serviço do coletor não cabe nos 31 bytes do anúncio. O aplicativo deve procurar o aparelho pelo nome (ou entre os aparelhos já pareados) e, depois de conectar, localizar o serviço pelo UUID.

O coletor pede pareamento ao conectar, por causa do teclado. As características do serviço do coletor não exigem criptografia.

## Serviço e características

Serviço: `a71a0001-6c0f-4e6b-9d2a-3f5b8c1e7a10`

| Característica | UUID | Propriedades | Sentido |
| --- | --- | --- | --- |
| Leitura | `a71a0002-6c0f-4e6b-9d2a-3f5b8c1e7a10` | Notify | coletor → celular |
| Comando | `a71a0003-6c0f-4e6b-9d2a-3f5b8c1e7a10` | Write | celular → coletor |
| Status | `a71a0004-6c0f-4e6b-9d2a-3f5b8c1e7a10` | Read, Notify | coletor → celular |

Todos os números de mais de um byte estão em **little-endian** (byte menos significativo primeiro).

### Leitura (Notify)

Enviada quando o operador solta o botão e uma tag foi lida. O aplicativo precisa ativar as notificações desta característica; sem isso o coletor entende que não há aplicativo ouvindo.

| Byte | Tamanho | Campo | Descrição |
| --- | --- | --- | --- |
| 0 | 1 | flags | bit 0 = 1: o horário é real (relógio já acertado pelo celular). Bit 0 = 0: o horário são segundos desde que o coletor ligou. |
| 1–2 | 2 | seq | Número sequencial da leitura (1 a 65535; 0 não é usado). |
| 3 | 1 | rssi | Força do sinal em dBm, com sinal (int8). `-128` (`0x80`) = não disponível. |
| 4–7 | 4 | horário | Segundos desde 01/01/1970 (UTC) quando o bit 0 de flags é 1. |
| 8… | 1–32 | EPC | Código da tag em texto ASCII, sem terminador. |

Com as tags aceitas hoje (até 12 caracteres, por exemplo `TP.01.123456`) o pacote tem no máximo 20 bytes e cabe em uma notificação sem negociar MTU.

Exemplo: `00 02 00 D6 2A 00 00 00 54 50 2E 30 31 2E 31 32 33 34 35 36`
— relógio não acertado, seq 2, RSSI −42 dBm, 42 s desde que ligou, EPC `TP.01.123456`.

### Comando (Write)

O primeiro byte diz qual é o comando.

| Código | Comando | Bytes seguintes |
| --- | --- | --- |
| `0x01` | Confirmação | seq (2 bytes) + nome do equipamento em UTF-8 (opcional, até 48 bytes) |
| `0x02` | Texto na tela | texto em UTF-8 (até 48 bytes) |
| `0x03` | Acertar relógio | segundos desde 01/01/1970 UTC (4 bytes) |

Comandos com código desconhecido ou tamanho menor que o mínimo são ignorados. Texto além de 48 bytes é cortado. A tela mostra letras acentuadas do português; outros símbolos (emoji, por exemplo) não aparecem.

**Confirmação (`0x01`).** Responde a uma leitura. O `seq` tem de ser o mesmo da leitura recebida. Com nome, o coletor mostra o nome do equipamento; sem nome, mostra os 8 últimos caracteres da tag. A confirmação precisa chegar em até **2 segundos** depois da leitura; depois disso o coletor mostra os 8 últimos caracteres da tag e ignora a confirmação atrasada.

Exemplo — confirmar a leitura 2 com o nome `Betoneira`:
`01 02 00 42 65 74 6F 6E 65 69 72 61`

**Texto na tela (`0x02`).** Mostra o texto por 3 segundos. É ignorado enquanto o coletor está lendo ou esperando uma confirmação.

Exemplo — mostrar `Ola`: `02 4F 6C 61`

**Acertar relógio (`0x03`).** O coletor não tem relógio com bateria: o horário se perde ao desligar. O aplicativo deve enviar este comando a cada conexão.

Exemplo — 12/11/2026 12:00:00 UTC (1794484800 = `0x6AF5AA40`): `03 40 AA F5 6A`

### Status (Read + Notify)

| Byte | Tamanho | Campo | Descrição |
| --- | --- | --- | --- |
| 0 | 1 | bateria | Carga em % (0 a 100). |
| 1–2 | 2 | pendentes | Leituras feitas sem celular que ainda não foram entregues. |

O valor é conferido a cada 5 segundos e notificado quando muda.

## Sequência de uma leitura

1. O operador segura o botão: o coletor lê as tags ao alcance.
2. O operador solta o botão: o coletor escolhe a tag de sinal mais forte e mostra "Enviando...".
3. O coletor envia a notificação de **Leitura** e digita a tag pelo teclado.
4. O aplicativo responde com o comando **Confirmação** (mesmo `seq`) e o nome do equipamento.
5. O coletor mostra o nome e o número da leitura por 3 segundos.

Se não houver aplicativo ouvindo, ou se a confirmação não chegar em 2 segundos, o coletor mostra os 8 últimos caracteres da tag.

## Limitações atuais

- **Leituras sem celular não são reenviadas.** Sem conexão, o coletor só conta as leituras (campo `pendentes`) e as perde ao desligar. A fila gravada em memória permanente, com reenvio em ordem e remoção só depois da confirmação, entra na ETAPA 4.
- O número sequencial volta a 1 quando o coletor é desligado (também será gravado na ETAPA 4).
- O leitor RFID ainda é o simulado: as tags e o RSSI são fictícios. O módulo RFID real não informa RSSI; quando ele entrar (ETAPA 5), o campo virá como `-128`.

## Roteiro de teste com o nRF Connect (Android)

1. Abra o nRF Connect, aba **Scanner**, e conecte em `Collector keyboard`. Aceite o pareamento.
2. Abra o serviço `a71a0001-…` (aparece como *Unknown Service*).
3. Na característica `a71a0002-…`, toque no ícone de três setas para baixo para ativar as notificações.
4. No coletor, segure o botão de leitura por 1 segundo e solte. Deve chegar uma notificação que começa com `00 01 00 …` e termina com o código da tag.
5. Sem responder, espere 2 segundos: a tela do coletor mostra os 8 últimos caracteres da tag.
6. Faça outra leitura e, em até 2 segundos, escreva na característica `a71a0003-…` (seta para cima, tipo **BYTE ARRAY**) a confirmação com o `seq` recebido. Para a leitura 2: `0102004265746F6E65697261`. A tela deve mostrar `Betoneira`. Dica: deixe o valor digitado antes de soltar o botão, trocando só o segundo byte pelo `seq` esperado.
7. Escreva `024F6C61`: a tela mostra `Ola` por 3 segundos.
8. Na característica `a71a0004-…`, toque na seta para baixo (ler): o primeiro byte é a bateria em hexadecimal (`41` = 65%).
9. Desligue o Bluetooth do celular e faça uma leitura: a tela mostra "1 guardada".
