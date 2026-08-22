import processing.serial.*;

Serial porta;
boolean semPorta = false;   // true quando nenhuma porta serial foi encontrada

// Índice da porta serial. -1 = detecção automática (prefere ttyUSB/ttyACM/COM);
// defina um número fixo (0,1,2...) se quiser forçar uma porta específica.
final int PORTA_IDX = -1;

// --- ESCALA DOS DADOS ---
// O firmware (Goertzel normalizado) envia amplitudes em unidades de g:
// limiar de detecção = 0.08 g e fundo de escala útil ~0.60 g.
// Formato Serial: roll,pitch,tX,tY,tZ,tTotal,terapia(0/1)
final float LIMIAR_TREMOR = 0.08;  // mesmo limiar de detecção do firmware
final float T_MAX         = 0.6;   // fundo de escala de barras e gráfico

float roll = 0, pitch = 0;
float tX = 0, tY = 0, tZ = 0, tTotal = 0;
float rollS = 0, pitchS = 0;
float tXs = 0, tYs = 0, tZs = 0, tTs = 0;
boolean terapiaAtiva = false;   // vem do 7º campo (modo Coordinated Reset)

int HIST = 200;
float[] histX   = new float[HIST];
float[] histY   = new float[HIST];
float[] histZ   = new float[HIST];
float[] histAll = new float[HIST];
int histIdx = 0;

// --- CORES DA MÃO ---
color PELE       = color(228, 178, 118);
color PELE_CLARA = color(240, 196, 140);
color TREMOR_COR = color(255, 60, 60);

void setup() {
  size(800, 600, P3D);
  textFont(createFont("Monospaced", 12));

  String[] portas = Serial.list();
  println("Portas disponíveis:");
  printArray(portas);

  if (portas.length == 0) {
    println("ERRO: nenhuma porta serial encontrada. Conecte o ESP32.");
    semPorta = true;
    return;
  }

  int idx = (PORTA_IDX >= 0 && PORTA_IDX < portas.length) ? PORTA_IDX : escolhePorta(portas);
  println("Usando porta: " + portas[idx]);
  porta = new Serial(this, portas[idx], 115200);
  porta.bufferUntil('\n');
}

// Detecta automaticamente a porta do ESP32 (ttyUSB/ttyACM no Linux,
// tty.usb/cu.usb no macOS, COM no Windows). Cai no índice 0 se não achar.
int escolhePorta(String[] portas) {
  String[] pistas = {"ttyUSB", "ttyACM", "tty.usb", "cu.usb", "COM"};
  for (String pista : pistas)
    for (int i = 0; i < portas.length; i++)
      if (portas[i].indexOf(pista) >= 0) return i;
  return 0;
}

