# Protocolo Bluetooth do Coletor RFID

Este documento descreve como um aplicativo de celular conversa com o coletor por Bluetooth Low Energy (BLE). Ele vale para o firmware com sessões de leitura (botões A e B). **Não é compatível com a versão anterior:** a leitura e a confirmação passaram a levar o número da sessão.

> **Estado:** teclado, envio da leitura, fila e leitor RFID real validados na placa na versão anterior. Sessões, modo em lote e sincronização por duplo clique foram validados na placa no modo teclado. O formato da confirmação (`0x01` com sessão) foi testado só no simulador, porque o aplicativo ainda não existe.

## Visão geral

O coletor aparece no Bluetooth com o nome **`Collector keyboard`** e oferece duas coisas na mesma conexão:

| Função | Para quê |
| --- | --- |
| Teclado HID (serviço `0x1812`) | Modo planilha: a tag lida é digitada no campo que estiver com o cursor, seguida de Enter. Funciona sem aplicativo. |
| Serviço do coletor (este documento) | O aplicativo recebe a leitura completa, confirma e devolve o nome do equipamento para a tela do coletor. |

Nada é enviado no momento da leitura: as leituras ficam guardadas no coletor até o operador pedir a sincronização com dois cliques rápidos no botão. Na sincronização, se o aplicativo estiver com as notificações ativas, as leituras vão pelo serviço do coletor; se não houver aplicativo ouvindo, são digitadas pelo teclado.

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

Enviada durante a sincronização, uma notificação por leitura pendente. O aplicativo precisa ativar as notificações desta característica; sem isso o coletor entende que não há aplicativo ouvindo.

| Byte | Tamanho | Campo | Descrição |
| --- | --- | --- | --- |
| 0 | 1 | flags | Bit 0 = 1: o horário é real (relógio já acertado pelo celular); 0: o horário são segundos desde que o coletor ligou. Bit 1 = 1: leitura do modo em lote (botão B); 0: leitura individual (botão A). Bits 2 a 7: **sessão** (1 a 63; 0 = leitura gravada por firmware anterior). |
| 1–2 | 2 | seq | Número sequencial da leitura (1 a 65535; 0 não é usado). |
| 3 | 1 | rssi | Força do sinal em dBm, com sinal (int8). `-128` (`0x80`) = não disponível. |
| 4–7 | 4 | horário | Segundos desde 01/01/1970 (UTC) quando o bit 0 de flags é 1. |
| 8… | 1–32 | EPC | Código da tag em texto ASCII, sem terminador. |

Com as tags aceitas hoje (até 12 caracteres, por exemplo `TP.01.123456`) o pacote tem no máximo 20 bytes e cabe em uma notificação sem negociar MTU.

A sessão é `flags >> 2`. Ela vai dentro de `flags`, e não em um byte próprio, para o pacote continuar cabendo em 20 bytes.

Exemplo: `0E 02 00 80 2A 00 00 00 54 50 2E 30 31 2E 31 32 33 34 35 36`
— `0x0E` = sessão 3, modo em lote, relógio não acertado; seq 2; RSSI não disponível; 42 s desde que ligou; EPC `TP.01.123456`.

### Comando (Write)

O primeiro byte diz qual é o comando.

| Código | Comando | Bytes seguintes |
| --- | --- | --- |
| `0x01` | Confirmação | seq (2 bytes) + sessão (1 byte) + nome do equipamento em UTF-8 (opcional, até 48 bytes) |
| `0x02` | Texto na tela | texto em UTF-8 (até 48 bytes) |
| `0x03` | Acertar relógio | segundos desde 01/01/1970 UTC (4 bytes) |
| `0x04` | Limpar histórico | nenhum |

Comandos com código desconhecido ou tamanho menor que o mínimo são ignorados. Texto além de 48 bytes é cortado. A tela mostra letras acentuadas do português; outros símbolos (emoji, por exemplo) não aparecem.

**Confirmação (`0x01`).** Diz ao coletor que a leitura foi **efetivamente registrada** pelo aplicativo. O `seq` e a sessão têm de ser os mesmos da leitura recebida (sessão = `flags >> 2`). Ao receber, o coletor retira essa leitura da fila na hora; a partir daí a mesma tag pode ser lida de novo. Confirmação com sessão diferente da leitura, ou de uma leitura que não está mais na fila, é ignorada.

