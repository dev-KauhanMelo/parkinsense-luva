# ParkinSense

Luva experimental que detecta tremor parkinsoniano e responde com estimulação
vibrotátil no padrão **Coordinated Reset**.

> **Protótipo acadêmico.** Não é dispositivo médico, não passou por validação
> clínica e **nunca foi testado em alguém com Parkinson**. Todo o comportamento
> descrito aqui foi verificado com sinais sintéticos e simulação, não com
> pacientes.

---

## A ideia

No Parkinson, populações de neurônios que deveriam disparar de forma
independente passam a disparar **em sincronia**, num ritmo de 4 a 7 Hz. É essa
sincronia patológica que chega aos dedos e produz o tremor.

Colocando de forma bem direta: o cérebro manda uma enxurrada de sinais nervosos
descoordenados para os dedos, e é isso que faz a mão tremer. A sacada da luva é
que a pele — principalmente nas pontas dos dedos — é cheia de mecanorreceptores,
com uma projeção enorme para o córtex. Uma vibração forte e bem colocada entra
por esse caminho e ocupa o canal, "atrapalhando" o padrão sincronizado.

A técnica que formaliza isso é o **Coordinated Reset (CR)**, proposto por Peter
Tass. Ela não tenta bloquear o sinal: entrega estímulos curtos em pontos
diferentes e **em ordem sorteada**, de modo que cada sub-população receba um
"reset" de fase num instante distinto. O grupo se dessincroniza. Como a rede
aprende por plasticidade dependente de disparo, a ideia é que ela vá
*desaprendendo* o padrão sincronizado — daí os relatos de benefício que persiste
depois de desligar o aparelho.

**A ordem embaralhada é o mecanismo, não um enfeite.** Um padrão regular e
previsível reforçaria a sincronia em vez de quebrá-la. Se você olhar a luva
funcionando e achar que os motores estão pulsando de forma aleatória, é
exatamente isso que deveria estar acontecendo.

---

## Hardware

| Componente | Quantidade | Observação |
|---|---|---|
| ESP32 (DevKit) | 1 | |
| MPU6050 | 1 | acelerômetro + giroscópio, I2C |
| Motor de vibração ERM | 5 | um por canal |
| Transistor | 5 | um por motor, o GPIO não aciona o motor direto |
| LED + resistor | 5 | indicador por canal |

### Mapeamento dos pinos

| Canal | Posição | LED | Motor |
|---|---|---|---|
| 0 | Polegar | 27 | 4 |
| 1 | Indicador | 26 | 18 |
| 2 | Nervo frente | 25 | 23 |
| 3 | Nervo trás | 33 | 13 |
| 4 | Mindinho | 32 | 19 |

MPU6050 no I2C padrão: **SDA 21, SCL 22**.

Os motores ficam deliberadamente fora dos pinos de *strapping* do ESP32
(GPIO 0, 2, 5, 12 e 15). Se o driver forçar nível errado num desses pinos
durante o reset, a placa não dá boot.

> **Recomendação de montagem:** coloque um resistor de *pull-down* na base de
> cada transistor. Entre o reset e o `setup()` os GPIOs ficam em entrada e a
> base flutua, o que pode ligar o motor. O firmware força nível baixo assim que
> começa a rodar, mas essa janela inicial só o hardware fecha.

---

## Como funciona o firmware

### 1. Detecção

O acelerômetro é lido a **50 Hz**. As amostras entram num buffer circular de
**128 posições** — uma janela de **2,56 s**, o que dá resolução de 0,39 Hz.

A cada 16 amostras (~0,32 s) a janela é reanalisada. O sinal é preparado assim:
remove-se a média (mata o DC da gravidade) e aplica-se uma **janela de Hann**,
que impede que um movimento voluntário lento vaze energia para a faixa do
tremor. Depois, um **algoritmo de Goertzel** mede a amplitude em frequências
específicas:

| Banda | Faixa | Sondas |
|---|---|---|
| Movimento voluntário | 0,50 – 3,00 Hz | 11 (passo 0,25 Hz) |
| Tremor parkinsoniano | 3,50 – 7,00 Hz | 15 (passo 0,25 Hz) |

O resultado sai direto em **g**, com significado físico. Dispara a terapia
quando os dois critérios são satisfeitos ao mesmo tempo:

- **força** `|T| ≥ 0,08 g` — equivale a um tremor de ~0,8 mm a 5 Hz.
  É este critério que barra movimento voluntário lento.
- **pureza** (dominância) `≥ 0,50` — a faixa de tremor precisa ao menos empatar
  com a de movimento voluntário. É este que barra gesto rápido perto da faixa.
- **nitidez** `≥ 3,50` — o pico da faixa dividido pela média da faixa. Tremor é
  uma oscilação sustentada: toda a energia cai numa frequência só. Impacto
  (digitar, passos, batida na mesa) espalha energia pela faixa inteira.
  Medido em simulação: tremor sustentado de 4 a 6 Hz dá 4,6 de forma estável,
  digitar com jitter humano dá 2,2 e batidas na mesa dão 2,4.

Este terceiro critério existe por um motivo concreto: **digitar são ~5 batidas
por segundo, ou seja 5 Hz — bem no meio da faixa de tremor.** Sem ele, digitar
no teclado aciona a terapia.

Os três valores são publicados na serial, e o painel do Processing mostra qual
deles está barrando. Sacudir a mão no ar costuma passar de sobra na
amplitude e falhar na dominância: o punho gira junto, e o giro redistribui a
gravidade pela faixa de 0,5–3 Hz. Com a mão apoiada, como num paciente em
repouso, isso praticamente não acontece.

Os três limiares ficam num bloco comentado no topo do `.ino`, com a explicação
de para que serve cada um e o que acontece ao mexer.

A decisão usa o **módulo dos três eixos**, não eixo a eixo: um tremor de 0,07 g
em cada eixo tem módulo real de 0,12 g e não pode ser descartado três vezes.

### 2. Terapia — dois modos

A luva implementa **duas hipóteses diferentes** sobre por que a vibração
reduziria o tremor. Elas compartilham a mesma detecção e o mesmo ciclo
medir/tratar; muda só o que acontece durante a estimulação, o que as torna
diretamente comparáveis.

| | `CONTRA-ESTÍMULO` | `CR` |
|---|---|---|
| Ideia | mascaramento sensorial: inundar os mecanorreceptores para o sinal do tremor se perder | dessincronizar a rede de neurônios que dispara em bloco |
| Motores | os 5 juntos, contínuo | 1 dedo por vez, ordem sorteada |
| Pausas | nenhuma | 3 ciclos estimulando, 2 em silêncio |
| LEDs | todos acesos junto | só o dedo sendo pulsado |
| Efeito esperado | enquanto está ligado | cumulativo, ao longo de semanas |
| Base | intuitiva | pesquisa publicada (Tass e col.) |

O contra-estímulo é o que o artigo do projeto descreve. O CR é a técnica com
estudos publicados para luva vibratória — e o motivo de todo o cuidado com
ordem sorteada, jitter e ciclagem.

**Importante para quem for demonstrar:** o benefício relatado do CR é
cumulativo e persistente, não imediato. Ligar a luva não faz o tremor parar na
hora, e isso não significa defeito. O contra-estímulo é o modo com chance de
efeito perceptível na hora — e o menos embasado. Essa troca é o ponto que vale
explicar.

**Troca em tempo real** pela serial: `m` = contra-estímulo, `c` = CR. No
visualizador, teclas `1` e `2`. Não precisa regravar a placa.

**Consumo:** no contra-estímulo os cinco motores ficam ligados ao mesmo tempo,
contra um de cada vez no CR. Se a bateria não segurar e o ESP32 reiniciar,
baixe `CONTRA_DUTY` no topo do `.ino`.

### Parâmetros do CR

