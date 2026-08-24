# ParkinSense

**Luva inteligente de baixo custo para controle de tremores e autonomia no
cotidiano.**

Detecta tremores involuntários da mão e responde com contra-estímulo vibratório
nos dedos.

Fagner da Silva Pereira Filho · Kauhan Rodrigues de Melo · Miguel Macedo do
Nascimento
Orientador: Pedro Ramalho Neto
Unidade de Tecnologia, Educação e Cidadania Gregório Bezerra — Recife/PE
Mostra Nacional de Robótica 2026 · Ensino Médio

> **Protótipo acadêmico.** Não é dispositivo médico, não passou por validação
> clínica e **nunca foi testado em alguém com Parkinson**. O comportamento
> descrito aqui foi verificado com sinais sintéticos e simulação.

---

## A ideia

A Doença de Parkinson afeta mais de 8,5 milhões de pessoas no mundo. Um dos
sintomas mais característicos é o tremor involuntário das mãos, que dificulta
tarefas simples do dia a dia — escrever, alimentar-se, segurar objetos.

A hipótese do projeto é a de **contra-estímulo**: o cérebro envia sinais
nervosos descoordenados para os dedos, e é isso que faz a mão tremer. A pele,
principalmente nas pontas dos dedos, é cheia de mecanorreceptores com uma
projeção grande para o córtex. Uma vibração forte entra por esse caminho e
**ocupa o canal sensorial**, disputando espaço com o sinal do tremor.

A luva detecta o tremor pelo acelerômetro e, quando ele aparece, aciona os
cinco motores por alguns segundos.

**O que o projeto demonstra hoje:** que é possível detectar tremor de forma
confiável com componentes baratos, distinguindo-o de movimento voluntário, e
responder automaticamente. **A atenuação do tremor em si não foi medida** — é
a hipótese, não um resultado.

---

## Hardware

| Componente | Qtd. | Observação |
|---|---|---|
| ESP32 (DevKit) | 1 | processamento e cálculo |
| MPU6050 | 1 | acelerômetro + giroscópio, I2C |
| Motor vibracall 1027 3V | 5 | um por canal |
| Transistor | 5 | um por motor — o GPIO não aciona o motor direto |
| LED + resistor | 5 | indicador por canal |
| Protoboard e jumpers | — | conexões |

O sensor fica no **dorso da mão**, região de maior amplitude de movimento. Os
motores ficam distribuídos ao longo dos dedos e da palma. A bateria vai num
bolso costurado ao pulso.

### Mapeamento dos pinos

| Canal | Posição | LED | Motor |
|---|---|---|---|
| 0 | Polegar | 27 | 4 |
| 1 | Indicador | 26 | 18 |
| 2 | Nervo frente | 25 | 23 |
| 3 | Nervo trás | 33 | 13 |
| 4 | Mindinho | 32 | 19 |

MPU6050 no I2C padrão: **SDA 21, SCL 22**.

Os motores ficam de propósito fora dos pinos de *strapping* do ESP32
(GPIO 0, 2, 5, 12 e 15) — se o driver forçar nível errado num desses durante o
reset, a placa não dá boot.

> **Recomendação de montagem:** um resistor de *pull-down* na base de cada
> transistor. Entre o reset e o `setup()` os GPIOs ficam em entrada e a base
> flutua, o que pode ligar o motor. O firmware força nível baixo assim que
> começa a rodar, mas essa janela inicial só o hardware fecha.

---

## Como o firmware detecta o tremor

Esta é a parte mais elaborada do sistema, e a que resolveu a principal
limitação apontada nos primeiros testes: **falsos positivos**.

O acelerômetro é lido a **50 Hz**. As amostras entram num buffer circular de
**128 posições** — uma janela de **2,56 s**, com resolução de 0,39 Hz. A cada
16 amostras (~0,32 s) a janela é reanalisada.

O sinal é preparado removendo a média (mata o componente da gravidade) e
aplicando uma **janela de Hann**, que impede que um movimento lento vaze
energia para a faixa do tremor. Depois, um **algoritmo de Goertzel** mede a
amplitude em frequências específicas:

| Banda | Faixa | Sondas |
|---|---|---|
| Movimento voluntário | 0,50 – 3,00 Hz | 11 (passo 0,25 Hz) |
| Tremor parkinsoniano | 3,50 – 7,00 Hz | 15 (passo 0,25 Hz) |

O resultado sai em **g**, com significado físico direto.

### Os três critérios

