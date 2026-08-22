#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

// --- PINOS FÍSICOS ---
// OBS.: MOTOR_X (GPIO2) e MOTOR_Z (GPIO5) são pinos de "strapping" do ESP32.
// Se houver boot intermitente, revisar o circuito de acionamento desses pinos.
const int LED_X      = 27;
const int LED_Y      = 26;
const int LED_Z      = 25;
const int LED_TOTAL  = 33;
const int LED_REFORC = 32;

const int MOTOR_X     = 2;
const int MOTOR_Y     = 4;
const int MOTOR_Z     = 5;
const int MOTOR_TOTAL = 18;
const int MOTOR_REFOR = 19;

// --- CONFIGURAÇÕES ---
const float ALPHA       = 0.96;   // filtro complementar (só telemetria roll/pitch)
const int   BUFFER_SIZE = 128;    // janela de 2,56 s @ 50 Hz -> resolução Δf = 0,39 Hz
const float SAMPLE_RATE = 50.0;   // Hz
const int   STEP        = 16;     // recalcula a detecção a cada 16 amostras (~0,32 s) -> janela deslizante

// --- LIMIARES DE DETECÇÃO (em unidades físicas, g) ---
// Tremor senoidal: a_pico = (2*pi*f)^2 * deslocamento.
//   ~0,08 g  ≈ tremor de ~0,8 mm a 5 Hz  -> limiar de detecção
//   ~0,60 g  ≈ tremor de ~6 mm a 5 Hz    -> saturação da escala dos LEDs
const float TREMOR_MIN_G   = 0.08;
const float MAX_INTENSITY  = 0.60;
const float DOMINANCE_MIN  = 0.75;  // fração da energia que precisa estar na faixa de tremor

// --- PARÂMETROS COORDINATED RESET (CR) ---
// Baseado em Tass/Stanford: pulsos curtos, 1 por dedo por ciclo, ordem embaralhada.
// (ERM não permite fixar 250 Hz de vibração; o ideal para replicar exatamente seria LRA.)
const unsigned long CR_CYCLE_MS   = 667;  // ciclo de ~1,5 Hz
const unsigned long CR_BURST_MS   = 100;  // duração do pulso de cada dedo
const int           CR_BURST_DUTY = 200;  // ~78% (intensidade do pulso, 0-255)
const unsigned long THERAPY_HOLD_MS = 2000; // mantém a terapia ligada 2 s após a última detecção

float bufferX[BUFFER_SIZE];
float bufferY[BUFFER_SIZE];
float bufferZ[BUFFER_SIZE];
int   bufferIndex = 0;
bool  bufferReady = false;   // true após o primeiro ciclo completo de amostras
int   stepCounter = 0;

float roll  = 0.0;
float pitch = 0.0;
unsigned long lastTime   = 0;
unsigned long lastSample = 0;

float offsetAX = 0, offsetAY = 0, offsetAZ = 0;

// Valores de tremor (amplitude em g da frequência dominante) persistem entre ciclos
float tX = 0, tY = 0, tZ = 0, tTotal = 0;

// Estado da terapia
bool          hasDetected = false;
unsigned long lastTremorDetected = 0;

// lastPWM indexado por posição lógica 0-4 (só LEDs). Motores usam crLast.
float lastPWM[10] = {0};

const int PINOS[10] = {
  LED_X, LED_Y, LED_Z, LED_TOTAL, LED_REFORC,
  MOTOR_X, MOTOR_Y, MOTOR_Z, MOTOR_TOTAL, MOTOR_REFOR
};

// Motores (índices lógicos 5-9) acionados pelo padrão CR
const int MOTOR_IDX[5] = {5, 6, 7, 8, 9};
int  crOrder[5] = {0, 1, 2, 3, 4};
long crJitter[5] = {0, 0, 0, 0, 0};
int  crLast[5]  = {0, 0, 0, 0, 0};
unsigned long crCycleStart = 0;