```
CR_CYCLE_MS   667 ms    ciclo de ~1,5 Hz
CR_BURST_MS   100 ms    duração do pulso de cada dedo
CR_BURST_DUTY 200/255   ~78% de intensidade
CR_JITTER_MS  ±15 ms    jitter temporal
ciclagem      3 ON / 2 OFF
```

Cada dedo pulsa **uma vez por ciclo**, em ordem sorteada (Fisher-Yates) com
jitter temporal. A cada 3 ciclos estimulando vêm 2 em silêncio — a pausa faz
parte do protocolo, é ela que dá tempo à rede de expressar a dessincronização.

### 3. O ciclo medir / tratar

Este é o ponto mais importante do projeto, e o menos óbvio.

**O acelerômetro está na mesma luva que os motores.** Se ele continuasse medindo
durante a terapia, mediria a própria vibração — e a luva se auto-detectaria,
mantendo a terapia ligada para sempre. Por isso existe uma máquina de estados:

```
MEDINDO  ──detectou tremor──▶  TRATANDO  ──6,7 s──▶  ASSENTANDO  ──250 ms──┐
   ▲                                                                        │
   └────────────────────────────────────────────────────────────────────────┘
     (buffer é descartado e reenchido do zero: 2,56 s)
```

- **MEDINDO** — motores parados, buffer sendo alimentado, análise rodando
- **TRATANDO** — padrão CR nos motores; o buffer **não** recebe amostras
- **ASSENTANDO** — motores desligados, esperando o ERM parar de girar

Além disso, o filtro passa-baixa interno do MPU6050 é configurado em **20 Hz**.
Sem ele o sensor responderia até 260 Hz, e como amostramos a 50 Hz (Nyquist =
25 Hz), a vibração do ERM (100–250 Hz) dobraria por *aliasing* para dentro da
faixa 3,5–7 Hz e seria lida como tremor.

### 4. Indicação visual

Cada LED espelha o motor do seu dedo: acende com ele e apaga com ele. No modo
CR isso significa um LED de cada vez; no contra-estímulo, todos juntos. Fora da
terapia todos ficam apagados.

---

## Protocolo serial

115200 baud, uma linha por amostra (50 Hz), 7 campos separados por vírgula:

```
roll,pitch,tX,tY,tZ,tTotal,estado,dominancia,nitidez,modo
```

| Campo | Unidade | Descrição |
|---|---|---|
| `roll`, `pitch` | graus | inclinação (filtro complementar) — só telemetria |
| `tX`, `tY`, `tZ` | g | amplitude do tremor em cada **eixo do acelerômetro** |
| `tTotal` | g | módulo dos três eixos; é ele que aciona a terapia |
| `estado` | 0/1/2 | 0 = medindo (ao vivo), 1 = terapia, 2 = janela enchendo |
| `dominancia` | 0–1 | fração da energia na faixa de tremor (2º critério) |
| `nitidez` | ≥0 | pico da faixa ÷ média da faixa (3º critério) |
| `modo` | 0/1 | 0 = contra-estímulo, 1 = coordinated reset |

O **estado 2** existe porque, logo depois de uma sessão de terapia, o firmware
descarta a janela e leva 2,56 s para enchê-la de novo. Nesse intervalo os
valores publicados ainda são os da análise anterior. Sem esse aviso o painel
exibia a medida velha como se fosse ao vivo e anunciava "TREMOR DETECTADO" com
a mão parada.

Dois detalhes que confundem quem lê pela primeira vez:

- **`tX`/`tY`/`tZ` são eixos, não dedos.** A luva tem um MPU só: mede a mão
  inteira e não tem como saber qual dedo está tremendo.
- **Força alta não significa disparo.** A terapia só liga com os três critérios
  satisfeitos ao mesmo tempo.
- **Durante a terapia os valores ficam congelados** no último medido, por causa
  do blanking descrito acima. O campo `terapia` diz quando isso está valendo.

