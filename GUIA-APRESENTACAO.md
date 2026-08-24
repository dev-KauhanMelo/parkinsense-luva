# Guia de apresentação do ParkinSense

Este guia existe para você conseguir explicar a luva inteira, do que aparece na
tela até a última linha do código, sem depender de decorar nada. A ideia é que
você entenda a lógica: se entender por que cada coisa está ali, a explicação sai
sozinha, com as suas palavras.

O guia assume que quem lê não sabe programar. Toda vez que aparecer um conceito
de C++ pela primeira vez, ele é explicado ali mesmo, junto com o pedaço de
código que usa aquele conceito. Se você já souber a parte de programação, pule
direto para as seções que explicam o funcionamento.

---

## O que a luva faz, em três frases

A luva tem um sensor de movimento nas costas da mão que mede o quanto a mão
está acelerando, cinquenta vezes por segundo. O programa dentro do ESP32 pega
esses números e descobre se o movimento tem a assinatura do tremor
parkinsoniano, que é uma oscilação rápida e regular entre três e meia e sete
vezes por segundo. Quando essa assinatura aparece, os cinco motores vibram
juntos por cinco segundos, na tentativa de ocupar o canal sensorial da pele e
disputar espaço com o sinal do tremor.

Tudo o mais que existe no projeto serve a esse trio: medir bem, decidir bem e
acionar bem.

---

# PARTE 1: A TELA DO PROCESSING, NOME POR NOME

O programa em Processing não controla nada. Ele só escuta o que a luva conta
pelo cabo USB e desenha. Se você fechar o Processing, a luva continua
funcionando igual. Isso é importante saber, porque alguém pode perguntar se o
computador é necessário. Não é: ele é uma janela para dentro da luva, um
instrumento de observação.

## A linha de cima

Aparece algo como `5379 linhas (1 ign.)`. O primeiro número conta quantas
mensagens válidas chegaram desde que você abriu o programa. Como a luva manda
cinquenta mensagens por segundo, esse número cresce rápido, e serve para você
saber que a comunicação está viva. Se ele congelar, o cabo caiu ou a placa
travou.

O `ign.` significa "ignoradas". Toda vez que a luva liga, ela imprime mensagens
de texto comum, como o resultado da calibração e o teste dos motores. Essas
mensagens não são medição, então o Processing as descarta. Um punhado de linhas
ignoradas logo no começo é absolutamente normal. Muitas linhas ignoradas
acumulando durante o uso indicariam ruído no cabo.

## Roll e pitch

São os dois ângulos de inclinação da mão. Roll é o quanto a mão está girada
para o lado, como quando você vira a palma para cima. Pitch é o quanto ela está
inclinada para frente ou para trás, como quando você aponta os dedos para baixo.

Eles servem só para girar o desenho da mão na tela. Nenhum dos dois entra na
decisão de ligar os motores. Vale saber disso, porque é uma pergunta natural:
"a inclinação influencia?" Não influencia.

## O estado, logo abaixo do roll e pitch

Aqui aparece uma de três palavras, e cada uma conta uma fase diferente do ciclo
de trabalho da luva.

**MEDINDO** quer dizer que os motores estão parados e o sensor está lendo a mão
de verdade, agora. É a única situação em que os números da tela são leitura ao
vivo.

**CONTRA-ESTIMULO ATIVO** quer dizer que os motores estão vibrando. Durante essa
fase o sensor não é usado para medir, porque ele estaria medindo a vibração dos
próprios motores em vez do tremor da pessoa. Os números na tela ficam
congelados no último valor válido.

**ATUALIZANDO JANELA** quer dizer que os motores acabaram de parar e o programa
está juntando amostras novas para poder analisar de novo. Isso leva dois
segundos e meio. Nessa fase os números também estão congelados, e o painel
avisa isso em letras pequenas.

Esse aviso existe por um motivo concreto. Antes de ele existir, a tela mostrava
o valor antigo como se fosse atual, e chegou a anunciar tremor detectado com a
mão parada em cima da mesa. Parecia defeito, mas era só o painel repetindo a
última medida sem avisar.

## Critérios de disparo: força, pureza e nitidez

Esta é a parte mais importante da tela, e a que mais vale você dominar. São três
medidas, e as três precisam estar verdes ao mesmo tempo para os motores
ligarem. Cada uma protege contra um jeito diferente de o sistema se enganar.

### Força

É o tamanho do tremor, medido em g. Um g é a aceleração da gravidade, a mesma
que faz as coisas caírem. Quando a tela mostra `0,673 g`, significa que a mão
está sofrendo uma aceleração de ida e volta com esse tamanho, dentro da faixa de
frequência do tremor.

O mínimo é 0,08 g, que corresponde mais ou menos a um tremor de oito décimos de
milímetro a cinco oscilações por segundo. É um movimento bem pequeno, quase
invisível a olho nu, e mesmo assim já conta.

O que a força protege: movimento voluntário lento. Se você acenar com a mão
devagar, mesmo com muita amplitude, quase nada dessa energia cai na faixa do
tremor, e a força fica abaixo do mínimo.

### Pureza