// --- CALIBRAÇÃO ---
void calibrateMPU() {
  Serial.println("Calibrando... mantenha o sensor completamente parado.");
  long sumX = 0, sumY = 0, sumZ = 0;
  const int N = 500;
  for (int i = 0; i < N; i++) {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    sumX += ax; sumY += ay; sumZ += az;
    delay(2);
  }
  offsetAX = sumX / (float)N / 16384.0;
  offsetAY = sumY / (float)N / 16384.0;
  offsetAZ = sumZ / (float)N / 16384.0 - 1.0;  // assume Z apontando para cima (~+1g) na calibração
  Serial.print("Offset X="); Serial.print(offsetAX, 4);
  Serial.print(" Y=");        Serial.print(offsetAY, 4);
  Serial.print(" Z=");        Serial.println(offsetAZ, 4);
  Serial.println("Calibracao concluida!");
}

// --- GOERTZEL (normalizado: retorna amplitude em g na frequência 'freq') ---
// 'start' permite ler o buffer circular em ordem cronológica (janela deslizante).
float goertzelBand(float* buffer, float freq, float mean, int start) {
  float omega = 2.0 * PI * freq / SAMPLE_RATE;
  float coeff = 2.0 * cos(omega);
  float s_prev = 0, s_prev2 = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    float x = buffer[(start + i) % BUFFER_SIZE] - mean;
    float s = x + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev  = s;
  }
  float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
  // Normaliza por (N/2): para um seno de amplitude A no bin, |X| ≈ (N/2)*A
  return sqrt(fabs(power)) / (BUFFER_SIZE / 2.0);
}

float calcMean(float* buffer) {
  float m = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) m += buffer[i];
  return m / BUFFER_SIZE;
}

// --- DETECÇÃO DE TREMOR ---
// Usa o PICO das bandas (amplitude da freq dominante em g), não a média,
// para o valor retornado ter significado físico direto.
float tremorRatio(float* buffer, int start) {
  float mean = calcMean(buffer);

  // Movimento voluntário: baixas frequências (< ~2,5 Hz)
  float voluntaryPeak = 0;
  float freqsVoluntary[] = {0.5, 1.0, 1.5, 2.0, 2.5};
  for (int i = 0; i < 5; i++) {
    float v = goertzelBand(buffer, freqsVoluntary[i], mean, start);
    if (v > voluntaryPeak) voluntaryPeak = v;
  }

  // Tremor de Parkinson: 3,5-7 Hz (literatura: 3-7 Hz, pico modal 4-6 Hz)
  float tremorPeak = 0;
  float freqsTremor[] = {3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0};
  for (int i = 0; i < 8; i++) {
    float v = goertzelBand(buffer, freqsTremor[i], mean, start);
    if (v > tremorPeak) tremorPeak = v;
  }

  if (voluntaryPeak < 0.001) voluntaryPeak = 0.001;

  float dominance = tremorPeak / (tremorPeak + voluntaryPeak);

  if (dominance < DOMINANCE_MIN || tremorPeak < TREMOR_MIN_G) return 0.0;
  return tremorPeak;  // amplitude (g) da frequência de tremor dominante
}

// --- PWM COM HISTERESE (só LEDs, índice lógico 0-4) ---
void setDevice(int idx, float intensidade, float maxVal) {
  float ratio = constrain(intensidade / maxVal, 0.0, 1.0);
  if (ratio == 0.0 && lastPWM[idx] == 0.0) return;
  if (fabs(ratio - lastPWM[idx]) < 0.05) return;
  lastPWM[idx] = ratio;
  ledcWrite(PINOS[idx], (int)(ratio * 255));
}

// --- EMBARALHA A ORDEM DOS DEDOS E APLICA JITTER TEMPORAL (CR) ---
void shuffleCR() {
  for (int i = 4; i > 0; i--) {            // Fisher-Yates
    int j = random(i + 1);
    int t = crOrder[i]; crOrder[i] = crOrder[j]; crOrder[j] = t;
  }
  for (int i = 0; i < 5; i++) crJitter[i] = random(-20, 21);  // ±20 ms
}