Movimento comum do dia a dia engana um detector ingênuo de várias formas
diferentes. Cada critério abaixo barra um tipo de engano, e **os três precisam
passar ao mesmo tempo** para acionar os motores.

**1. Força — `|T| ≥ 0,08 g`**
Equivale a um tremor de ~0,8 mm a 5 Hz. Barra movimento voluntário lento:
acenar a 1,5 Hz, mesmo com 2 g de amplitude, deixa só 0,08 g na faixa de
tremor.

**2. Pureza — `≥ 0,50`**
Fração da energia que está em 3,5–7 Hz, e não em 0,5–3 Hz. Barra gesto rápido
perto da faixa de tremor.

**3. Nitidez — `≥ 3,50`**
Pico da faixa dividido pela média da faixa. Tremor é uma **oscilação
sustentada**: concentra a energia numa frequência só. **Impacto** — digitar,
passos, batida na mesa — espalha energia pela faixa inteira.

Este terceiro critério existe por um motivo bem concreto: **digitar são ~5
batidas por segundo, ou seja 5 Hz — bem no meio da faixa de tremor.** Sem ele,
digitar no teclado aciona a luva. Medido em simulação: tremor sustentado de 4 a
6 Hz dá nitidez 4,6 de forma estável; digitar com o jitter natural de quem
digita dá 2,2; batidas na mesa dão 2,4.

Os três limiares ficam num bloco comentado no topo do `.ino`, com a explicação
de para que serve cada um e o que acontece ao mexer. Os três valores medidos
são publicados na serial, e o painel mostra qual deles está barrando.

---

## Como o firmware responde

Detectado o tremor, **os cinco motores vibram juntos** por 5 segundos
(`BLOCO_TERAPIA_MS`), com intensidade `CONTRA_DUTY` (200 de 255, ~78%). Cada
LED acende junto com o motor do seu dedo.

### O ciclo medir / tratar

Este é o ponto menos óbvio do projeto, e o mais importante.

**O acelerômetro está na mesma luva que os motores.** Se ele continuasse
medindo durante o contra-estímulo, mediria a própria vibração — e a luva se
auto-detectaria, mantendo os motores ligados para sempre. Por isso existe uma
máquina de estados:

```
MEDINDO  ──detectou tremor──▶  TRATANDO  ──5 s──▶  ASSENTANDO  ──250 ms──┐
   ▲                                                                      │
   └──────────────────────────────────────────────────────────────────────┘
     (a janela é descartada e reenchida do zero: 2,56 s)
```

- **MEDINDO** — motores parados, buffer sendo alimentado, análise rodando
- **TRATANDO** — motores vibrando; o buffer **não** recebe amostras
- **ASSENTANDO** — motores desligados, esperando o motor parar de girar

Além disso, o filtro passa-baixa interno do MPU6050 é configurado em **20 Hz**.
Sem ele o sensor responderia até 260 Hz, e como amostramos a 50 Hz (Nyquist =
25 Hz), a vibração do motor (100–250 Hz) dobraria por *aliasing* para dentro da
faixa 3,5–7 Hz e seria lida como tremor.

> Medido em simulação: sem essas duas proteções, com o tremor já cessado, os
> motores continuavam ligados **74% do tempo**. Com elas, 0%.

---

## Protocolo serial

115200 baud, uma linha por amostra (50 Hz), 9 campos separados por vírgula:

```
roll,pitch,tX,tY,tZ,tTotal,estado,dominancia,nitidez
```

| Campo | Unidade | Descrição |
|---|---|---|
| `roll`, `pitch` | graus | inclinação da mão — só telemetria |
| `tX`, `tY`, `tZ` | g | amplitude do tremor em cada **eixo do acelerômetro** |
| `tTotal` | g | módulo dos três eixos — a "força" (1º critério) |
| `estado` | 0/1/2 | 0 = medindo (ao vivo), 1 = tratando, 2 = janela enchendo |
| `dominancia` | 0–1 | a "pureza" (2º critério) |
| `nitidez` | ≥0 | pico ÷ média da faixa (3º critério) |

Três detalhes que confundem quem lê o formato pela primeira vez:

- **`tX`/`tY`/`tZ` são eixos, não dedos.** A luva tem um MPU só: mede a mão
  inteira e não tem como saber qual dedo está tremendo.
- **Força alta não significa acionamento.** Os três critérios precisam passar.
- **Estado 2 = valores antigos.** Depois de uma sessão o firmware descarta a
  janela e leva 2,56 s para enchê-la. Nesse intervalo os valores publicados
  ainda são os da análise anterior.

