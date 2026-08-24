#include <Wire.h>
#include <MPU6050.h>
#include <esp_task_wdt.h>

MPU6050 mpu;

// --- MAPEAMENTO DEDO <-> MOTOR <-> LED ---
// Cada canal (0-4) = um dedo/posição, com seu LED indicador e seu motor.
// Motores em pinos NÃO-strapping (evita os problemas de boot do GPIO2/GPIO5).
//   Canal 0  Polegar (dedão)                LED 27   MOTOR 4
//   Canal 1  Indicador                      LED 26   MOTOR 18
//   Canal 2  Nervo frente (meio-anelar)     LED 25   MOTOR 23
//   Canal 3  Nervo trás   (meio-anelar)     LED 33   MOTOR 13
//   Canal 4  Mindinho                       LED 32   MOTOR 19
const int LED_POLEGAR   = 27;
const int LED_INDICADOR = 26;
const int LED_NERVO_F   = 25;
const int LED_NERVO_T   = 33;
const int LED_MINDINHO  = 32;

const int MOTOR_POLEGAR   = 4;
const int MOTOR_INDICADOR = 18;
const int MOTOR_NERVO_F   = 23;
const int MOTOR_NERVO_T   = 13;
const int MOTOR_MINDINHO  = 19;

// --- CONFIGURAÇÕES ---
const float ALPHA       = 0.96;   // filtro complementar (só telemetria roll/pitch)
const int   BUFFER_SIZE = 128;    // janela de 2,56 s @ 50 Hz -> resolução Δf = 0,39 Hz
const float SAMPLE_RATE = 50.0;   // Hz
const unsigned long SAMPLE_INTERVAL_MS = 20;   // 1000 / SAMPLE_RATE
const int   STEP        = 16;     // recalcula a detecção a cada 16 amostras (~0,32 s) -> janela deslizante
const uint32_t WDT_TIMEOUT_MS = 4000;  // watchdog: reinicia se o loop travar

// --- CALIBRAÇÃO ---
// Em repouso o ruído do MPU fica na casa de 0,01-0,02 g pico-a-pico; acima de
// 0,05 g é porque a mão se mexeu e os offsets sairiam errados.
const float CALIB_MAX_RANGE_G = 0.05;
const int   CALIB_TENTATIVAS  = 3;

// ===========================================================================
// AJUSTE DE SENSIBILIDADE  <<< é aqui que se mexe se a luva estiver disparando
//                              cedo demais ou tarde demais
// ===========================================================================
//
// São DOIS critérios, e os dois têm que passar ao mesmo tempo. Eles protegem
// contra coisas diferentes — mexer no errado não resolve.
//
// TREMOR_MIN_G  quão FORTE precisa ser o tremor.
//   Tremor senoidal: a_pico = (2*pi*f)^2 * deslocamento.
//   0,08 g equivale a um tremor de ~0,8 mm a 5 Hz.
//   É este critério que barra movimento voluntário lento (acenar a 1,5 Hz,
//   mesmo com 2 g de amplitude, deixa só 0,08 g na faixa de tremor).
//   Diminuir -> dispara com tremor mais fraco, e também com mais ruído.
//
// DOMINANCE_MIN  quão LIMPO precisa ser o tremor.
//   Fração da energia que precisa estar em 3,5-7 Hz, e não em 0,5-3 Hz:
//     0,50 = a faixa de tremor só precisa empatar com a de movimento
//     0,75 = a faixa de tremor precisa ser 3x maior
//   É este critério que barra gesto rápido perto da faixa de tremor.
//   Diminuir -> tolera tremor "sujo", misturado com movimento voluntário.
//
// Por que 0,50 e não 0,75 (o valor original):
//   Sacudir a mão no ar não produz só oscilação — o punho também GIRA, e o
//   giro redistribui a gravidade (1 g inteiro!) entre os eixos, enchendo a
//   faixa de 0,5-3 Hz. Medido em simulação, com 0,20 g de tremor e 0,20 g de
//   giro a dominância fica em 0,51: com o limiar em 0,75 era preciso tremer
//   ~3x mais forte só para vencer o giro do próprio punho. Já um paciente com
//   a mão apoiada quase não gira o punho e chega a 0,99 de dominância — ou
//   seja, o limiar antigo atrapalhava o teste de bancada, não o uso real.
//   Conferido: nenhum movimento voluntário testado passa de 0,45 de
//   dominância, então 0,50 continua com margem.
//
// NITIDEZ_MIN  quão CONCENTRADO é o tremor numa única frequência.
//   Razão entre o pico da faixa 3,5-7 Hz e a média dessa mesma faixa.
//   Tremor é uma oscilação sustentada: toda a energia cai numa frequência só,
//   e a razão fica alta. Digitar, passos e batidas na mesa são IMPACTOS: cada
//   golpe espalha energia por toda a faixa, e a razão cai.
//   Medido em simulação: tremor sustentado de 4 a 6 Hz dá 4,6 de forma bem
//   estável; digitar com o jitter natural de quem digita dá 2,2, batidas na
//   mesa dão 2,4. Daí o limiar em 3,5, no meio dos dois grupos.
//   Este critério existe porque digitar são ~5 batidas por segundo — 5 Hz,
//   bem no meio da faixa de tremor. Sem ele, digitar aciona a terapia.
//   Diminuir -> aceita tremor mais irregular, e também impactos.
const float TREMOR_MIN_G   = 0.08;
const float DOMINANCE_MIN  = 0.50;
const float NITIDEZ_MIN    = 3.50;