// --- PADRÃO COORDINATED RESET NOS 5 MOTORES ---
void updateCR(bool active) {
  static bool wasActive = false;

  if (!active) {
    if (wasActive) {
      for (int i = 0; i < 5; i++) { ledcWrite(PINOS[MOTOR_IDX[i]], 0); crLast[i] = 0; }
      wasActive = false;
    }
    return;
  }

  unsigned long now = millis();
  if (!wasActive) { crCycleStart = now; shuffleCR(); wasActive = true; }
  if (now - crCycleStart >= CR_CYCLE_MS) { crCycleStart = now; shuffleCR(); }

  long phase = (long)(now - crCycleStart);
  long slot  = (long)(CR_CYCLE_MS / 5);    // ~133 ms por dedo

  for (int s = 0; s < 5; s++) {
    int phys = crOrder[s];                 // qual motor ocupa este slot
    int m    = MOTOR_IDX[phys];
    long start = (long)s * slot + crJitter[s];
    long end   = start + (long)CR_BURST_MS;
    int duty = (phase >= start && phase < end) ? CR_BURST_DUTY : 0;
    if (crLast[phys] != duty) {
      ledcWrite(PINOS[m], duty);
      crLast[phys] = duty;
    }
  }
}

// --- DESLIGA TODOS OS DISPOSITIVOS ---
void desligarTudo() {
  for (int i = 0; i < 10; i++) {
    ledcWrite(PINOS[i], 0);
    lastPWM[i] = 0;
  }
  for (int i = 0; i < 5; i++) crLast[i] = 0;
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  Serial.setTxBufferSize(512);

  // Timeout I2C: 3ms — se o MPU travar, o Wire abandona e continua
  Wire.begin(21, 22);
  Wire.setTimeOut(3);

  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);   // ±2g  -> 16384 LSB/g
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);   // ±250 dps -> 131 LSB/dps

  // LEDs: 5000 Hz (indicadores visuais, luz suave)
  if (!ledcAttach(LED_X,      5000, 8)) Serial.println("Falha LEDC LED_X");
  if (!ledcAttach(LED_Y,      5000, 8)) Serial.println("Falha LEDC LED_Y");
  if (!ledcAttach(LED_Z,      5000, 8)) Serial.println("Falha LEDC LED_Z");
  if (!ledcAttach(LED_TOTAL,  5000, 8)) Serial.println("Falha LEDC LED_TOTAL");
  if (!ledcAttach(LED_REFORC, 5000, 8)) Serial.println("Falha LEDC LED_REFORC");

  // Motores ERM: 20 kHz (PWM inaudível; a inércia do motor filtra para tensão média)
  if (!ledcAttach(MOTOR_X,     20000, 8)) Serial.println("Falha LEDC MOTOR_X");
  if (!ledcAttach(MOTOR_Y,     20000, 8)) Serial.println("Falha LEDC MOTOR_Y");
  if (!ledcAttach(MOTOR_Z,     20000, 8)) Serial.println("Falha LEDC MOTOR_Z");
  if (!ledcAttach(MOTOR_TOTAL, 20000, 8)) Serial.println("Falha LEDC MOTOR_TOTAL");
  if (!ledcAttach(MOTOR_REFOR, 20000, 8)) Serial.println("Falha LEDC MOTOR_REFOR");

  memset(bufferX, 0, sizeof(bufferX));
  memset(bufferY, 0, sizeof(bufferY));
  memset(bufferZ, 0, sizeof(bufferZ));

  // TESTE: acende todos os LEDs em 100% por 3 segundos
  ledcWrite(LED_X, 255);   ledcWrite(LED_Y, 255);   ledcWrite(LED_Z, 255);
  ledcWrite(LED_TOTAL, 255); ledcWrite(LED_REFORC, 255);
  delay(3000);
  ledcWrite(LED_X, 0);     ledcWrite(LED_Y, 0);     ledcWrite(LED_Z, 0);
  ledcWrite(LED_TOTAL, 0); ledcWrite(LED_REFORC, 0);

  // MPU obrigatório: se não responder, sinaliza piscando LED_TOTAL (em vez de travar silenciosamente)
  if (!mpu.testConnection()) {
    Serial.println("ERRO: MPU6050 nao encontrado!");
    while (true) {
      ledcWrite(LED_TOTAL, 255); delay(200);
      ledcWrite(LED_TOTAL, 0);   delay(200);
    }
  }

  randomSeed(esp_random());   // semente para o embaralhamento do CR

  calibrateMPU();

  lastTime   = millis();
  lastSample = millis();
}