É a proporção da energia que está na faixa rápida, de três e meia a sete
oscilações por segundo, comparada com a faixa lenta, de meia a três oscilações
por segundo. O valor vai de zero a um. Meio significa empate entre as duas
faixas, e o mínimo exigido é exatamente meio.

O que a pureza protege: gesto rápido misturado com movimento normal. Se a
pessoa estiver andando, gesticulando ou girando o punho, a faixa lenta enche de
energia e a pureza cai.

Aqui vale contar uma coisa que descobrimos testando, porque é uma boa história
para a apresentação. No começo o mínimo de pureza era 0,75, e a luva quase não
ligava por mais que a gente sacudisse a mão. O motivo é que sacudir a mão no ar
não produz só oscilação: o punho gira junto, e girar o punho redistribui a
gravidade inteira, que vale um g, entre os eixos do sensor. Isso enchia a faixa
lenta e derrubava a pureza. Uma pessoa com tremor de repouso, com o braço
apoiado, quase não gira o punho, então isso não acontece com ela. Ou seja, o
limite antigo atrapalhava o nosso teste de bancada, não o uso real.

### Nitidez

É o quanto a energia está concentrada em uma única frequência. O programa mede
o pico da faixa do tremor e divide pela média dessa mesma faixa. Se toda a
energia estiver em uma frequência só, o pico é muito maior que a média e o
número sobe. Se a energia estiver espalhada por toda a faixa, pico e média ficam
parecidos e o número cai. O mínimo é 3,20.

O que a nitidez protege: impactos. E aqui está o achado mais interessante do
projeto. Digitar no teclado são mais ou menos cinco batidas por segundo, o que
dá cinco oscilações por segundo, exatamente no meio da faixa do tremor
parkinsoniano. Nem a força nem a pureza conseguem separar as duas coisas,
porque digitar realmente tem energia forte e realmente está na faixa certa.

O que separa é a forma. Tremor é uma oscilação sustentada, então concentra a
energia em uma frequência. Digitar são golpes, e cada golpe é um estouro curto
que espalha energia por toda a faixa. Medindo em simulação, tremor sustentado
entre quatro e seis oscilações por segundo dá nitidez perto de 4,6 de forma
bem estável, enquanto digitar com o ritmo natural de quem digita dá 2,2, e
batidas na mesa dão 2,4.

Se alguém perguntar qual foi a maior dificuldade técnica do projeto, esta é uma
resposta excelente, porque mostra investigação de verdade.

## O veredito, logo abaixo dos três critérios

Se os três estiverem verdes, aparece **TREMOR DETECTADO**. Se não, aparece
**sem tremor**, e uma linha menor dizendo qual critério faltou, com o motivo
mais provável em linguagem comum. Se faltou nitidez, por exemplo, ele sugere
"parece impacto, nao oscilacao (digitar?)".

Essa linha foi feita para você não precisar interpretar número nenhum na hora da
apresentação. Ela já traduz.

## Eixos do acelerômetro: X, Y e Z

O sensor mede aceleração em três direções perpendiculares entre si, como as
três arestas de uma quina de caixa. X, Y e Z são essas três direções, e cada
barra mostra quanto tremor foi encontrado naquela direção.

Preste atenção neste ponto, porque é o erro de leitura mais comum: **X, Y e Z
não são dedos**. A luva tem um único sensor, nas costas da mão, então ela mede a
mão inteira e não tem como saber qual dedo está tremendo. Se alguém perguntar
por que não dá para detectar dedo por dedo, a resposta é essa: precisaria de um
sensor por dedo.

A força que aparece nos critérios é a combinação das três direções, calculada do
mesmo jeito que se calcula a diagonal de uma caixa a partir dos três lados.

## Canais: Polegar, Indicador, Nervo frente, Nervo trás, Mindinho

São os cinco pontos da luva onde existe um motor e um LED. Cada nome é uma
posição física na mão, não uma medida. Eles ficam apagados quando a luva está
medindo e acesos quando os motores estão vibrando.

Os nomes "nervo frente" e "nervo trás" vêm da montagem de vocês, e indicam duas
posições na região do dedo médio e anelar.

## O gráfico de baixo

Mostra a força do tremor nos últimos quatro segundos, com o tempo correndo da
esquerda para a direita. A linha branca horizontal marca o mínimo de 0,08 g, e
a faixa azul de fundo marca os trechos em que os motores estiveram ligados.

Um detalhe que pode chamar atenção de quem olhar de perto: a linha tem cara de
escadinha em vez de curva suave. Isso não é defeito. A análise só é refeita a
cada trinta e dois centésimos de segundo, então o valor fica constante entre uma
análise e a seguinte. A escadinha é literalmente o ritmo do pensamento da luva
aparecendo no desenho.

## O desenho da mão

A mão gira conforme o roll e o pitch, e treme de leve quando o tremor passa do
mínimo. Ela também muda de cor: quanto mais forte o tremor, mais puxada para o
vermelho. Quando os motores estão ligados, ela fica azulada.