// Modo diagnóstico: em vez da telemetria CSV, imprime uma linha por análise
// dizendo os dois valores medidos e QUAL critério barrou. Use com o Monitor
// Serial (o visualizador em Processing não funciona com isto ligado).
const bool DIAGNOSTICO = false;

// --- BANDAS DE ANÁLISE ---
// Resolução da janela: Δf = 50/128 = 0,39 Hz. Sondando de 0,5 em 0,5 Hz, um
// tremor caído entre duas sondas perdia mais da metade da amplitude medida
// (perda por scalloping). Com passo de 0,25 Hz a perda máxima cai para ~6%.
const float F_STEP    = 0.25;
const float VOL_F_MIN = 0.50;  const int VOL_N = 11;  // 0,50 .. 3,00 Hz (voluntário)
const float TRE_F_MIN = 3.50;  const int TRE_N = 15;  // 3,50 .. 7,00 Hz (tremor)

// --- PARÂMETROS COORDINATED RESET (CR) ---
// Baseado em Tass/Stanford: pulsos curtos, 1 por dedo por ciclo, ordem embaralhada.
// (ERM não permite fixar 250 Hz de vibração; o ideal para replicar exatamente seria LRA.)
const unsigned long CR_CYCLE_MS   = 667;  // ciclo de ~1,5 Hz
const unsigned long CR_BURST_MS   = 100;  // duração do pulso de cada dedo
const int           CR_BURST_DUTY = 200;  // ~78% (intensidade do pulso, 0-255)
const long          CR_JITTER_MS  = 15;   // jitter temporal de cada pulso, ±15 ms
const int           CR_CYCLES_ON  = 3;    // ciclagem do protocolo: 3 ciclos estimulando...
const int           CR_CYCLES_OFF = 2;    // ...e 2 em silêncio (Tass)

// --- CICLO MEDIR / TRATAR ---
// O acelerômetro está na MESMA luva que os motores, então durante a terapia ele
// mede a própria vibração: a amplitude medida infla (medimos 0,05 g virar
// 0,094 g) e a leitura deixa de ter significado. Por isso o buffer de detecção
// só é alimentado com os motores parados — nunca durante a terapia.
const unsigned long BLOCO_TERAPIA_MS = 2 * (CR_CYCLES_ON + CR_CYCLES_OFF) * CR_CYCLE_MS;  // ~6,7 s
const unsigned long ASSENTAMENTO_MS  = 250;   // espera o motor parar de girar antes de medir

float bufferX[BUFFER_SIZE];
float bufferY[BUFFER_SIZE];
float bufferZ[BUFFER_SIZE];
float hannWindow[BUFFER_SIZE];   // janela de Hann pré-computada
float orderedBuf[BUFFER_SIZE];   // buffer auxiliar, já em ordem cronológica
int   bufferIndex = 0;
bool  bufferReady = false;   // true após o primeiro ciclo completo de amostras
int   stepCounter = 0;

float roll  = 0.0;
float pitch = 0.0;
unsigned long lastTime   = 0;
unsigned long lastSample = 0;

float offsetAX = 0, offsetAY = 0, offsetAZ = 0;
float offsetGX = 0, offsetGY = 0;   // bias do giroscópio, em graus/s

// Valores de tremor (amplitude em g da frequência dominante) persistem entre ciclos
float tX = 0, tY = 0, tZ = 0, tTotal = 0;
// Última análise, publicada na telemetria e usada no modo diagnóstico
float ultVolTotal = 0, ultDominancia = 0, ultNitidez = 0;
// false enquanto a janela ainda está se enchendo: os valores de tX/tY/tZ são
// os da análise anterior, e o painel não pode mostrá-los como leitura ao vivo.
bool  leituraValida = false;