void draw() {
  background(15, 15, 25);

  // Sem porta serial: mostra aviso em vez de travar
  if (semPorta) {
    fill(255, 90, 90);
    textAlign(CENTER, CENTER);
    text("Nenhuma porta serial encontrada.\nConecte o ESP32 e reinicie o sketch.",
         width / 2, height / 2);
    textAlign(LEFT, BASELINE);
    return;
  }

  // Suavização com tratamento de volta em ±180° (evita o giro "pelo caminho longo")
  rollS  = suavizaAngulo(rollS,  roll,  0.15);
  pitchS = suavizaAngulo(pitchS, pitch, 0.15);
  tXs    += (tX     - tXs) * 0.2;
  tYs    += (tY     - tYs) * 0.2;
  tZs    += (tZ     - tZs) * 0.2;
  tTs    += (tTotal - tTs) * 0.2;

  // ---- Cena 3D (desenhada primeiro; HUD vai por cima no final) ----
  pushMatrix();
  translate(width / 2, height / 2 - 80, 0);

  // Grade do chão: fixa no mundo, não gira com a mão
  stroke(40, 40, 65);
  strokeWeight(1);
  for (int i = -400; i <= 400; i += 50) line(i, 170, -250, i, 170, 250);
  for (int j = -250; j <= 250; j += 50) line(-400, 170, j, 400, 170, j);

  ambientLight(90, 90, 110);
  directionalLight(255, 255, 255, -0.5, 1, -1);

  rotateX(radians(-pitchS));
  rotateZ(radians(-rollS));

  // Vibração visual proporcional ao tremor
  if (tTs > LIMIAR_TREMOR) {
    float shake = map(constrain(tTs, LIMIAR_TREMOR, 1.0), LIMIAR_TREMOR, 1.0, 0.5, 5.0);
    translate(random(-shake, shake), random(-shake, shake), random(-shake, shake));
  }

  desenhaMao();

  // Eixos do corpo (giram junto com a mão)
  strokeWeight(2);
  stroke(255, 80, 80);  line(0, 0, 0, 170, 0, 0);
  stroke(80, 255, 80);  line(0, 0, 0, 0, -150, 0);
  stroke(80, 80, 255);  line(0, 0, 0, 0, 0, 150);

  popMatrix();

  // ---- HUD 2D (por cima da cena 3D) ----
  camera();
  noLights();
  hint(DISABLE_DEPTH_TEST);

  fill(255);
  text("Roll  : " + nf(rollS,  1, 1) + "°", 20, 25);
  text("Pitch : " + nf(pitchS, 1, 1) + "°", 20, 45);

  boolean temTremor = tTs > LIMIAR_TREMOR;
  fill(temTremor ? color(255, 60, 60) : color(60, 255, 120));
  text("Status: " + (temTremor ? "TREMOR DETECTADO" : "Repouso"), 20, 70);

  // Estado da terapia CR (7º campo). Ponto pisca a ~1,5 Hz imitando o ciclo CR.
  boolean crPulse = (millis() % 667) < 100;
  fill(terapiaAtiva ? (crPulse ? color(120, 200, 255) : color(60, 110, 150))
                    : color(90, 90, 110));
  text("Terapia CR: " + (terapiaAtiva ? "ATIVA  " + (crPulse ? "●" : "○") : "—"), 300, 70);

  // Barras por eixo
  desenhaBarraEixo("X (Indicador)", tXs, color(255, 80,  80),  20, 90);
  desenhaBarraEixo("Y (Medio)    ", tYs, color(80,  255, 80),  20, 115);
  desenhaBarraEixo("Z (Anelar)   ", tZs, color(80,  80,  255), 20, 140);
  desenhaBarraEixo("Total(Minimo)", tTs, color(255, 200, 50),  20, 165);

  // Gráfico histórico
  int gx = 20, gy = 430, gw = 760, gh = 80;
  fill(20, 20, 35);
  stroke(50, 50, 80);
  rect(gx, gy, gw, gh, 4);

  desenhaGrafico(histX,   gx, gy, gw, gh, color(255, 80,  80));
  desenhaGrafico(histY,   gx, gy, gw, gh, color(80,  255, 80));
  desenhaGrafico(histZ,   gx, gy, gw, gh, color(80,  80,  255));
  desenhaGrafico(histAll, gx, gy, gw, gh, color(255, 200, 50));

  fill(150); noStroke();
  text("Histórico tremor por eixo (3,5-7 Hz)", gx + 5, gy - 5);

  fill(255, 80, 80);   text("■ X", 20,  height - 40);
  fill(80, 255, 80);   text("■ Y", 60,  height - 40);
  fill(80, 80, 255);   text("■ Z", 100, height - 40);
  fill(255, 200, 50);  text("■ Total", 140, height - 40);

  hint(ENABLE_DEPTH_TEST);
}

// ================= MÃO 3D =================
// Mão direita deitada, palma para baixo, dedos apontando para +X,
// polegar para o lado +Z. Cada dedo fica vermelho conforme o tremor
// do canal correspondente (mesmo mapeamento das barras do HUD).
void desenhaMao() {
  noStroke();

  // Punho
  fill(PELE_CLARA);
  pushMatrix();
  translate(-100, 0, 0);
  box(55, 26, 68);
  popMatrix();

  // Palma
  fill(PELE);
  pushMatrix();
  box(145, 22, 100);
  popMatrix();

  // Dedos: posição Z na borda da palma, comprimento e canal de tremor
  desenhaDedo(-36, 62, norm01(tTs));  // mínimo   <- Total
  desenhaDedo(-12, 80, norm01(tZs));  // anelar   <- Z
  desenhaDedo( 12, 86, norm01(tYs));  // médio    <- Y
  desenhaDedo( 36, 74, norm01(tXs));  // indicador<- X

  // Polegar: 2 falanges, saindo da lateral da palma em diagonal
  pushMatrix();
  translate(30, 0, 52);
  rotateY(radians(-48));
  fill(PELE);
  float[] segPol = {34, 30};
  for (int s = 0; s < 2; s++) {
    rotateZ(radians(6));
    translate(segPol[s] / 2, 0, 0);
    box(segPol[s], 16, 18);
    translate(segPol[s] / 2, 0, 0);
  }
  popMatrix();
}

