# Protocolo Bluetooth do Coletor RFID

Este documento descreve como um aplicativo de celular conversa com o coletor por Bluetooth Low Energy (BLE). Ele vale para o firmware a partir da ETAPA 4.

> **Estado:** teclado, envio da leitura, fila offline e leitor RFID real validados na placa.

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

## Leituras guardadas (fila offline)

O coletor guarda em memória permanente até **250 leituras** que ainda não foram confirmadas. Elas sobrevivem a desligar o aparelho. Uma leitura entra na fila em dois casos:

- foi feita sem celular conectado;
- foi enviada ao aplicativo, mas a confirmação não chegou em 2 segundos.

**Reenvio para o aplicativo.** Quando o aplicativo ativa as notificações de Leitura e o coletor está na tela "Pronto", ele reenvia a leitura mais antiga, no mesmo formato de uma leitura nova, e espera a **Confirmação** com o mesmo `seq`. Só depois da confirmação a leitura sai da fila e a próxima é enviada. Sem confirmação em 2 segundos, o coletor tenta de novo a mesma leitura 3 segundos depois.

O que o aplicativo precisa cuidar:

- **Confirmar toda leitura recebida**, nova ou reenviada. Leitura não confirmada volta indefinidamente.
- **Ignorar repetidas.** Se a energia cair logo depois de uma confirmação, a mesma leitura (mesmo `seq`, mesmo EPC e horário) pode chegar outra vez. Confirme de novo e não registre em dobro.
- **Ordenar por `seq`.** Uma leitura nova feita enquanto ainda há guardadas é enviada na hora, antes das antigas.
- **Horário das guardadas.** Se o relógio não estava acertado no momento da leitura (bit 0 de flags = 0), o horário são segundos desde que o coletor ligou e não pode ser convertido em data.
- O `seq` é gravado e continua de onde parou depois de desligar. Volta a 1 depois de 65535.

**Só com o teclado (sem aplicativo).** As guardadas não são digitadas sozinhas, para não caírem em um campo errado do celular. A tela "Pronto" mostra quantas há e o operador aperta o botão A ou B para o coletor digitá-las em ordem. Como o teclado não tem confirmação, cada uma sai da fila logo depois de digitada.

**Fila cheia.** Sem celular e com 250 guardadas, o coletor recusa novas leituras e mostra "Memória cheia".

Gravar o firmware com a opção `EraseFlash=all` apaga a fila e o número sequencial.

## Limitações atuais

- O módulo RFID não informa RSSI: o campo vem sempre como `-128`. A melhor tag é a que respondeu mais vezes enquanto o botão estava pressionado. (Com o leitor simulado, o RSSI é fictício.)
- O horário se perde ao desligar; ver o comando **Acertar relógio**.

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
10. Desligue e ligue o coletor: a tela "Pronto" continua mostrando "1 guardada".
11. Ligue o Bluetooth, conecte e ative as notificações de `a71a0002-…`: a leitura guardada chega sozinha e se repete a cada 5 segundos até você escrever a confirmação com o `seq` dela. Depois da confirmação, o aviso de guardada some da tela.