As três linhas coloridas saindo da mão são os eixos do sensor, nas mesmas cores
das barras do painel: vermelho para X, verde para Y e azul para Z. Servem para
mostrar em que direção o sensor está apontando.

---

# PARTE 2: AS IDEIAS POR TRÁS

Esta parte explica os conceitos que o código usa. Nenhuma fórmula é necessária
para entender.

## O que o acelerômetro mede de verdade

Um acelerômetro não mede movimento, mede aceleração, que é a mudança de
velocidade. Se a mão se move em velocidade perfeitamente constante, o sensor não
percebe nada. É por isso que ele enxerga tremor tão bem: tremor é justamente
mudança de direção o tempo todo, e mudar de direção é acelerar.

Tem um detalhe que confunde bastante e vale você saber. O acelerômetro sempre
mede a gravidade, mesmo parado em cima da mesa. Um sensor em repouso marca um g
apontando para cima. Por isso, quando a mão gira, essa gravidade se
redistribui entre os eixos, e o sensor vê uma variação grande sem que exista
movimento nenhum de tremor. Foi exatamente isso que atrapalhou os testes no
começo, como contado na seção da pureza.

## Frequência, e por que quatro a seis importa

Frequência é quantas vezes uma coisa se repete por segundo, e a unidade se chama
hertz. Cinco hertz são cinco idas e voltas por segundo.

O tremor de repouso do Parkinson fica caracteristicamente entre quatro e seis
hertz. Movimento voluntário do dia a dia fica em geral abaixo de três hertz,
porque o corpo humano não consegue fazer movimentos amplos e controlados mais
rápido que isso. Essa separação natural é o que torna a detecção possível.

Por isso o código procura tremor na faixa de três e meia a sete hertz, com uma
margem para os lados, e usa a faixa de meia a três hertz como referência do que
é movimento voluntário.

## Como se descobre a frequência de um movimento

Aqui está o coração do projeto. O sensor entrega só uma lista de números, uma
aceleração atrás da outra. Como se descobre, olhando essa lista, se tem uma
oscilação de cinco hertz escondida ali dentro?

A resposta se chama análise de frequência, e o algoritmo que o código usa se
chama Goertzel. A ideia dele é simples de explicar por analogia: é como
perguntar à lista de números "o quanto você se parece com uma oscilação de cinco
hertz?". Se a resposta for "muito", é porque tem cinco hertz ali. O programa faz
essa pergunta várias vezes, para várias frequências diferentes, e monta um mapa
de quanta energia existe em cada uma.

O código pergunta em vinte e seis frequências no total, de um quarto em um
quarto de hertz: onze perguntas na faixa lenta e quinze na faixa do tremor.

## Janela, e por que dois segundos e meio

Para responder aquela pergunta, o algoritmo precisa olhar um pedaço da lista, não
um número só. Esse pedaço se chama janela, e a do projeto tem cento e vinte e
oito amostras, o que a cinquenta amostras por segundo dá dois segundos e meio.

Existe um compromisso aqui que vale entender. Janela maior distingue melhor
frequências parecidas, mas demora mais para responder. Janela menor responde
rápido, mas confunde frequências vizinhas. Dois segundos e meio foi o meio termo
escolhido, e dá uma precisão de quatro décimos de hertz.

Para não ter que esperar dois segundos e meio a cada resposta, o programa usa
uma janela deslizante: ele guarda sempre as últimas cento e vinte e oito
amostras e refaz a conta a cada dezesseis amostras novas, ou seja, três vezes
por segundo. A janela anda continuamente, como uma esteira.

## Por que os motores atrapalham o sensor

O sensor está na mesma luva que os motores. Quando eles vibram, o sensor sente
essa vibração e não tem como distinguir dela e do tremor da pessoa.

Isso causava um problema sério que descobrimos em simulação. A luva ligava os
motores, sentia a vibração dos motores, achava que era tremor, e mantinha os
motores ligados para sempre. Medindo em simulação, depois de o tremor da mão já
ter cessado, os motores continuavam ligados setenta e quatro por cento do tempo.

A solução foi separar as coisas no tempo, e é o que se chama ciclo de medir e
tratar. A luva mede com os motores parados, decide, liga os motores por cinco
segundos sem medir nada, espera um quarto de segundo o motor parar de girar de
verdade, joga fora a janela inteira e recomeça a medir do zero. Depois dessa
mudança, o mesmo teste deu zero por cento.

## Aliasing, o problema mais sutil de todos

Este aqui é o mais difícil de explicar, mas dá uma resposta impressionante se
alguém perguntar sobre limitações.

O sensor é lido cinquenta vezes por segundo. Existe uma regra na área que diz
que, amostrando cinquenta vezes por segundo, só é possível enxergar
corretamente frequências até vinte e cinco hertz, que é a metade. Frequências
acima disso não somem: elas aparecem disfarçadas de outra frequência, mais
baixa. Esse disfarce se chama aliasing.

O efeito é o mesmo daquela ilusão de roda de carroça em filme antigo, quando a
roda parece girar ao contrário. A câmera fotografa poucas vezes por segundo e a
roda gira rápido demais, então o olho vê um movimento que não existe.