// --- ESTADO: MEDINDO -> TRATANDO -> ASSENTANDO -> MEDINDO ---
// MEDINDO     motores parados, buffer sendo preenchido, análise rodando
// TRATANDO    padrão CR nos motores; o buffer NÃO é alimentado (blanking)
// ASSENTANDO  motores desligados, esperando o ERM parar de girar
enum Estado { MEDINDO, TRATANDO, ASSENTANDO };
Estado        estado       = MEDINDO;
unsigned long estadoInicio = 0;

// Índices 0-4 = LEDs (por dedo); índices 5-9 = motores (mesmo dedo, +5).
const int PINOS[10] = {
  LED_POLEGAR,   LED_INDICADOR,   LED_NERVO_F,   LED_NERVO_T,   LED_MINDINHO,
  MOTOR_POLEGAR, MOTOR_INDICADOR, MOTOR_NERVO_F, MOTOR_NERVO_T, MOTOR_MINDINHO
};

// Motores (índices lógicos 5-9) acionados pelo padrão CR
const int MOTOR_IDX[5] = {5, 6, 7, 8, 9};
int  crOrder[5]  = {0, 1, 2, 3, 4};
long crJitter[5] = {0, 0, 0, 0, 0};
int  crLast[5]   = {0, 0, 0, 0, 0};   // duty atual do motor de cada dedo
int  ledLast[5]  = {-1, -1, -1, -1, -1};
unsigned long crCycleStart = 0;
unsigned long crCycleCount = 0;
bool crAtivo = false;

// --- CONFIGURAÇÃO DO MPU (boot e recuperação de I2C) ---
// Centralizada para o recovery não esquecer nenhum registrador — em especial
// o DLPF, que sumia depois de um mpu.initialize() solto.
void configuraMPU() {
  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);   // ±2g  -> 16384 LSB/g
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);   // ±250 dps -> 131 LSB/dps
  // ANTI-ALIASING (essencial). Sem o DLPF o sensor responde até 260 Hz; como
  // amostramos a 50 Hz (Nyquist = 25 Hz), a vibração do motor ERM (100-250 Hz)
  // dobra para dentro da banda 3,5-7 Hz e é lida como tremor. Cortar em 20 Hz
  // deixa passar toda a faixa de interesse e mata o aliasing.
  mpu.setDLPFMode(MPU6050_DLPF_BW_20);
}

// --- CALIBRAÇÃO ---
// Uma passada de calibração. Devolve a maior excursão pico-a-pico vista em
// qualquer eixo do acelerômetro (em g) — serve para saber se a mão ficou parada.
float passadaCalibracao() {
  const int N = 500;
  long sumAX = 0, sumAY = 0, sumAZ = 0;
  long sumGX = 0, sumGY = 0;
  int16_t minA[3] = { 32767,  32767,  32767};
  int16_t maxA[3] = {-32768, -32768, -32768};

  for (int i = 0; i < N; i++) {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    sumAX += ax; sumAY += ay; sumAZ += az;
    sumGX += gx; sumGY += gy;

    int16_t a[3] = {ax, ay, az};
    for (int e = 0; e < 3; e++) {
      if (a[e] < minA[e]) minA[e] = a[e];
      if (a[e] > maxA[e]) maxA[e] = a[e];
    }
    delay(2);
  }

  offsetAX = sumAX / (float)N / 16384.0;
  offsetAY = sumAY / (float)N / 16384.0;
  offsetAZ = sumAZ / (float)N / 16384.0 - 1.0;  // assume Z apontando para cima (~+1g) na calibração
  // Sem remover o bias do giroscópio o filtro complementar deriva sozinho
  // e o roll/pitch da visualização vai girando com a mão parada.
  offsetGX = sumGX / (float)N / 131.0;
  offsetGY = sumGY / (float)N / 131.0;

  float maiorRange = 0;
  for (int e = 0; e < 3; e++) {
    float r = (maxA[e] - minA[e]) / 16384.0;
    if (r > maiorRange) maiorRange = r;
  }
  return maiorRange;
}

