#include <Wire.h>
#include <MPU6050.h>

MPU6050 mpu;

// --- PINOS FÍSICOS ---
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
const float ALPHA       = 0.96;
const int   BUFFER_SIZE = 64;
const float SAMPLE_RATE = 50.0;

float bufferX[BUFFER_SIZE];
float bufferY[BUFFER_SIZE];
float bufferZ[BUFFER_SIZE];
int   bufferIndex = 0;
bool  bufferReady = false;   // true após o primeiro ciclo completo de 64 amostras

float roll  = 0.0;
float pitch = 0.0;
unsigned long lastTime   = 0;
unsigned long lastSample = 0;

float offsetAX = 0, offsetAY = 0, offsetAZ = 0;

// Valores de tremor persistem entre ciclos (enviados pelo Serial a cada amostra)
float tX = 0, tY = 0, tZ = 0, tTotal = 0;

// lastPWM indexado por posição lógica (0-9) em vez de número de pino
// ordem: LED_X, LED_Y, LED_Z, LED_TOTAL, LED_REFORC,
//        MOTOR_X, MOTOR_Y, MOTOR_Z, MOTOR_TOTAL, MOTOR_REFOR
float lastPWM[10] = {0};

const int PINOS[10] = {
  LED_X, LED_Y, LED_Z, LED_TOTAL, LED_REFORC,
  MOTOR_X, MOTOR_Y, MOTOR_Z, MOTOR_TOTAL, MOTOR_REFOR
};

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
  offsetAZ = sumZ / (float)N / 16384.0 - 1.0;
  Serial.print("Offset X="); Serial.print(offsetAX, 4);
  Serial.print(" Y=");        Serial.print(offsetAY, 4);
  Serial.print(" Z=");        Serial.println(offsetAZ, 4);
  Serial.println("Calibracao concluida!");
}

// --- GOERTZEL ---
float goertzelBand(float* buffer, float freq, float mean) {
  float omega = 2.0 * PI * freq / SAMPLE_RATE;
  float coeff = 2.0 * cos(omega);
  float s_prev = 0, s_prev2 = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    float s = (buffer[i] - mean) + coeff * s_prev - s_prev2;
    s_prev2 = s_prev;
    s_prev  = s;
  }
  float power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
  return sqrt(abs(power));
}

float calcMean(float* buffer) {
  float m = 0;
  for (int i = 0; i < BUFFER_SIZE; i++) m += buffer[i];
  return m / BUFFER_SIZE;
}

// --- DETECÇÃO DE TREMOR ---
float tremorRatio(float* buffer) {
  float mean = calcMean(buffer);

  float voluntaryEnergy = 0;
  float freqsVoluntary[] = {0.5, 1.0, 1.5, 2.0, 2.5};
  for (int i = 0; i < 5; i++)
    voluntaryEnergy += goertzelBand(buffer, freqsVoluntary[i], mean);
  voluntaryEnergy /= 5.0;

  float tremorEnergy = 0;
  float freqsTremor[] = {4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0};
  for (int i = 0; i < 7; i++)
    tremorEnergy += goertzelBand(buffer, freqsTremor[i], mean);
  tremorEnergy /= 7.0;

  if (voluntaryEnergy < 0.001) voluntaryEnergy = 0.001;

  float dominance = tremorEnergy / (tremorEnergy + voluntaryEnergy);
  float magnitude = tremorEnergy;

  if (dominance < 0.75 || magnitude < 0.15) return 0.0;
  return magnitude;
}

// --- PWM COM HISTERESE (índice lógico 0-9) ---
void setDevice(int idx, float intensidade, float maxVal) {
  float ratio = constrain(intensidade / maxVal, 0.0, 1.0);
  if (ratio == 0.0 && lastPWM[idx] == 0.0) return;
  if (abs(ratio - lastPWM[idx]) < 0.05) return;
  lastPWM[idx] = ratio;
  ledcWrite(PINOS[idx], (int)(ratio * 255));
}