O problema concreto é que um motor de vibração gira entre cem e duzentos e
cinquenta hertz. Sem proteção, um motor girando a cento e cinquenta e quatro
hertz apareceria, para a luva, como uma oscilação limpa de quatro hertz. Bem no
meio da faixa do tremor.

A proteção é uma linha só de código, que liga um filtro dentro do próprio
sensor. Esse filtro corta tudo acima de vinte hertz antes que o número chegue ao
programa. Como toda a faixa de interesse está abaixo de sete hertz, não se perde
nada de útil e o disfarce deixa de acontecer.

## Vazamento espectral e a janela de Hann

Quando o programa recorta dois segundos e meio de um movimento contínuo, ele
está cortando o sinal no meio. Esse corte abrupto cria, na análise, uma energia
falsa que se espalha por frequências vizinhas. Isso se chama vazamento
espectral, e o efeito prático seria um movimento lento e forte vazando energia
para dentro da faixa do tremor e causando falso positivo.

A solução é multiplicar o pedaço recortado por uma curva suave, que vale zero no
começo, cresce até o meio e volta a zero no fim. Assim o sinal não é cortado
abruptamente, ele entra e sai suave. Essa curva se chama janela de Hann, e no
código ela é calculada uma vez só, quando a luva liga, e reaproveitada sempre.

---

# PARTE 3: O CÓDIGO, DO COMEÇO AO FIM

Agora vamos passar pelo arquivo `sketch_tremor_FINAL222.ino` na ordem em que ele
está escrito. Cada seção explica um conceito de C++ novo quando ele aparece, e
depois explica o que aquele trecho faz e por que ele está ali.

## Antes de tudo: o que é esse programa

O ESP32 é um computador minúsculo, do tamanho de meio dedo. Ele não tem tela nem
teclado, e a única coisa que sabe fazer é executar instruções, uma atrás da
outra, muito rápido.

Essas instruções são escritas em uma linguagem chamada C++, que é um texto que
uma pessoa consegue ler. O ESP32 não entende esse texto: ele entende só números.
Então existe um programa intermediário, chamado compilador, que traduz o texto
em números e envia para dentro da placa. É isso que acontece quando você clica
na seta do Arduino IDE.

Depois de traduzido, o programa fica gravado na placa e roda sozinho toda vez
que ela liga, mesmo sem computador nenhum conectado.

## A estrutura obrigatória: setup e loop

Todo programa de Arduino tem duas partes obrigatórias.

A primeira se chama `setup`, e roda uma vez só, quando a placa liga. É onde se
prepara tudo: configurar o sensor, preparar os pinos, calibrar.

A segunda se chama `loop`, e roda em repetição infinita depois disso. Termina e
começa de novo, milhares de vezes por segundo, até a placa desligar. É onde
acontece o trabalho de verdade.

Uma comparação que funciona bem: o `setup` é arrumar a cozinha antes de começar,
e o `loop` é cozinhar, repetindo o ciclo de olhar a panela e mexer.

## As três primeiras linhas: bibliotecas

```cpp
#include <Wire.h>
#include <MPU6050.h>
#include <esp_task_wdt.h>
```

`#include` significa "traga para cá o código de outra pessoa". Uma biblioteca é
um conjunto de funções prontas que alguém já escreveu e testou, para você não
precisar escrever do zero.

`Wire` cuida da conversa elétrica entre o ESP32 e o sensor, num protocolo
chamado I2C, que usa dois fios. Sem ela seria preciso controlar a tensão de cada
fio na mão, e isso são centenas de linhas.

`MPU6050` sabe conversar especificamente com o modelo de sensor que vocês usam.
Ela traduz coisas como "me dê a aceleração" para a sequência exata de bytes que
aquele chip espera.

`esp_task_wdt` dá acesso ao watchdog, que é um mecanismo de segurança explicado
mais adiante.

## Constantes: os pinos

```cpp
const int LED_POLEGAR   = 27;
const int MOTOR_POLEGAR = 4;
```

Aqui aparecem três conceitos de uma vez.

O primeiro é **variável**, que é um nome para guardar um valor. Em vez de
escrever o número vinte e sete espalhado por todo o programa, você dá um nome a
ele e usa o nome. Se um dia a fiação mudar, você troca em um lugar só.

O segundo é **tipo**. Em C++, toda variável precisa dizer que espécie de valor
ela guarda. `int` guarda número inteiro, sem casa decimal. Existem outros tipos
que vão aparecer: `float` guarda número com casa decimal, `bool` guarda apenas
verdadeiro ou falso, e `unsigned long` guarda número inteiro grande e que nunca
é negativo, usado para contar tempo.

O terceiro é `const`, que significa constante. É uma promessa de que aquele
valor nunca vai mudar durante a execução. Isso serve para o compilador avisar se
você tentar mudar sem querer, e para quem lê o código saber, de cara, que aquilo
é uma configuração e não algo que varia.