No boot o firmware também imprime mensagens de diagnóstico que **não** seguem
esse formato (teste sequencial, offsets de calibração, erros de I2C). Qualquer
consumidor deve descartar linhas com menos de 10 campos.
Linhas iniciadas por `#` são avisos do firmware (modo ativo, diagnóstico).

Ligando `DIAGNOSTICO = true` no topo do `.ino`, a telemetria dá lugar a uma
linha por análise dizendo os valores medidos e qual critério barrou — útil no
Monitor Serial (o visualizador não funciona nesse modo).

---

## Visualização

`visualizacao/visuLuvinha/` é um sketch em Processing que mostra a mão em 3D,
as barras de amplitude por eixo com a marca do limiar, o histórico dos últimos
4 s e o estado da terapia.

Teclas `1` e `2` trocam o modo de estimulação em tempo real.

A porta serial é detectada automaticamente (procura `ttyUSB`, `ttyACM`, `COM`).
Para forçar uma porta específica, mude `PORTA_IDX` no topo do arquivo.

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

1. **Teste sequencial (~15 s)** — liga cada LED e cada motor um de cada vez,
   imprimindo nome e pino no Serial. Serve para isolar problema de fiação por
   canal de problema de alimentação compartilhada.
2. **Calibração** — mantenha a luva **parada** apoiada numa superfície firme. Se
   o firmware detectar movimento (excursão acima de 0,05 g), avisa e repete até
   3 vezes.
3. **Operação** — a partir daí, ciclos de medição e terapia.

Se o MPU6050 não responder, o LED do mindinho pisca continuamente em vez de a
placa travar em silêncio.

---

## Limitações conhecidas

**Motores ERM, não LRA.** No ERM a frequência de vibração está acoplada à
tensão: não dá para fixá-la. A literatura de vCR usa atuadores com frequência
controlada (~250 Hz), o que exigiria LRA. Esta é a diferença mais relevante em
relação aos estudos publicados.

**Um único acelerômetro.** A luva mede o tremor da mão como um todo. Detecção
por dedo exigiria um sensor por dedo.

**Latência de resposta.** Da parada do tremor até a terapia soltar passa-se o
bloco em curso (até 6,7 s) mais o reenchimento da janela (2,56 s).

**Movimento rítmico na faixa de tremor.** O critério de nitidez separa impacto
de oscilação, mas um movimento voluntário *sustentado e regular* entre 3,5 e
7 Hz é indistinguível de tremor para um acelerômetro. Não há como resolver isso
sem outro sinal (EMG, por exemplo).

**Sem validação clínica.** Os parâmetros vieram da literatura e de simulação. A
eficácia real não foi medida.

---

## Referências

Confira cada citação antes de usar em trabalho acadêmico — a lista abaixo é um
ponto de partida, não uma bibliografia verificada.

- **Tass, P. A. (2003).** *Biological Cybernetics.* Artigo teórico original que
  propõe o Coordinated Reset.
- **Syrkin-Nikolau, J. et al. / Bronte-Stewart, H. (2018).** *Movement
  Disorders.* CR vibrotátil com melhora prolongada no Parkinson (Stanford).
- **Pfeifer, K. J. et al., incl. Tass, P. A. (2021).** *Frontiers in
  Physiology.* "Coordinated Reset Vibrotactile Stimulation Induces Sustained
  Cumulative Benefits in Parkinson's Disease" — o trabalho mais próximo deste
  projeto: luva vibrotátil e benefício cumulativo.
- **Cala Trio (Cala Health).** Pulseira com autorização da FDA, mas para *tremor
  essencial* e por estimulação *elétrica*. Mecanismo diferente, útil como
  comparação.
- **GyroGear / GyroGlove.** Luva giroscópica: estabiliza mecanicamente em vez de
  tratar.

---

## Estrutura

```
sketch_tremor_FINAL222.ino          firmware do ESP32
visualizacao/visuLuvinha/           visualização em Processing
abrir-projeto.sh                    abre os dois de uma vez
```
