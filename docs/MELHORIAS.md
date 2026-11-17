# Melhorias do firmware do Coletor RFID

Este documento resume o que mudou no firmware do coletor RFID portátil da Ativa Locação em relação à versão original de Junior Polegato (estado de 23/10/2025, guardada sem alterações na pasta `coletor/`). Situação em 16/11/2026.

Para o uso do aparelho no dia a dia, veja o [README](../README.md). Para mexer no firmware no futuro, veja o [guia de manutenção](MANUTENCAO.md). Para quem vai desenvolver o aplicativo do celular, veja o [Protocolo Bluetooth](PROTOCOLO_BLE.md).

## Resumo

O coletor continua sendo o mesmo aparelho, com o mesmo hardware e a mesma forma de gravar. O que mudou é o programa:

- A tela e os botões ficaram mais simples: segurar um botão lê, dois cliques enviam.
- Há dois modos de leitura: uma tag por vez (botão A) ou várias de uma vez (botão B).
- As leituras ficam guardadas no aparelho, mesmo desligado, até o celular confirmar que recebeu.
- Além do modo teclado (planilha), que já existia, o coletor passou a ter um canal próprio para um aplicativo de celular.
- A tela apaga sozinha para economizar bateria.

## O que não mudou

- Placa LilyGO TTGO T-Display (ESP32), tela, módulo RFID UHF, antena e pinos.
- Ligar e desligar pelo módulo carregador (1 clique liga, 2 cliques desligam).
- Modo teclado: a tag é digitada no campo do celular que estiver com o cursor, seguida de Enter.
- Nome Bluetooth `Collector keyboard`.
- Filtro das tags aceitas (formato `XX.00.000000`).
- Desligamento automático (deep sleep) após 10 minutos sem uso.

## Como o aparelho funciona agora

| Ação | Resultado |
| --- | --- |
| Segurar A | Lê uma tag. Ao soltar, fica com a que respondeu mais vezes. |
| Segurar B | Lê em lote: registra toda tag nova que aparecer enquanto o botão está pressionado. |
| Dois cliques rápidos em A | Envia ao celular as leituras individuais guardadas. |
| Dois cliques rápidos em B | Envia ao celular as leituras do lote guardadas. |
| Segurar A e B juntos por 3 s | Apaga todas as leituras ainda não enviadas, sem pergunta. |
| Um clique em C | Envia todas as leituras guardadas para a API da Ativa pelo Wi-Fi. |

Regras que o operador precisa conhecer:

- **Nada é enviado sozinho.** Nem ao ler, nem ao conectar o celular. O operador escolhe a planilha ou a tela no celular e só então dá os dois cliques.
- **Trocar de A para B (ou de B para A) apaga as leituras do modo anterior que ainda não foram enviadas.** A ordem certa é: ler, enviar com dois cliques, e só depois usar o outro botão.
- **Dois envios independentes:** cada leitura precisa ir para o celular (dois cliques) e para a API (botão C). Ela só sai da memória depois dos dois, ou quando o operador limpa com A + B.
- **Tag repetida:** uma tag que ainda está na memória não é lida de novo ("Já registrada").
- **Memória:** até 250 leituras guardadas. Com a memória cheia, o coletor recusa novas leituras.

## Melhorias, por etapa

### Etapa 1 — Tela e botões

- Layout novo, escuro, com textos grandes. As imagens de fundo e a animação da bandeira foram retiradas.
- Barra superior fixa com o estado do celular ("Celular ligado" ou "Sem celular") e a bateria.
- Botões com filtro de ruído (debounce), para um aperto não contar duas vezes.
- O menu antigo saiu da tela; o código dele foi guardado na pasta `legacy/` para consulta.

### Etapa 2 — Organização interna e simulador

- O programa foi dividido em módulos (botões, tela, leitor RFID, Bluetooth, memória, energia) com uma máquina de estados central. Antes, tudo ficava em um arquivo só.
- Leitor RFID simulado, para testar no computador sem a placa e sem tags.
- O mesmo código compila para a placa e para um simulador de PC.
- Retorno sonoro e de vibração preparados no código. **Não funcionam ainda:** o aparelho não tem buzzer nem motor de vibração ligados.