Sobre os números escolhidos, vale saber uma coisa que pode virar pergunta. Os
motores estão nos pinos 4, 18, 23, 13 e 19, e isso não é aleatório. O ESP32 tem
alguns pinos especiais, chamados de strapping, que são lidos no instante em que
a placa liga para decidir como ela vai iniciar. Se um motor estiver ligado em um
desses pinos e puxar a tensão no momento errado, a placa simplesmente não liga.
Os pinos de strapping são o 0, 2, 5, 12 e 15, e todos os motores foram
deliberadamente colocados fora deles.

## Constantes: as configurações da medição

```cpp
const int   BUFFER_SIZE = 128;
const float SAMPLE_RATE = 50.0;
const int   STEP        = 16;
```

`BUFFER_SIZE` é o tamanho da janela em número de amostras. `SAMPLE_RATE` é
quantas amostras por segundo. Cento e vinte e oito dividido por cinquenta dá dois
segundos e meio, que é a janela.

`STEP` é de quantas em quantas amostras a análise é refeita. Dezesseis amostras
a cinquenta por segundo dão trinta e dois centésimos de segundo, ou seja, a
análise roda cerca de três vezes por segundo.

Repare que `SAMPLE_RATE` é `float` e não `int`, porque ele é usado em contas com
divisão, e usar inteiro faria a conta arredondar e perder precisão.

## Constantes: os três critérios

```cpp
const float TREMOR_MIN_G   = 0.08;
const float DOMINANCE_MIN  = 0.50;
const float NITIDEZ_MIN    = 3.20;
```

São os três limites explicados na Parte 1. Estão juntos, no topo do arquivo,
dentro de um comentário grande que explica o que cada um protege e o que
acontece ao mexer.

Isso é uma decisão de organização que vale mencionar se perguntarem sobre a
qualidade do código: os valores que uma pessoa pode querer ajustar ficam todos
em um lugar visível, e não escondidos no meio das contas.

Aliás, vale saber a história do 3,20. Ele começou em 3,50, que foi o valor tirado
da simulação. Ao testar na luva de verdade, um tremor legítimo mediu 3,30 e
ficava barrado por dois centésimos. O motivo é que mão humana não é uma
oscilação matematicamente perfeita: a amplitude e a velocidade variam ao longo
dos dois segundos e meio, e isso espalha um pouco a energia. O valor foi
corrigido com base na medida real.

## Comentários

Todo texto depois de `//` é comentário. O compilador ignora completamente. Serve
só para quem lê.

Neste projeto os comentários explicam sobretudo o **porquê**, não o **o quê**. O
que o código faz dá para ler no próprio código. O que não dá para adivinhar é
por que alguém escolheu aquele caminho, e é isso que está escrito ali. Vários
comentários explicam bugs que já aconteceram, justamente para ninguém desfazer a
correção sem saber.

## Vetores

```cpp
float bufferX[BUFFER_SIZE];
```

Um vetor, também chamado de array, é uma variável que guarda vários valores em
vez de um só, todos do mesmo tipo, numerados a partir do zero.

Essa linha cria espaço para cento e vinte e oito números decimais. `bufferX[0]`
é o primeiro, `bufferX[127]` é o último. É neles que ficam guardadas as
acelerações medidas na direção X.

Existem três buffers, um por direção, mais dois auxiliares usados durante a
análise.

## Buffer circular

Este conceito não é palavra-chave de C++, é uma ideia de organização, e é uma
das coisas mais elegantes do projeto.

O programa precisa lembrar sempre das últimas cento e vinte e oito amostras. A
maneira ingênua seria, a cada amostra nova, empurrar todas as outras uma casa
para trás, como uma fila. Isso custaria cento e vinte e oito movimentações
cinquenta vezes por segundo.

A maneira usada é outra: as amostras são gravadas em círculo. Existe um marcador
chamado `bufferIndex` que aponta onde gravar a próxima. Quando ele chega ao fim,
volta ao começo e passa por cima do valor mais antigo, que é exatamente o que se
queria descartar.

O truque para fazer o marcador voltar sozinho é o operador `%`, que dá o resto
da divisão:

```cpp
bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
```

Quando `bufferIndex` vale 127, somar um dá 128, e o resto de 128 dividido por
128 é zero. O marcador volta ao início sem nenhum `if`.

## Enum e a máquina de estados

```cpp
enum Estado { MEDINDO, TRATANDO, ASSENTANDO };
Estado estado = MEDINDO;
```

`enum` cria um tipo novo que só aceita uma lista fechada de valores. Aqui a luva
só pode estar em uma de três situações, e o compilador impede qualquer outra.

Poderia ter sido feito com números, zero, um e dois, mas aí ninguém lembraria o
que é o dois. Com nomes, o código fica legível sem consulta.

As três situações são as mesmas que aparecem na tela. MEDINDO é motores parados
e sensor confiável. TRATANDO é motores vibrando e sensor ignorado. ASSENTANDO é
o quarto de segundo de espera para o motor parar de girar de verdade antes de
voltar a confiar no sensor.

Uma estrutura assim, em que o programa está sempre em exatamente um estado e as
mudanças seguem regras claras, se chama máquina de estados. É um jeito de evitar
que o programa fique em situações contraditórias, como achar que está medindo e
ao mesmo tempo estar vibrando.

## Funções