Se a confirmação não chegar em **2 segundos**, a leitura continua na fila e o coletor segue para a próxima. A confirmação atrasada ainda é aceita e retira a leitura da fila. O nome do equipamento é aceito, mas não é mostrado na tela nesta versão (a tela mostra o andamento da sincronização).

Exemplo — confirmar a leitura 2 da sessão 3 com o nome `Betoneira`:
`01 02 00 03 42 65 74 6F 6E 65 69 72 61`

**Texto na tela (`0x02`).** Mostra o texto por 3 segundos. É ignorado enquanto o coletor está lendo ou esperando uma confirmação.

Exemplo — mostrar `Ola`: `02 4F 6C 61`

**Acertar relógio (`0x03`).** O coletor não tem relógio com bateria: o horário se perde ao desligar. O aplicativo deve enviar este comando a cada conexão.

Exemplo — 12/11/2026 12:00:00 UTC (1794484800 = `0x6AF5AA40`): `03 40 AA F5 6A`

**Limpar histórico (`0x04`).** Sem efeito prático nesta versão: o coletor não guarda mais histórico de tags descarregadas (ver "Controle de duplicidade"). O comando continua sendo aceito e a tela mostra "Histórico limpo". A fila de pendências não é apagada.

Exemplo: `04`

### Status (Read + Notify)

| Byte | Tamanho | Campo | Descrição |
| --- | --- | --- | --- |
| 0 | 1 | bateria | Carga em % (0 a 100). |
| 1–2 | 2 | pendentes | Leituras na fila: aguardando transmissão ou confirmação (0 a 250). |
| 3–4 | 2 | histórico | Sempre 0 nesta versão (o histórico de tags descarregadas não é mais usado). |

O valor é conferido a cada 5 segundos e notificado quando muda.

## Botões, modos de leitura e sessões

| Botão | Segurar (pelo menos 0,4 s) | Dois cliques rápidos |
| --- | --- | --- |
| A | **Leitura individual.** Lê as tags ao alcance enquanto pressionado. Ao soltar, registra **uma** leitura: a tag inédita que respondeu mais vezes. | Sincroniza as leituras **individuais** pendentes. |
| B | **Leitura em lote.** Registra **toda** tag inédita que aparecer enquanto pressionado, uma leitura para cada. Soltar pausa; segurar B de novo continua a mesma sessão. | Sincroniza as leituras **em lote** pendentes. |
| C | Nenhuma ação. | Nenhuma ação. Um clique simples envia as pendentes para a API da Ativa pelo Wi-Fi (não usa Bluetooth; ver o README). |

O botão C não participa do Bluetooth. Ele também acorda o aparelho do deep sleep.

**A e B juntos por 3 segundos** apagam todas as leituras guardadas, dos dois modos e das duas filas (Bluetooth e API), sem pergunta. Elas nunca chegam ao aplicativo; os números `seq` delas ficam sem uso e as tags podem ser lidas de novo.

**Como o coletor distingue os acionamentos.** O botão pressionado por 0,4 s ou mais é uma pressão longa e começa a leitura. Solto antes disso, é um clique curto. Dois cliques curtos do mesmo botão, com no máximo 0,35 s entre soltar o primeiro e apertar o segundo, são um duplo clique. Um clique curto sozinho não faz nada; um aperto longo nunca conta como clique. Os dois tempos são constantes em `src/state/app.h` (`APP_LONG_PRESS_MS` e `APP_DOUBLE_CLICK_MS`).

Uma **sessão** é o período em que o operador lê no mesmo modo. Ler com A depois de ter lido com B (ou o contrário) encerra a sessão e abre outra, com o número seguinte (1 a 63, depois volta a 1). O número é gravado e continua depois de desligar. Só a leitura troca de sessão; o duplo clique não.

Ao encerrar uma sessão por troca de modo, o coletor trata as leituras dela assim:

| Situação da leitura | O que acontece |
| --- | --- |
| Confirmada pelo celular | Já saiu da fila. |
| Transmitida, ainda sem confirmação | Continua na fila e sai na próxima sincronização daquele modo. |
| Ainda não transmitida | **É cancelada** na fila do Bluetooth: nunca chega ao aplicativo. Continua pendente na fila da API. |

> **Atenção:** como nada é transmitido antes do duplo clique, ler com o outro botão **sem sincronizar antes** apaga todas as leituras pendentes do modo anterior. A ordem de uso é: ler, sincronizar com dois cliques e só então trocar de modo.

## Sincronização (duplo clique)

Conectar o celular **não** inicia transmissão nenhuma. O operador escolhe a planilha ou a tela certa no celular e dá dois cliques no botão do modo que quer descarregar.

1. Dois cliques em A (ou em B). O coletor confere se há celular conectado e se há leituras pendentes daquele modo.
   - Sem celular: mostra "Celular desconectado" e nada é transmitido.
   - Sem pendentes daquele modo: mostra "Nenhuma leitura pendente".
2. A tela mostra "Preparando envio..." e depois "Enviando leituras..." com a quantidade de pendentes.
3. O coletor transmite **só as leituras daquele modo**, uma por vez, da mais antiga para a mais nova, e espera a **Confirmação** de cada uma por até 2 segundos. As leituras do outro modo ficam guardadas até o duplo clique no botão delas.
4. **Cada confirmação retira aquela leitura da fila na hora**, sem esperar o fim da sincronização. Com 10 leituras enviadas e 6 confirmadas, ficam 4 na fila.
5. A leitura que não foi confirmada a tempo continua na fila. Ao terminar a volta, o coletor tenta de novo as que sobraram, enquanto alguma estiver sendo confirmada.
6. Fila daquele modo vazia: "Sincronização concluída". Se uma volta inteira passar sem nenhuma confirmação, o coletor para e mostra "Faltam N leituras"; elas continuam guardadas para um novo duplo clique.
7. Se o Bluetooth cair no meio, o coletor para e mostra "Celular desconectado". Só as leituras ainda não confirmadas continuam guardadas.

Um novo duplo clique ou um aperto longo durante a sincronização é ignorado.

## Controle de duplicidade

O bloqueio de repetidas vale **só para as leituras ainda guardadas**: uma tag que está pendente em qualquer uma das duas filas não é registrada de novo. Isso continua valendo depois de desligar o aparelho ou de reconectar o Bluetooth, porque a fila fica em memória permanente.

**A fila do Bluetooth e a fila da API são independentes.** A confirmação do aplicativo retira a leitura só da fila do Bluetooth; o envio pela API (botão C) retira só da fila da API. A leitura sai da memória, e a tag pode ser lida de novo, quando os dois canais confirmaram ou quando o operador limpa a memória com A + B.

Durante a leitura, as tags repetidas são ignoradas. Se só havia repetidas ao alcance, a tela mostra "Já registrada" com o final da tag.

Consequências para o aplicativo:

- A mesma tag pode chegar em mais de uma leitura, cada uma com seu `seq`: é o operador lendo de novo um equipamento já descarregado. Se isso não for desejado, o controle fica por conta do aplicativo.
- O reenvio de uma leitura ainda não confirmada vem sempre com o `seq` e a sessão originais; é por eles que o aplicativo reconhece a leitura já processada.
- Uma leitura cancelada por troca de modo nunca chega ao aplicativo; depois de sair da memória, a mesma tag pode aparecer em uma leitura nova, com outro `seq`.

## Fila de pendências

Toda leitura entra na fila no momento em que é feita e só sai quando o celular confirma. A fila guarda até **250 leituras**, somando os dois modos, e sobrevive a desligar o aparelho.

O que o aplicativo precisa cuidar:

- **Confirmar só depois de registrar.** A confirmação apaga a leitura do coletor; se o aplicativo confirmar e não gravar, a leitura se perde.
- **Confirmar toda leitura recebida**, nova ou reenviada, devolvendo o `seq` e a sessão dela.
- **Reconhecer as já processadas.** A mesma leitura (mesmo `seq`, mesma sessão, mesmo EPC e horário) pode chegar mais de uma vez: quando a confirmação se perde, quando o Bluetooth cai no meio do envio ou quando a energia cai logo depois de uma confirmação. O aplicativo deve responder com a confirmação de novo e **não criar um segundo registro**.
- **Ordenar por `seq`.** As leituras podem chegar fora de ordem quando alguma fica para a volta seguinte.
- **Horário.** Se o relógio não estava acertado no momento da leitura (bit 0 de flags = 0), o horário são segundos desde que o coletor ligou e não pode ser convertido em data. Envie **Acertar relógio** ao conectar.
- O `seq` é gravado e continua de onde parou depois de desligar. Volta a 1 depois de 65535.

**Só com o teclado (sem aplicativo).** O duplo clique digita as pendentes daquele modo, uma a uma, no campo que estiver com o cursor. O teclado não tem confirmação: a tag digitada conta como confirmada e sai da fila.

**Fila cheia.** Com 250 leituras pendentes, o coletor recusa novas leituras e mostra "Memória cheia".

Gravar o firmware com a opção `EraseFlash=all` apaga a fila, o número sequencial e a sessão.

## Limitações atuais

- O módulo RFID não informa RSSI: o campo vem sempre como `-128`. A melhor tag é a que respondeu mais vezes enquanto o botão estava pressionado. (Com o leitor simulado, o RSSI é fictício.)
- O horário se perde ao desligar; ver o comando **Acertar relógio**.

## Roteiro de teste com o nRF Connect (Android)

Nos valores a escrever, o 2º byte é o `seq` e o 4º é a sessão. A sessão é o primeiro byte da notificação dividido por 4 (em hexadecimal: `04` → 1, `08` → 2, `0A` → 2 em lote, `0E` → 3 em lote).

1. Abra o nRF Connect, aba **Scanner**, e conecte em `Collector keyboard`. Aceite o pareamento.
2. Abra o serviço `a71a0001-…` (aparece como *Unknown Service*).
3. Na característica `a71a0002-…`, toque no ícone de três setas para baixo para ativar as notificações.
4. No coletor, segure o botão A por 1 segundo e solte. A tela mostra o final da tag e "Leitura feita". **Nenhuma notificação chega.**
5. Dê um clique curto em A: nada acontece. Pressione C: nada acontece.
6. Dê dois cliques rápidos em A: a tela mostra "Preparando envio..." e "Enviando leituras...", e chega uma notificação com o `seq` no 2º byte e o código da tag no final.
7. Sem responder, espere: depois de 2 segundos a tela mostra "Falta 1 leitura".
8. Dê dois cliques em A de novo e, em até 2 segundos, escreva na característica `a71a0003-…` (seta para cima, tipo **BYTE ARRAY**) a confirmação. Para `seq` 1 e sessão 1: `010100014265746F6E65697261`. A tela mostra "Sincronização concluída".
9. Leia a mesma tag de novo com A: ela é registrada normalmente, porque já foi descarregada. Antes de sincronizar, leia-a mais uma vez: agora a tela mostra "Já registrada" (está pendente). Dê dois cliques em A e confirme para esvaziar a fila.
10. Com várias tags inéditas ao alcance, segure B por 3 segundos: a tela mostra "Lote: N". Nada chega.
11. Dois cliques em A: "Nenhuma leitura pendente". Dois cliques em B: chega a primeira leitura do lote (o primeiro byte tem o bit 1 ligado). Confirme-a: ela sai da fila e a próxima chega.
12. Confirme só algumas e desligue o Bluetooth do celular: a tela mostra "Celular desconectado" e a tela "Pronto" mostra quantas ficaram pendentes em B.
13. Dois cliques em B sem celular: "Celular desconectado".
14. Desligue e ligue o coletor: a quantidade de pendentes é a mesma.
15. Ligue o Bluetooth, conecte e ative as notificações: **nada chega sozinho**. Dois cliques em B: chegam só as que não foram confirmadas.
16. Escreva `024F6C61`: a tela mostra `Ola` por 3 segundos.
17. Na característica `a71a0004-…`, toque na seta para baixo (ler): bateria (`41` = 65%), pendentes (2 bytes) e histórico (2 bytes, sempre zero).
18. Escreva `04`: a tela mostra "Histórico limpo" (sem outro efeito nesta versão).