### Etapa 3 — Canal para aplicativo de celular

- Além do teclado, o coletor oferece um serviço Bluetooth próprio, na mesma conexão.
- Por ele, o aplicativo recebe cada leitura com número sequencial, horário e modo (individual ou lote), e responde confirmando.
- O aplicativo também pode acertar o relógio do coletor e mostrar um texto na tela dele.
- O coletor informa ao aplicativo a carga da bateria e quantas leituras estão guardadas.

### Etapa 4 — Leituras guardadas no aparelho

- As leituras ficam em memória permanente e sobrevivem a desligar o aparelho ou à falta de celular.
- Uma leitura só é apagada depois que o celular confirma o recebimento.
- O número sequencial de cada leitura também é guardado e continua de onde parou.

### Etapa 5 — Leitor RFID sem travar o aparelho

- A comunicação com o módulo RFID foi reescrita para não bloquear o resto do programa: a tela e os botões continuam respondendo durante a leitura.
- Enquanto o botão está pressionado, o coletor repete o inventário e conta quantas vezes cada tag respondeu.

### Etapa 6 — Economia de bateria

- A tela apaga após 5 minutos sem uso. Qualquer botão acende, e esse aperto não inicia leitura.
- O deep sleep após 10 minutos foi mantido; o aparelho acorda pelo botão C.

### Correções feitas depois das etapas

**Confirmação leitura por leitura.** Cada leitura sai da memória no momento em que o celular a confirma, sem esperar o fim do envio. Se o Bluetooth cair no meio, ficam guardadas só as que não foram confirmadas, e o próximo envio retoma dali. Uma leitura reenviada leva o mesmo número sequencial, para o aplicativo não registrar em dobro.

**Modo em lote (botão B).** Leitura contínua de várias tags, uma leitura para cada tag nova.

**Sessões.** Cada troca entre A e B abre uma sessão numerada. O número vai junto com a leitura e com a confirmação.

**Envio por duplo clique.** O envio deixou de ser automático. Dois cliques rápidos no botão enviam as leituras daquele modo; os grupos A e B não se misturam. O coletor distingue três acionamentos:

| Acionamento | Como é reconhecido |
| --- | --- |
| Pressão longa (leitura) | Botão pressionado por 0,4 s ou mais |
| Clique curto (nada) | Botão solto antes de 0,4 s |
| Duplo clique (envio) | Segundo clique do mesmo botão até 0,35 s depois do primeiro |

Durante o envio a tela mostra "Preparando envio...", "Enviando leituras..." com a quantidade que falta e, no fim, "Sincronização concluída". Sem celular, mostra "Celular desconectado"; sem nada a enviar, "Nenhuma leitura pendente".

**Controle de tags repetidas.** A regra passou por três versões a pedido da operação. A que vale hoje: só as leituras ainda não enviadas bloqueiam a tag. Depois de enviada ao celular, a tag pode ser lida de novo.

**Apagar as pendentes (A + B por 3 s).** Limpa a memória de leituras não enviadas direto no aparelho.

### Envio para a API da Ativa pelo Wi-Fi (botão C)

- O Wi-Fi fica desligado. No clique em C o coletor liga o Wi-Fi, conecta na rede `Ativa`, envia e desliga. Isso acrescenta alguns segundos a cada envio.
- Durante o envio pela API o Bluetooth também desliga: com os dois rádios ligados não sobrava memória para a conexão HTTPS. No fim, o Bluetooth volta e o celular reconecta sozinho.
- O Wi-Fi não fica ligado o tempo todo porque a bateria é medida por um pino que o ESP32 não deixa ler com o Wi-Fi ligado. Na primeira versão, com Wi-Fi sempre ligado, o aparelho reiniciava sem parar.
- Um clique no botão C envia cada leitura guardada para `https://api-app.ativalocacao.com.br/rfid`, no formato da API (`tag_reads`), com o nome de leitor `ColetorTI`.
- O horário de cada envio vem da internet. A conexão é HTTPS com conferência do certificado.
- A leitura só sai da fila da API quando a API responde com sucesso. Se o Wi-Fi ou a API falharem no meio, as que faltam continuam guardadas.
- A fila da API é separada da fila do Bluetooth: enviar por um canal não apaga a leitura do outro.