```cpp
void configuraMPU() {
  mpu.initialize();
  mpu.setDLPFMode(MPU6050_DLPF_BW_20);
}
```

Uma função é um pedaço de programa com nome, que você escreve uma vez e usa
quantas vezes quiser. Serve para não repetir código e para dar nome a uma ideia.

A palavra na frente do nome diz o que a função devolve. `void` significa que ela
não devolve nada, só faz. Se estivesse escrito `float`, ela devolveria um número
decimal.

Os parênteses depois do nome guardam os parâmetros, que são as informações que a
função recebe para trabalhar. Aqui está vazio, então ela não recebe nada.

Esta função em particular existe por um motivo prático. Ela é usada em dois
lugares, quando a luva liga e quando o sensor precisa ser reiniciado depois de
uma falha de comunicação. Antes de ela existir, a configuração estava escrita
duas vezes, e a segunda esquecia de ligar o filtro anti-aliasing. Juntar tudo em
uma função só resolveu o esquecimento de vez.

## Repetição com for

```cpp
for (int i = 0; i < 5; i++) {
  ledcWrite(PINOS[MOTOR_IDX[i]], duty);
}
```

`for` repete um bloco várias vezes. Ele tem três partes dentro dos parênteses,
separadas por ponto e vírgula.

A primeira, `int i = 0`, cria um contador que começa em zero. A segunda,
`i < 5`, é a condição para continuar: enquanto o contador for menor que cinco,
repete. A terceira, `i++`, soma um ao contador no fim de cada volta.

O resultado é que o bloco roda cinco vezes, com `i` valendo zero, um, dois, três
e quatro. E como vetores são numerados a partir do zero, o contador serve
diretamente como índice.

Esse `for` específico é o que liga os cinco motores. Sem ele, seriam cinco
linhas quase idênticas, e um erro de digitação em uma delas seria difícil de
achar.

## Condição com if

```cpp
if (motorDuty[i] != duty) {
  ledcWrite(PINOS[MOTOR_IDX[i]], duty);
  motorDuty[i] = duty;
}
```

`if` executa o bloco só se a condição for verdadeira. `!=` significa diferente
de.

Este trecho tem uma sutileza que vale explicar, porque mostra cuidado com
eficiência. O `loop` roda milhares de vezes por segundo, e escrever no motor a
cada volta seria desperdício. Então o programa guarda, em `motorDuty`, qual foi o
último valor enviado a cada motor, e só escreve de novo se o valor mudou.

É a diferença entre gritar a mesma ordem mil vezes por segundo e falar uma vez e
confiar que foi entendido.

## Ponteiros

Este é o conceito de C++ que costuma assustar, mas a ideia é simples e no
projeto ele aparece com um propósito muito claro.

```cpp
float analisaEixo(const float* src, int start, float* picoVoluntario,
                  float* mediaTremor) {
```

Um ponteiro, marcado pelo asterisco, é um endereço em vez de um valor. Em vez de
dizer "aqui está o número", ele diz "o número está guardado ali".

Ele serve para duas coisas aqui.

A primeira é evitar cópia. `const float* src` recebe o endereço do buffer em vez
de copiar as cento e vinte e oito casas. A palavra `const` junto é uma promessa
de que a função só vai ler aquele buffer, nunca alterar.

A segunda é devolver mais de um resultado. Uma função em C++ só consegue
devolver um valor pelo `return`. Mas `analisaEixo` precisa entregar três coisas:
o pico da faixa do tremor, o pico da faixa lenta e a média da faixa do tremor.
Então ela devolve o primeiro pelo `return`, e escreve os outros dois nos
endereços que recebeu. É como dar a alguém um envelope endereçado para a resposta
chegar de volta.

## O algoritmo de Goertzel, no código

```cpp
float goertzelBand(const float* buf, float freq) {
  float omega = 2.0 * PI * freq / SAMPLE_RATE;
  float coeff = 2.0 * cos(omega);
  float s_prev = 0, s_prev2 = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    float s = buf[i] + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev  = s;
  }
  ...
}
```

Você não precisa saber deduzir isso para apresentar. O que vale saber é o que
está acontecendo e por que este algoritmo foi escolhido.

O que acontece: as duas primeiras linhas convertem a frequência procurada em um
número que representa o quanto girar a cada passo. Depois o `for` percorre as
cento e vinte e oito amostras uma única vez, carregando duas memórias
(`s_prev` e `s_prev2`) que vão acumulando o resultado. Ao fim, essas duas
memórias juntas dizem quanta energia existe naquela frequência.

Por que ele foi escolhido: existe um algoritmo muito mais famoso para isso,
chamado FFT, que descobre todas as frequências de uma vez. Mas o projeto não
precisa de todas: precisa de vinte e seis específicas. O Goertzel é muito mais
leve quando você quer poucas frequências, e cabe folgado num microcontrolador.

Esta é uma boa resposta para "por que vocês não usaram FFT?".

## Divisão para dar significado físico

```cpp
return sqrt(fabs(power)) / (BUFFER_SIZE / 4.0);
```