// --- DESLIGA TODOS OS DISPOSITIVOS ---
void desligarTudo() {
  for (int i = 0; i < 10; i++) {
    ledcWrite(PINOS[i], 0);
    lastPWM[i] = 0;
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  Serial.setTxBufferSize(512);

  // Timeout I2C: 3ms — se o MPU travar, o Wire abandona e continua
  Wire.begin(21, 22);
  Wire.setTimeOut(3);         // milissegundos — CORREÇÃO DO TRAVAMENTO PRINCIPAL

  mpu.initialize();
  mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
  mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);

  if (!mpu.testConnection()) {
    Serial.println("ERRO: MPU6050 nao encontrado!");
    while (true);
  }

  // LEDs: 5000 Hz
  ledcAttach(LED_X,      5000, 8);
  ledcAttach(LED_Y,      5000, 8);
  ledcAttach(LED_Z,      5000, 8);
  ledcAttach(LED_TOTAL,  5000, 8);
  ledcAttach(LED_REFORC, 5000, 8);

  // Motores ERM: 100 Hz
  ledcAttach(MOTOR_X,     100, 8);
  ledcAttach(MOTOR_Y,     100, 8);
  ledcAttach(MOTOR_Z,     100, 8);
  ledcAttach(MOTOR_TOTAL, 100, 8);
  ledcAttach(MOTOR_REFOR, 100, 8);

  memset(bufferX, 0, sizeof(bufferX));
  memset(bufferY, 0, sizeof(bufferY));
  memset(bufferZ, 0, sizeof(bufferZ));

  // TESTE: acende todos os LEDs em 100% por 3 segundos
  ledcWrite(LED_X, 255);
  ledcWrite(LED_Y, 255);
  ledcWrite(LED_Z, 255);
  ledcWrite(LED_TOTAL, 255);
  ledcWrite(LED_REFORC, 255);
  delay(3000);
  ledcWrite(LED_X, 0);
  ledcWrite(LED_Y, 0);
  ledcWrite(LED_Z, 0);
  ledcWrite(LED_TOTAL, 0);
  ledcWrite(LED_REFORC, 0);
  
  calibrateMPU();

  lastTime   = millis();
  lastSample = millis();
}

// --- LOOP ---
void loop() {
  unsigned long now = millis();

  if (now - lastSample >= (unsigned long)(1000.0 / SAMPLE_RATE)) {
    lastSample = now;

    int16_t ax, ay, az, gx, gy, gz;
    // Tenta ler o sensor; se todos os valores forem zero, houve falha I2C
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    if (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0) {
      // Falha de leitura: reinicia I2C, desliga tudo e aguarda próximo ciclo
      Wire.end();
      delay(5);
      Wire.begin(21, 22);
      Wire.setTimeOut(3);
      mpu.initialize();
      desligarTudo();
      lastTime = millis();
      return;
    }

    // dt protegido contra zero e valores absurdos
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

    // bufferReady fica true após o primeiro ciclo completo e nunca volta a false
    if (!bufferReady && bufferIndex == 0) bufferReady = true;

    float rollAcc  = atan2(ayG, azG) * 180.0 / PI;
    float pitchAcc = atan2(-axG, sqrt(ayG * ayG + azG * azG)) * 180.0 / PI;
    roll  = ALPHA * (roll  + gxDPS * dt) + (1.0 - ALPHA) * rollAcc;
    pitch = ALPHA * (pitch + gyDPS * dt) + (1.0 - ALPHA) * pitchAcc;

    // Goertzel: só quando buffer está cheio E acabou de completar um ciclo
    if (bufferReady && bufferIndex == 0) {
      tX     = tremorRatio(bufferX);
      tY     = tremorRatio(bufferY);
      tZ     = tremorRatio(bufferZ);
      tTotal = sqrt(tX * tX + tY * tY + tZ * tZ);

      const float MAX_INTENSITY = 0.5;

      setDevice(0, tX,     MAX_INTENSITY);        // LED_X
      setDevice(1, tY,     MAX_INTENSITY);        // LED_Y
      setDevice(2, tZ,     MAX_INTENSITY);        // LED_Z
      setDevice(3, tTotal, MAX_INTENSITY);        // LED_TOTAL
      setDevice(4, tTotal, MAX_INTENSITY * 0.8);  // LED_REFORC

      setDevice(5, tX,     MAX_INTENSITY);        // MOTOR_X
      setDevice(6, tY,     MAX_INTENSITY);        // MOTOR_Y
      setDevice(7, tZ,     MAX_INTENSITY);        // MOTOR_Z
      setDevice(8, tTotal, MAX_INTENSITY);        // MOTOR_TOTAL
      setDevice(9, tTotal, MAX_INTENSITY * 0.8);  // MOTOR_REFOR
    }

    // Serial não bloqueia nunca
    if (Serial.availableForWrite() > 40) {
      Serial.print(roll, 2);   Serial.print(",");
      Serial.print(pitch, 2);  Serial.print(",");
      Serial.print(tX, 4);     Serial.print(",");
      Serial.print(tY, 4);     Serial.print(",");
      Serial.print(tZ, 4);     Serial.print(",");
      Serial.println(tTotal, 4);
    }
  }
}