// Um dedo com 3 falanges que se curvam levemente para baixo.
// 'intensidade' (0-1) pinta o dedo de vermelho e aumenta a curvatura.
void desenhaDedo(float baseZ, float comprimento, float intensidade) {
  color cor = lerpColor(PELE, TREMOR_COR, intensidade);
  float curva = radians(7 + intensidade * 22);
  float[] prop = {0.42, 0.32, 0.26};

  pushMatrix();
  translate(70, -1, baseZ);
  fill(cor);
  for (int s = 0; s < 3; s++) {
    rotateZ(curva);
    float seg = comprimento * prop[s];
    translate(seg / 2, 0, 0);
    box(seg, 13, 16);
    translate(seg / 2, 0, 0);
  }
  popMatrix();
}

// ================= AUXILIARES =================
float norm01(float v) {
  return constrain(map(v, 0, T_MAX, 0, 1), 0, 1);
}

// Interpola ângulos pelo caminho mais curto (trata a descontinuidade em ±180°)
float suavizaAngulo(float atual, float alvo, float fator) {
  float d = ((alvo - atual + 540) % 360) - 180;
  return atual + d * fator;
}

void desenhaBarraEixo(String label, float val, color cor, int x, int y) {
  float w = map(constrain(val, 0, T_MAX), 0, T_MAX, 0, 300);
  fill(40, 40, 60); noStroke();
  rect(x + 120, y, 300, 14, 3);
  fill(cor);
  rect(x + 120, y, w, 14, 3);
  // Marca do limiar de detecção do firmware
  stroke(255, 255, 255, 120);
  float xLim = x + 120 + map(LIMIAR_TREMOR, 0, T_MAX, 0, 300);
  line(xLim, y, xLim, y + 14);
  fill(200); noStroke();
  text(label + " " + nf(val, 1, 3), x, y + 12);
}

void desenhaGrafico(float[] hist, int gx, int gy, int gw, int gh, color cor) {
  noFill();
  stroke(cor);
  strokeWeight(1.2);
  beginShape();
  for (int i = 0; i < HIST; i++) {
    int idx = (histIdx + i) % HIST;
    float x = gx + map(i, 0, HIST, 0, gw);
    float y = gy + gh - map(hist[idx], 0, T_MAX, 0, gh);
    y = constrain(y, gy, gy + gh);
    vertex(x, y);
  }
  endShape();
}

void serialEvent(Serial p) {
  String linha = p.readStringUntil('\n');
  if (linha == null) return;
  linha = trim(linha);
  String[] v = split(linha, ',');
  // Tolerante: 6 campos (firmware antigo) ou 7 (novo, com flag de terapia CR)
  if (v.length < 6) return;

  float r  = float(v[0]);
  float pt = float(v[1]);
  float x  = float(v[2]);
  float y  = float(v[3]);
  float z  = float(v[4]);
  float t  = float(v[5]);

  // float() do Processing devolve NaN em linha corrompida (não lança exceção).
  // Um único NaN contaminaria a suavização para sempre — descarta a linha.
  if (Float.isNaN(r) || Float.isNaN(pt) || Float.isNaN(x) ||
      Float.isNaN(y) || Float.isNaN(z)  || Float.isNaN(t)) return;

  roll = r;  pitch = pt;
  tX = x;  tY = y;  tZ = z;  tTotal = t;

  // 7º campo (opcional): 1 = terapia Coordinated Reset ativa
  terapiaAtiva = (v.length >= 7) ? (int(trim(v[6])) == 1) : (tTotal > 0.0);

  histX[histIdx]   = tX;
  histY[histIdx]   = tY;
  histZ[histIdx]   = tZ;
  histAll[histIdx] = tTotal;
  histIdx = (histIdx + 1) % HIST;
}