Linhas iniciadas por `#` são avisos do firmware. No boot ele também imprime
mensagens de diagnóstico que não seguem o formato CSV — qualquer consumidor
deve descartar linhas com menos de 9 campos.

Ligando `DIAGNOSTICO = true` no topo do `.ino`, a telemetria dá lugar a uma
linha por análise dizendo os valores medidos e qual critério barrou. Útil no
Monitor Serial (o visualizador não funciona nesse modo).

---

## Visualização

`visualizacao/visuLuvinha/` é um sketch em Processing que mostra a mão em 3D,
os três critérios com seus limiares, a amplitude por eixo, o histórico dos
últimos 4 s e o estado do ciclo.

A porta serial é detectada automaticamente (procura `ttyUSB`, `ttyACM`, `COM`).
Para forçar uma porta, mude `PORTA_IDX` no topo do arquivo.

> Se o Monitor Serial do Arduino IDE estiver aberto, a porta fica ocupada e o
> Processing não consegue abri-la. Feche um antes de rodar o outro.

---

## Como rodar

```bash
./abrir-projeto.sh
```

Abre o firmware no Arduino IDE 2 e a visualização no Processing de uma vez. Os
caminhos dos dois programas estão no topo do script.

**Dependências do firmware:** biblioteca `MPU6050` (Electronic Cats / jrowberg)
e o core `esp32` da Espressif. Placa: ESP32 Dev Module.

### No primeiro boot, o que esperar

1. **Teste do MPU.** Se o sensor não responder, o LED do mindinho pisca
   continuamente e nada mais roda.
2. **Teste sequencial (~15 s)** — liga cada LED e cada motor um de cada vez,
   imprimindo nome e pino no Serial. Isola problema de fiação por canal de
   problema de alimentação compartilhada.
3. **Calibração** — mantenha a luva **parada** apoiada numa superfície firme.
   Se detectar movimento (excursão acima de 0,05 g), avisa e repete até 3 vezes.
4. **Operação** — a partir daí, ciclos de medição e contra-estímulo.

### Consumo

Os cinco motores ligam ao mesmo tempo. Se a bateria não segurar e o ESP32
reiniciar no meio do uso, baixe `CONTRA_DUTY` no topo do `.ino`. **Teste com a
bateria real, não só no cabo USB.**

---

## Limitações conhecidas

**A atenuação não foi medida.** O sistema detecta e responde; que a vibração
efetivamente reduza o tremor é a hipótese do trabalho, não um resultado. Medir
isso exigiria comparar a amplitude do tremor com e sem estimulação, em alguém
que realmente tenha o sintoma.

**Um único acelerômetro.** A luva mede a mão como um todo. Detecção por dedo
exigiria um sensor por dedo.

**Movimento rítmico na faixa de tremor.** O critério de nitidez separa impacto
de oscilação, mas um movimento voluntário *sustentado e regular* entre 3,5 e
7 Hz é indistinguível de tremor para um acelerômetro. Não há como resolver isso
sem outro tipo de sinal.

**Latência.** Da parada do tremor até os motores soltarem passa-se o bloco em
curso (até 5 s) mais o reenchimento da janela (2,56 s).

**Sem validação clínica.** Os parâmetros vieram da literatura e de simulação.

---

## Outras abordagens

O [Coordinated Reset](ABORDAGEM-COORDINATED-RESET.md) é uma técnica diferente,
que em vez de mascarar o sinal tenta **dessincronizar** os neurônios que
disparam em bloco. Chegou a ser implementada e testada neste projeto, e está
documentada e preservada caso seja retomada no futuro.

---

## Referências

- ARDUINO. **Arduino IDE Software.** Ivrea: Arduino LLC, 2023.
  <https://www.arduino.cc/>
- ORGANIZAÇÃO MUNDIAL DA SAÚDE (OMS). **Parkinson disease.** Genebra: WHO, 2023.
  <https://www.who.int/news-room/fact-sheets/detail/parkinson-disease>
- PARKINSON'S FOUNDATION. **Tremor.** Miami: Parkinson's Foundation, 2025.
  <https://www.parkinson.org/understanding-parkinsons/movement-symptoms/tremor>

---

## Estrutura

```
sketch_tremor_FINAL222.ino          firmware do ESP32
visualizacao/visuLuvinha/           visualização em Processing
abrir-projeto.sh                    abre os dois de uma vez
ABORDAGEM-COORDINATED-RESET.md      abordagem alternativa (não adotada)
```