O número que o Goertzel produz depende do tamanho da janela e da janela de Hann.
Sem corrigir isso, o valor seria arbitrário e não significaria nada no mundo
real.

Essa divisão converte o resultado para amplitude em g. É por isso que a tela
consegue mostrar 0,673 g e esse número ter significado físico de verdade, e não
ser apenas um número relativo. Isso importa para o projeto, porque permite dizer
coisas como "o limite de 0,08 g equivale a um tremor de oito décimos de
milímetro".

## A decisão final

```cpp
return (tTotal        >= TREMOR_MIN_G
     && ultDominancia >= DOMINANCE_MIN
     && ultNitidez    >= NITIDEZ_MIN);
```

`&&` significa E. Os três precisam ser verdadeiros ao mesmo tempo, senão a
função devolve falso.

Repare que a função devolve `bool`, que só pode ser verdadeiro ou falso. Toda a
complexidade da análise termina em uma resposta de sim ou não, que é o que o
resto do programa precisa saber.

Um detalhe de projeto que vale mencionar: a decisão usa a força combinada dos
três eixos, não cada eixo separado. Antes ela testava eixo por eixo, e um tremor
de sete centésimos em cada direção era descartado três vezes, mesmo tendo doze
centésimos de força real. Somando primeiro e decidindo depois, isso não acontece.

## O setup, na ordem

O `setup` faz sete coisas, nesta ordem, e a ordem importa em quase todas.

Primeiro ele abre a comunicação serial, para poder falar com o computador. O
tamanho do buffer de saída é configurado antes de abrir, porque configurar
depois não tem efeito nenhum.

Segundo, ele força todos os dez pinos em nível baixo. Isso existe porque, no
instante entre o reset e o programa começar, os pinos ficam em estado
indefinido, e a base do transistor pode flutuar e ligar o motor sozinho. Um
resistor de pull-down no hardware fecharia essa janela por completo, e o
comentário no código diz isso.

Terceiro, ele configura o sensor, incluindo o filtro de vinte hertz que evita o
aliasing.

Quarto, ele prepara os pinos para gerar PWM, que é a técnica de controlar
intensidade ligando e desligando muito rápido. Quanto maior a fração de tempo
ligado, mais forte fica. Os LEDs usam cinco mil vezes por segundo e os motores
vinte mil, para a vibração não produzir um chiado audível.

Quinto, ele testa se o sensor responde. Se não responder, o LED do mindinho
pisca para sempre. Isso é melhor que travar em silêncio, porque dá um
diagnóstico visível em campo, sem precisar de computador. Esse teste vem antes
do teste dos motores de propósito, para você descobrir o problema em um segundo e
não depois de quinze.

Sexto, ele liga cada LED e cada motor um de cada vez, por um segundo e dois
décimos, dizendo no monitor serial qual está sendo testado. Isso separa problema
de fiação de um canal específico de problema de alimentação compartilhada.

Sétimo, ele calibra, e depois arma o watchdog. O watchdog é um contador que
reinicia a placa se o programa parar de dar sinal de vida por quatro segundos. A
razão é de segurança física: se o programa travasse com os motores ligados, eles
ficariam vibrando contra o dedo indefinidamente. O watchdog é armado só no fim
porque as esperas longas do teste e da calibração o fariam disparar sem motivo.

## A calibração

Calibrar é descobrir o erro de fábrica do sensor. Nenhum sensor marca exatamente
zero quando está parado, e essa diferença é constante para cada peça.

A função lê quinhentas amostras com a luva parada, tira a média e guarda o
resultado. Depois, toda leitura desconta esse valor.

Ela também verifica se a mão ficou realmente parada, olhando a maior variação
que apareceu durante a leitura. Se passar de cinco centésimos de g, ela avisa e
tenta de novo, até três vezes.

Um detalhe que vale saber e que só apareceu quando começamos a pensar no usuário
real: uma pessoa com tremor de repouso não consegue ficar parada, porque é
justamente o sintoma. A calibração vai reclamar as três vezes. O efeito prático
é pequeno, porque a análise remove a média de qualquer jeito e a detecção não é
afetada, mas o jeito certo de fazer é calibrar com a luva em cima da mesa, antes
de vestir.

## O loop, e a ordem das coisas dentro dele

O `loop` faz três blocos de trabalho a cada volta.

O primeiro é a máquina de estados, que roda em toda volta, milhares de vezes por
segundo. Isso é de propósito: assim o fim dos cinco segundos de vibração cai no
instante exato, sem depender do ritmo de leitura do sensor.

O segundo é a leitura, que só acontece a cada vinte milésimos de segundo. O
código descobre isso comparando o relógio interno com a hora da última leitura.

Aqui tem um detalhe pequeno com consequência grande. Ao marcar a hora da
próxima leitura, o programa soma vinte ao horário anterior em vez de anotar a
hora atual. Se anotasse a hora atual, o atraso de cada volta se acumularia, a
taxa real cairia para uns quarenta e oito por segundo e, como o valor cinquenta
está fixo nas contas do Goertzel, todas as frequências sairiam deslocadas. Um
erro de meio milésimo por volta viraria erro de medição.