// --- LOOP ---
void loop() {
  unsigned long now = millis();

  // Terapia ativa enquanto houve tremor há menos de THERAPY_HOLD_MS
  bool therapyActive = hasDetected && (now - lastTremorDetected < THERAPY_HOLD_MS);
  // Padrão CR roda a cada iteração (timing fino, independente da taxa de amostragem)
  updateCR(therapyActive);

  if (now - lastSample >= (unsigned long)(1000.0 / SAMPLE_RATE)) {
    lastSample = now;

    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    // Falha de leitura I2C (tudo zero): reinicia o barramento e desliga tudo
    if (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0) {
      Wire.end();
      delay(5);
      Wire.begin(21, 22);
      Wire.setTimeOut(3);
      mpu.initialize();
      desligarTudo();
      hasDetected = false;      // encerra a terapia até nova detecção
      lastTime = millis();
      return;
    }

    float dt = (now - lastTime) / 1000.0;
    lastTime = now;
    if (dt <= 0.0 || dt > 0.5) dt = 1.0 / SAMPLE_RATE;

    float axG = ax / 16384.0 - offsetAX;
    float ayG = ay / 16384.0 - offsetAY;
    float azG = az / 16384.0 - offsetAZ;

    float gxDPS = gx / 131.0;
    float gyDPS = gy / 131.0;

    bufferX[bufferIndex] = axG;
    bufferY[bufferIndex] = ayG;
    bufferZ[bufferIndex] = azG;
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
    if (!bufferReady && bufferIndex == 0) bufferReady = true;

    // Telemetria de inclinação (filtro complementar) — não entra na decisão de terapia
    float rollAcc  = atan2(ayG, azG) * 180.0 / PI;
    float pitchAcc = atan2(-axG, sqrt(ayG * ayG + azG * azG)) * 180.0 / PI;
    roll  = ALPHA * (roll  + gxDPS * dt) + (1.0 - ALPHA) * rollAcc;
    pitch = ALPHA * (pitch + gyDPS * dt) + (1.0 - ALPHA) * pitchAcc;

    // Janela deslizante: recalcula a detecção a cada STEP amostras
    stepCounter++;
    if (bufferReady && stepCounter >= STEP) {
      stepCounter = 0;
      int start = bufferIndex;   // posição mais antiga do buffer circular

      tX     = tremorRatio(bufferX, start);
      tY     = tremorRatio(bufferY, start);
      tZ     = tremorRatio(bufferZ, start);
      tTotal = sqrt(tX * tX + tY * tY + tZ * tZ);

      // LEDs: indicação visual proporcional (ótimo para demonstração)
      setDevice(0, tX,     MAX_INTENSITY);        // LED_X
      setDevice(1, tY,     MAX_INTENSITY);        // LED_Y
      setDevice(2, tZ,     MAX_INTENSITY);        // LED_Z
      setDevice(3, tTotal, MAX_INTENSITY);        // LED_TOTAL
      setDevice(4, tTotal, MAX_INTENSITY * 0.8);  // LED_REFORC

      // Aciona a terapia (motores em modo CR) quando há tremor
      if (tTotal > 0.0) {
        hasDetected = true;
        lastTremorDetected = now;
      }
    }

    // Serial não bloqueia nunca. Campos: roll,pitch,tX,tY,tZ,tTotal,terapia(0/1)
    if (Serial.availableForWrite() > 44) {
      Serial.print(roll, 2);   Serial.print(",");
      Serial.print(pitch, 2);  Serial.print(",");
      Serial.print(tX, 4);     Serial.print(",");
      Serial.print(tY, 4);     Serial.print(",");
      Serial.print(tZ, 4);     Serial.print(",");
      Serial.print(tTotal, 4); Serial.print(",");
      Serial.println(therapyActive ? 1 : 0);
    }
  }
}