// --- CALIBRAÇÃO (acelerômetro + giroscópio, com verificação de repouso) ---
void calibrateMPU() {
  float range = 0;

  for (int tentativa = 1; tentativa <= CALIB_TENTATIVAS; tentativa++) {
    Serial.print("Calibrando (tentativa ");
    Serial.print(tentativa); Serial.print("/"); Serial.print(CALIB_TENTATIVAS);
    Serial.println(")... mantenha o sensor completamente parado.");

    range = passadaCalibracao();

    if (range <= CALIB_MAX_RANGE_G) break;

    // A mão se mexeu: os offsets saem errados e o zero fica torto.
    Serial.print("AVISO: movimento durante a calibracao (");
    Serial.print(range, 3);
    Serial.print(" g de excursao, limite ");
    Serial.print(CALIB_MAX_RANGE_G, 3);
    Serial.println(" g).");
    if (tentativa < CALIB_TENTATIVAS) {
      Serial.println("Repetindo em 2 s. Apoie a mao numa superficie firme.");
      delay(2000);
    } else {
      Serial.println("Seguindo mesmo assim: o zero do roll/pitch pode ficar torto.");
    }
  }

  Serial.print("Offset acel X="); Serial.print(offsetAX, 4);
  Serial.print(" Y=");             Serial.print(offsetAY, 4);
  Serial.print(" Z=");             Serial.println(offsetAZ, 4);
  Serial.print("Offset gyro X="); Serial.print(offsetGX, 4);
  Serial.print(" Y=");             Serial.println(offsetGY, 4);
  Serial.print("Excursao durante a calibracao: "); Serial.print(range, 3); Serial.println(" g");
  Serial.println("Calibracao concluida!");
}

// --- GOERTZEL (normalizado: devolve amplitude em g na frequência 'freq') ---
// Espera o buffer já em ordem cronológica, sem média e com a janela de Hann
// aplicada — o Goertzel é um filtro recursivo, processar as amostras fora de
// ordem muda o resultado.
float goertzelBand(const float* buf, float freq) {
  float omega = 2.0 * PI * freq / SAMPLE_RATE;
  float coeff = 2.0 * cos(omega);
  float s_prev = 0, s_prev2 = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    float s = buf[i] + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev  = s;
  }
  float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
  // Para um seno de amplitude A: |X| = (A/2) * soma(w[n]). A janela de Hann
  // tem ganho coerente 0,5, então soma(w) = N/2 e |X| = A*N/4.
  return sqrt(fabs(power)) / (BUFFER_SIZE / 4.0);
}

// Copia o buffer circular em ordem cronológica a partir da amostra mais antiga,
// remove a média (mata o DC da gravidade) e aplica a janela de Hann.
// Sem a janela, um movimento voluntário forte a 1-2 Hz vaza energia para a
// faixa 3,5-7 Hz e dispara falso positivo.
void prepareBuffer(const float* src, int start) {
  float mean = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) mean += src[i];
  mean /= BUFFER_SIZE;
  for (int i = 0; i < BUFFER_SIZE; i++)
    orderedBuf[i] = (src[(start + i) % BUFFER_SIZE] - mean) * hannWindow[i];
}

// --- ANÁLISE DE UM EIXO ---
// Devolve o pico da banda de tremor (amplitude em g da frequência dominante)
// e escreve o pico da banda voluntária em *picoVoluntario. Não aplica limiar
// nenhum: quem decide se é tremor é analisaTremor(), olhando os três eixos
// juntos — senão um tremor de 0,07 g em cada eixo (0,12 g de módulo real)
// seria descartado três vezes e o total daria zero.
float analisaEixo(const float* src, int start, float* picoVoluntario,
                  float* mediaTremor) {
  prepareBuffer(src, start);

  // Movimento voluntário: 0,50 a 3,00 Hz
  float picoVol = 0;
  for (int i = 0; i < VOL_N; i++) {
    float v = goertzelBand(orderedBuf, VOL_F_MIN + i * F_STEP);
    if (v > picoVol) picoVol = v;
  }

  // Tremor de Parkinson: 3,50 a 7,00 Hz (literatura: 3-7 Hz, modal 4-6 Hz)
  // Guarda o pico E a média da faixa: a razão entre os dois diz se a energia
  // está concentrada numa frequência (tremor) ou espalhada (impacto).
  float picoTre = 0, somaTre = 0;
  for (int i = 0; i < TRE_N; i++) {
    float v = goertzelBand(orderedBuf, TRE_F_MIN + i * F_STEP);
    somaTre += v;
    if (v > picoTre) picoTre = v;
  }

  *picoVoluntario = picoVol;
  *mediaTremor    = somaTre / TRE_N;
  return picoTre;
}