O terceiro bloco é enviar a telemetria pelo cabo, e ele só envia se houver
espaço livre suficiente na fila de saída. Sem essa verificação, o programa
ficaria parado esperando o cabo, e essa espera atrasaria a leitura do sensor.

## A detecção de falha de comunicação

```cpp
bool allZero = (ax == 0 && ay == 0 && ...);
bool allFF   = (ax == -1 && ay == -1 && ...);
```

Se o fio do sensor soltar, a leitura não dá erro: ela devolve lixo. E o lixo tem
dois formatos típicos. Ou vem tudo zero, o que é fisicamente impossível porque o
eixo vertical sempre sente a gravidade. Ou vem tudo com o valor menos um, que é
o que o barramento devolve quando ninguém responde.

Detectando os dois casos, o programa reinicia a comunicação, desliga tudo, joga
fora a janela e recomeça. Sem isso, um cabo mal encaixado viraria uma leitura de
menos seis centésimos de milésimo de g e passaria como se fosse válida.

---

# PARTE 4: A COMUNICAÇÃO ENTRE A LUVA E O COMPUTADOR

A luva manda uma linha de texto por amostra, cinquenta por segundo, com nove
números separados por vírgula. Ela manda e não espera resposta, como quem fala
sem exigir que alguém escute.

Os nove campos, na ordem, são: inclinação lateral, inclinação para frente,
tremor na direção X, tremor na direção Y, tremor na direção Z, força combinada,
estado atual, pureza e nitidez.

O estado é o campo que evita o mal-entendido de mostrar valor velho como se
fosse novo. Ele vale zero quando a leitura é ao vivo, um quando os motores estão
ligados e dois quando a janela está enchendo.

Qualquer linha com menos de nove campos é descartada pelo Processing. É assim
que as mensagens de texto do momento em que a luva liga são ignoradas sem
atrapalhar.

---

# PARTE 5: PERGUNTAS QUE PODEM APARECER

**Por que cinquenta leituras por segundo, e não mais?**
Porque para enxergar até sete hertz com folga, cinquenta é suficiente, e mais
que isso gastaria processamento e energia sem ganho. O limite teórico com
cinquenta leituras é enxergar até vinte e cinco hertz, bem acima do que
precisamos.

**Por que não dá para saber qual dedo está tremendo?**
Porque existe um sensor só, nas costas da mão. Ele mede o movimento da mão
inteira. Detectar dedo por dedo exigiria um sensor por dedo.

**A luva cura ou trata o Parkinson?**
Não. A hipótese do trabalho é que a vibração ajuda a mascarar o sinal do tremor
enquanto está ligada. Nada disso cura a doença, e o projeto não mediu redução do
tremor. O que foi demonstrado é a detecção confiável e a resposta automática.

**Como vocês sabem que atenua o tremor?**
Esta é a pergunta mais difícil, e a resposta honesta é que não sabemos ainda. O
sistema detecta e responde, e a atenuação é a hipótese que ainda precisa ser
medida, com alguém que tenha o sintoma. Responder isso com franqueza é muito
mais forte que inventar um resultado.

**Por que os motores demoram para desligar?**
Porque cada acionamento dura cinco segundos, e depois disso a luva ainda precisa
de dois segundos e meio para juntar amostras novas antes de decidir de novo. É o
preço de não deixar o sensor medir a própria vibração.

**Qual foi a maior dificuldade?**
Descobrir que digitar no teclado são cinco batidas por segundo, exatamente a
mesma frequência do tremor parkinsoniano, e que nenhuma medida de força ou de
faixa de frequência separava as duas coisas. A solução foi perceber que tremor é
oscilação e digitar são impactos, e que impacto espalha energia enquanto
oscilação concentra.

**Quanto custa?**
Um ESP32, um sensor MPU6050, cinco motores de vibração, cinco transistores,
cinco LEDs com resistores, jumpers e uma luva de tecido. Tudo material comum de
eletrônica básica, que é justamente a proposta do trabalho.

---

# ROTEIRO SUGERIDO DE DEMONSTRAÇÃO

Comece mostrando a mão parada e o painel, com os três critérios apagados e o
gráfico rente ao chão. Explique que a luva está medindo agora, ao vivo, e que
nada está ligado.

Depois acene devagar com a mão, com bastante amplitude. A força vai subir e a
pureza não. Aponte para a tela e explique que o sistema está vendo movimento,
reconhecendo que não é tremor, e recusando ligar. Isso é bem mais convincente
que só mostrar o caso que funciona.

Aí sim faça o tremor de verdade: antebraço apoiado, só a mão oscilando rápido e
curto. Os três critérios ficam verdes, o painel anuncia a detecção, os motores e
os LEDs ligam juntos e o gráfico ganha a faixa azul.

Enquanto os motores estiverem ligados, aponte que os números congelaram e que o
painel avisa isso. Explique que é de propósito, porque o sensor está na mesma
luva que os motores e não conseguiria distinguir uma coisa da outra.

Para fechar, pare de tremer e mostre que a luva termina o ciclo e volta a ficar
quieta sozinha, sem nunca se religar por conta própria.