## Situação dos testes

| Item | Situação |
| --- | --- |
| Tela nova e botões | Validado na placa |
| Leitura de tags reais | Validado na placa |
| Modo teclado (planilha) | Validado na placa |
| Leituras guardadas ao desligar | Validado na placa |
| Tela apaga em 5 min; acordar do deep sleep pelo botão C | Validado na placa |
| Leitura em lote, envio por duplo clique e releitura após o envio | Validado na placa |
| A + B por 3 s apaga as pendentes | Validado na placa |
| Wi-Fi e envio para a API (botão C) | Validado na placa |
| Envio pelo serviço próprio com um aplicativo de verdade | Testado com o nRF Connect nas etapas iniciais; não há aplicativo final ainda |

Além dos testes na placa, a lógica é conferida por 107 verificações automáticas no computador (leitura, envio, confirmação parcial, desconexão no meio do envio, reinício, duplo clique, troca de modo, filas do Bluetooth e da API). O teste fica em [lv_port_pc_vscode/tests/app_test.cpp](../lv_port_pc_vscode/tests/app_test.cpp).

## Pontos de atenção

- **Quem usa só um canal enche a memória.** Uma leitura enviada só pelo Bluetooth (ou só pela API) continua guardada, esperando o outro canal, e a tag fica como "Já registrada". Com 250 leituras guardadas o coletor para de ler. A saída é A + B por 3 s.
- **A senha do Wi-Fi está escrita no código**, que fica no GitHub. Se o repositório for público, a senha da rede fica exposta.
- **Durante o envio pela API o percentual da bateria não atualiza.** Ele volta a ser medido quando o Wi-Fi desliga, no fim do envio.
- **Durante o envio pela API o celular fica desconectado.** O modo teclado não digita até o Bluetooth voltar, alguns segundos depois do fim do envio.
- **A API não tem autenticação nem proteção contra repetição.** Se a conexão cair depois de a API receber e antes de o coletor ter a resposta, a mesma tag é enviada de novo no próximo clique em C.

- **Troca de botão apaga leituras não enviadas.** É a regra pedida, mas é o erro de operação mais provável: ler com A, esquecer de enviar e ler com B.
- **A + B por 3 s não pede confirmação.** Um aperto acidental dos dois botões por 3 segundos perde as leituras guardadas.
- **O coletor não impede o mesmo equipamento de ser enviado duas vezes** em envios diferentes. Se a planilha ou o aplicativo não pode ter linha repetida, esse controle fica do lado do celular.
- **Gravar o firmware com `EraseFlash=all` apaga as leituras guardadas.** Use sempre `EraseFlash=none`.
- **Para gravar, o fio preto da antena precisa estar desconectado** (ele usa o GPIO 2, que interfere na gravação).

## Pendências

- Buzzer e vibração: o código está pronto, falta definir os pinos e ligar os componentes.
- Percentual da bateria: falta conferir com multímetro.
- O módulo RFID não informa a força do sinal; a "melhor tag" é a que respondeu mais vezes.
- O relógio do coletor se perde ao desligar; o aplicativo precisa acertá-lo a cada conexão.
- O nome do equipamento, que o aplicativo pode devolver na confirmação, não é mostrado na tela nesta versão.
- O histórico de tags já enviadas e o comando Bluetooth "Limpar histórico" continuam no código, sem uso, caso a regra de bloqueio volte.
- Aplicativo de celular: o protocolo está documentado, o aplicativo ainda não existe.