// --- DECISÃO DE TREMOR (três eixos juntos) ---
// Preenche tX/tY/tZ/tTotal com as amplitudes medidas (sempre, sem corte) e
// devolve true quando o conjunto passa nos dois critérios: amplitude mínima
// e dominância da faixa de tremor sobre a faixa voluntária.
bool analisaTremor(int start) {
  float volX, volY, volZ;
  float medX, medY, medZ;
  tX = analisaEixo(bufferX, start, &volX, &medX);
  tY = analisaEixo(bufferY, start, &volY, &medY);
  tZ = analisaEixo(bufferZ, start, &volZ, &medZ);

  tTotal = sqrt(tX * tX + tY * tY + tZ * tZ);
  float volTotal = sqrt(volX * volX + volY * volY + volZ * volZ);
  float medTotal = sqrt(medX * medX + medY * medY + medZ * medZ);
  if (volTotal < 0.001) volTotal = 0.001;
  if (medTotal < 1e-6)  medTotal = 1e-6;

  ultVolTotal   = volTotal;
  ultDominancia = tTotal / (tTotal + volTotal);
  ultNitidez    = tTotal / medTotal;
  leituraValida = true;   // esta janela produziu uma medida de verdade

  return (tTotal        >= TREMOR_MIN_G
       && ultDominancia >= DOMINANCE_MIN
       && ultNitidez    >= NITIDEZ_MIN);
}

// --- DIAGNÓSTICO ---
// Uma linha por análise dizendo os dois valores medidos e qual critério barrou.
// Sem vírgulas de propósito: se sobrar alguma linha destas no meio da
// telemetria, o visualizador a descarta em vez de tentar interpretá-la.
void imprimeDiagnostico(bool disparou) {
  bool ampOk = (tTotal        >= TREMOR_MIN_G);
  bool domOk = (ultDominancia >= DOMINANCE_MIN);
  bool nitOk = (ultNitidez    >= NITIDEZ_MIN);

  Serial.print("# forca=");    Serial.print(tTotal, 3);
  Serial.print("g");           Serial.print(ampOk ? "[ok] " : "[--] ");
  Serial.print(" pureza=");    Serial.print(ultDominancia, 2);
  Serial.print(domOk ? "[ok] " : "[--] ");
  Serial.print(" nitidez=");   Serial.print(ultNitidez, 2);
  Serial.print(nitOk ? "[ok] " : "[--] ");
  Serial.print(" -> ");

  if (disparou)    Serial.println("DISPARA");
  else if (!ampOk) Serial.println("parado: tremor fraco demais");
  else if (!domOk) Serial.println("parado: muito movimento lento junto (punho girando?)");
  else             Serial.println("parado: energia espalhada, parece impacto e nao oscilacao");
}

// --- EMBARALHA A ORDEM DOS DEDOS E APLICA JITTER TEMPORAL (CR) ---
// A ordem sorteada é o mecanismo do Coordinated Reset, não um enfeite: um
// padrão regular reforçaria a sincronia em vez de quebrá-la.
void shuffleCR() {
  for (int i = 4; i > 0; i--) {            // Fisher-Yates
    int j = random(i + 1);
    int t = crOrder[i]; crOrder[i] = crOrder[j]; crOrder[j] = t;
  }
  for (int i = 0; i < 5; i++)
    crJitter[i] = random(-CR_JITTER_MS, CR_JITTER_MS + 1);
}

// --- PADRÃO COORDINATED RESET NOS 5 MOTORES ---
// Cada dedo pulsa 1x por ciclo de CR_CYCLE_MS, em ordem sorteada.
// Ciclagem: CR_CYCLES_ON ciclos estimulando, CR_CYCLES_OFF em silêncio. A pausa
// faz parte do protocolo (Tass) — é ela que dá tempo à rede de expressar a
// dessincronização; estimular sem parar não replica a literatura.
// Esta função é dona APENAS dos motores. Os LEDs são tratados em atualizaLeds().
void updateCR(bool active) {
  if (!active) {
    if (crAtivo) {
      for (int i = 0; i < 5; i++) {
        ledcWrite(PINOS[MOTOR_IDX[i]], 0);
        crLast[i] = 0;
      }
      crAtivo = false;
    }
    return;
  }

  unsigned long now = millis();
  if (!crAtivo) {
    crCycleStart = now;
    crCycleCount = 0;
    shuffleCR();
    crAtivo = true;
  }
  if (now - crCycleStart >= CR_CYCLE_MS) {
    crCycleStart += CR_CYCLE_MS;
    if (now - crCycleStart >= CR_CYCLE_MS) crCycleStart = now;   // re-sincroniza
    crCycleCount++;
    shuffleCR();
  }

  // Ciclo de silêncio da ciclagem: motores parados, mas o relógio do CR segue
  bool pulsando = (crCycleCount % (CR_CYCLES_ON + CR_CYCLES_OFF)) < CR_CYCLES_ON;

  long phase = (long)(now - crCycleStart);
  long slot  = (long)(CR_CYCLE_MS / 5);

  for (int sl = 0; sl < 5; sl++) {
    int phys = crOrder[sl];                       // qual dedo ocupa este slot
    // O jitter soma CR_JITTER_MS de offset para o slot 0 nunca começar em
    // instante negativo (antes, um jitter negativo encurtava o 1º pulso em 20%).
    // Pior caso: 4*133 + 2*15 + 100 = 662 ms, ainda dentro dos 667 do ciclo.
    long start = (long)sl * slot + CR_JITTER_MS + crJitter[sl];
    long end   = start + (long)CR_BURST_MS;
    bool on    = pulsando && (phase >= start && phase < end);
    int  duty  = on ? CR_BURST_DUTY : 0;
    if (crLast[phys] != duty) {
      ledcWrite(PINOS[MOTOR_IDX[phys]], duty);
      crLast[phys] = duty;
    }
  }
}

// --- LEDS: ESPELHO DOS MOTORES ---
// Cada LED representa o motor do seu dedo e mais nada: acende junto com o
// pulso CR, apaga junto. (Chegou a existir aqui um brilho de fundo
// proporcional ao tremor medido, mas na bancada ele acendia com qualquer
// movimentinho e mais atrapalhava do que ajudava a ler o que a luva faz.)
void atualizaLeds() {
  for (int i = 0; i < 5; i++) {
    int alvo = (crLast[i] > 0) ? 255 : 0;
    if (alvo != ledLast[i]) {
      ledcWrite(PINOS[i], alvo);
      ledLast[i] = alvo;
    }
  }
}

// --- ZERA OS BUFFERS DE ANÁLISE ---
// Descarta a janela inteira: a análise só volta depois de BUFFER_SIZE amostras
// novas e contíguas, para o Goertzel nunca misturar amostras de antes e depois.
void limpaBuffers() {
  memset(bufferX, 0, sizeof(bufferX));
  memset(bufferY, 0, sizeof(bufferY));
  memset(bufferZ, 0, sizeof(bufferZ));
  bufferIndex   = 0;
  bufferReady   = false;
  stepCounter   = 0;
  leituraValida = false;   // até a janela encher, o valor exibido é o antigo
}

// --- DESLIGA TODOS OS DISPOSITIVOS ---
void desligarTudo() {
  for (int i = 0; i < 10; i++) ledcWrite(PINOS[i], 0);
  for (int i = 0; i < 5; i++) { crLast[i] = 0; ledLast[i] = 0; }
  crAtivo = false;
}

// --- TROCA DE ESTADO ---
// Entrar em MEDINDO sempre descarta a janela: as amostras tomadas com o motor
// ligado (ou logo depois dele) não podem se misturar com as novas dentro do
// mesmo buffer, senão a análise volta a medir a própria terapia.
void trocaEstado(Estado novo, unsigned long agora) {
  estado       = novo;
  estadoInicio = agora;
  if (novo == MEDINDO) limpaBuffers();
}

// --- SETUP ---
void setup() {
  // setTxBufferSize() só tem efeito ANTES do begin(); depois é ignorado
  Serial.setTxBufferSize(512);
  Serial.begin(115200);

  // Nível baixo garantido antes de anexar o PWM: logo após um reset os GPIOs
  // ficam em entrada e a base do transistor flutua, o que pode ligar o motor.
  // (No hardware, um resistor de pull-down na base de cada transistor fecha
  // essa janela por completo — a proteção por software só cobre o pós-boot.)
  for (int i = 0; i < 10; i++) {
    pinMode(PINOS[i], OUTPUT);
    digitalWrite(PINOS[i], LOW);
  }

  // Timeout I2C: 3ms — se o MPU travar, o Wire abandona e continua
  Wire.begin(21, 22);
  Wire.setTimeOut(3);

  configuraMPU();

  // LEDs: 5000 Hz (indicadores visuais, luz suave)
  if (!ledcAttach(LED_POLEGAR,   5000, 8)) Serial.println("Falha LEDC LED_POLEGAR");
  if (!ledcAttach(LED_INDICADOR, 5000, 8)) Serial.println("Falha LEDC LED_INDICADOR");
  if (!ledcAttach(LED_NERVO_F,   5000, 8)) Serial.println("Falha LEDC LED_NERVO_F");
  if (!ledcAttach(LED_NERVO_T,   5000, 8)) Serial.println("Falha LEDC LED_NERVO_T");
  if (!ledcAttach(LED_MINDINHO,  5000, 8)) Serial.println("Falha LEDC LED_MINDINHO");

  // Motores ERM: 20 kHz (PWM inaudível; a inércia do motor filtra para tensão média)
  if (!ledcAttach(MOTOR_POLEGAR,   20000, 8)) Serial.println("Falha LEDC MOTOR_POLEGAR");
  if (!ledcAttach(MOTOR_INDICADOR, 20000, 8)) Serial.println("Falha LEDC MOTOR_INDICADOR");
  if (!ledcAttach(MOTOR_NERVO_F,   20000, 8)) Serial.println("Falha LEDC MOTOR_NERVO_F");
  if (!ledcAttach(MOTOR_NERVO_T,   20000, 8)) Serial.println("Falha LEDC MOTOR_NERVO_T");
  if (!ledcAttach(MOTOR_MINDINHO,  20000, 8)) Serial.println("Falha LEDC MOTOR_MINDINHO");

  memset(bufferX, 0, sizeof(bufferX));
  memset(bufferY, 0, sizeof(bufferY));
  memset(bufferZ, 0, sizeof(bufferZ));

  for (int i = 0; i < BUFFER_SIZE; i++)
    hannWindow[i] = 0.5 * (1.0 - cos(2.0 * PI * i / (BUFFER_SIZE - 1)));

  // MPU obrigatório: se não responder, sinaliza piscando o LED do mindinho (em vez de travar silenciosamente)
  if (!mpu.testConnection()) {
    Serial.println("ERRO: MPU6050 nao encontrado!");
    while (true) {
      ledcWrite(LED_MINDINHO, 255); delay(200);
      ledcWrite(LED_MINDINHO, 0);   delay(200);
    }
  }

  // DEBUG: testa cada dispositivo UM DE CADA VEZ (isola problema de
  // alimentacao compartilhada vs. fiacao por canal) e imprime nome/pino
  // no Serial. Roda antes da calibracao: motores param antes de calibrar.
  const char* NOMES[10] = {
    "LED Polegar (27)",   "LED Indicador (26)", "LED NervoFrente (25)",
    "LED NervoTras (33)", "LED Mindinho (32)",
    "MOTOR Polegar (4)",  "MOTOR Indicador (18)", "MOTOR NervoFrente (23)",
    "MOTOR NervoTras (13)", "MOTOR Mindinho (19)"
  };
  Serial.println("DEBUG: teste sequencial (um por vez, 1,2 s cada)...");
  for (int i = 0; i < 10; i++) {
    Serial.print("  -> "); Serial.println(NOMES[i]);
    ledcWrite(PINOS[i], 255);
    delay(1200);
    ledcWrite(PINOS[i], 0);
    delay(300);
  }

  Serial.println("DEBUG: teste concluido.");

  randomSeed(esp_random());   // semente para o embaralhamento do CR

  calibrateMPU();

  // Watchdog: se o loop travar com um motor acionado, o ESP32 reinicia em vez
  // de deixar o motor preso vibrando contra o dedo. Armado só agora, depois
  // dos delays longos do teste sequencial e da calibração.
  esp_task_wdt_config_t wdtCfg = {
    .timeout_ms     = WDT_TIMEOUT_MS,
    .idle_core_mask = 0,
    .trigger_panic  = true
  };
  if (esp_task_wdt_reconfigure(&wdtCfg) != ESP_OK) esp_task_wdt_init(&wdtCfg);
  esp_task_wdt_add(NULL);

  lastTime   = millis();
  lastSample = millis();
  trocaEstado(MEDINDO, lastTime);
}

// --- LOOP ---
void loop() {
  unsigned long now = millis();
  esp_task_wdt_reset();

  // O padrão CR exige timing fino, então a máquina de estados roda a cada
  // iteração — independente da taxa de amostragem de 50 Hz.
  switch (estado) {
    case TRATANDO:
      updateCR(true);
      if (now - estadoInicio >= BLOCO_TERAPIA_MS) trocaEstado(ASSENTANDO, now);
      break;

    case ASSENTANDO:
      updateCR(false);
      if (now - estadoInicio >= ASSENTAMENTO_MS) trocaEstado(MEDINDO, now);
      break;

    case MEDINDO:
      updateCR(false);
      break;
  }

  atualizaLeds();

  if (now - lastSample >= SAMPLE_INTERVAL_MS) {
    // Incrementa pelo intervalo fixo em vez de "= now": com "= now" o atraso de
    // cada iteração se acumula, a taxa real cai para ~48 Hz e como SAMPLE_RATE
    // é constante no cálculo do Goertzel, TODAS as frequências saem deslocadas.
    lastSample += SAMPLE_INTERVAL_MS;
    // Atraso maior que um intervalo inteiro: re-sincroniza sem rajada de catch-up
    if (now - lastSample >= SAMPLE_INTERVAL_MS) lastSample = now;

    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    // Falha de leitura I2C. Dois padrões possíveis:
    //   tudo zero  -> fisicamente impossível (o eixo Z sempre vê a gravidade)
    //   tudo 0xFFFF (-1) -> NACK no barramento, o caso de fio solto. Sem esta
    //                       checagem um cabo mal encaixado vira -0,00006 g e
    //                       passa como leitura válida.
    bool allZero = (ax ==  0 && ay ==  0 && az ==  0 && gx ==  0 && gy ==  0 && gz ==  0);
    bool allFF   = (ax == -1 && ay == -1 && az == -1 && gx == -1 && gy == -1 && gz == -1);
    if (allZero || allFF) {
      Serial.println(allFF ? "ERRO I2C: NACK (verifique a fiacao do MPU)"
                           : "ERRO I2C: leitura zerada");
      Wire.end();
      delay(5);
      Wire.begin(21, 22);
      Wire.setTimeOut(3);
      configuraMPU();          // reaplica também o DLPF, senão o anti-aliasing some
      desligarTudo();
      // trocaEstado(MEDINDO) descarta os buffers: sem isso as amostras
      // corrompidas poluiriam as análises pelos próximos 2,56 s.
      trocaEstado(MEDINDO, millis());
      // Zera a medida inteira, não só a força: senão o painel mostrava
      // pureza e nitidez antigas ao lado de uma força zerada.
      tX = tY = tZ = tTotal = 0;
      ultDominancia = ultNitidez = ultVolTotal = 0;
      lastTime   = millis();
      lastSample = millis();
      return;
    }

    float dt = (now - lastTime) / 1000.0;
    lastTime = now;
    if (dt <= 0.0 || dt > 0.5) dt = 1.0 / SAMPLE_RATE;

    float axG = ax / 16384.0 - offsetAX;
    float ayG = ay / 16384.0 - offsetAY;
    float azG = az / 16384.0 - offsetAZ;

    float gxDPS = gx / 131.0 - offsetGX;
    float gyDPS = gy / 131.0 - offsetGY;

    // Telemetria de inclinação (filtro complementar) — não entra na decisão de
    // terapia, então continua rodando também durante a estimulação.
    float rollAcc  = atan2(ayG, azG) * 180.0 / PI;
    float pitchAcc = atan2(-axG, sqrt(ayG * ayG + azG * azG)) * 180.0 / PI;
    roll  = ALPHA * (roll  + gxDPS * dt) + (1.0 - ALPHA) * rollAcc;
    pitch = ALPHA * (pitch + gyDPS * dt) + (1.0 - ALPHA) * pitchAcc;

    // BLANKING: o buffer de detecção só recebe amostras com os motores parados.
    if (estado == MEDINDO) {
      bufferX[bufferIndex] = axG;
      bufferY[bufferIndex] = ayG;
      bufferZ[bufferIndex] = azG;
      bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
      if (!bufferReady && bufferIndex == 0) bufferReady = true;

      // Janela deslizante: recalcula a detecção a cada STEP amostras
      stepCounter++;
      if (bufferReady && stepCounter >= STEP) {
        stepCounter = 0;
        int start = bufferIndex;   // posição mais antiga do buffer circular
        bool disparou = analisaTremor(start);
        if (DIAGNOSTICO) imprimeDiagnostico(disparou);
        if (disparou) trocaEstado(TRATANDO, now);
      }
    }

    // Serial não bloqueia nunca.
    // Campos: roll,pitch,tX,tY,tZ,tTotal,estado,dominancia,nitidez
    //   estado 0 = medindo, leitura ao vivo
    //          1 = terapia rodando (leitura congelada)
    //          2 = medindo, mas a janela ainda está enchendo — os valores
    //              exibidos são os da análise anterior
    // Sem o estado 2 o painel mostrava a medida velha como se fosse ao vivo:
    // logo depois da terapia ele anunciava "TREMOR DETECTADO" com a mão
    // parada, porque repetia o valor que tinha disparado a terapia.
    // A dominância vai junto para o painel poder dizer qual dos dois critérios
    // barrou. Sem ela o visualizador só via a amplitude e anunciava "TREMOR
    // ACIMA DO LIMIAR" mesmo quando o firmware tinha decidido que não era.
    // A linha completa chega a ~61 bytes; 64 de folga cobre com margem.
    if (!DIAGNOSTICO && Serial.availableForWrite() > 80) {
      Serial.print(roll, 2);   Serial.print(",");
      Serial.print(pitch, 2);  Serial.print(",");
      Serial.print(tX, 4);     Serial.print(",");
      Serial.print(tY, 4);     Serial.print(",");
      Serial.print(tZ, 4);     Serial.print(",");
      Serial.print(tTotal, 4); Serial.print(",");
      Serial.print(estado == TRATANDO ? 1 : (leituraValida ? 0 : 2));
      Serial.print(",");
      Serial.print(ultDominancia, 3); Serial.print(",");
      Serial.println(ultNitidez, 2);
    }
  }
}
