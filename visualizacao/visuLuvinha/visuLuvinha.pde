import processing.serial.*;

// ============================================================================
// visuLuvinha - visualizacao da luva ParkinSense
//
// Formato recebido pela serial, 50 linhas por segundo:
//   roll,pitch,tX,tY,tZ,tTotal,estado,dominancia,nitidez
//
// O que cada campo significa:
//   tX, tY, tZ  EIXOS DO ACELEROMETRO, nao dedos. A luva tem um unico MPU6050:
//               mede o tremor da mao inteira e nao sabe qual dedo treme.
//   tTotal      modulo do vetor (X,Y,Z) - a "forca" do tremor.
//   estado      0 = medindo, leitura ao vivo
//               1 = terapia rodando (leitura congelada)
//               2 = medindo, janela ainda enchendo (valor exibido e' o antigo)
//   dominancia  quanto da energia esta em 3,5-7 Hz e nao em 0,5-3 Hz
//   nitidez     pico da faixa dividido pela media da faixa
//
// O contra-estimulo so liga com os TRES criterios satisfeitos ao mesmo tempo.
// Este painel mostra os tres lado a lado justamente para deixar claro qual
// esta faltando quando os motores nao entram.
// ============================================================================

Serial porta;
boolean semPorta = false;

// Indice da porta serial. -1 = deteccao automatica.
final int PORTA_IDX = -1;

// --- LIMIARES (espelham o .ino) ---
final float LIMIAR_FORCA   = 0.08;   // TREMOR_MIN_G
final float LIMIAR_PUREZA  = 0.50;   // DOMINANCE_MIN
final float LIMIAR_NITIDEZ = 3.50;   // NITIDEZ_MIN
final float T_MAX          = 1.00;   // fundo de escala das barras de forca
final int   SEM_DADOS_MS   = 1000;

// --- LAYOUT ---
final int LARG_PAINEL = 300;         // faixa da esquerda com os numeros
final int ALT_GRAFICO = 92;

// --- estados vindos do firmware ---
final int MEDINDO = 0, TERAPIA = 1, ENCHENDO = 2;

// ---------------------------------------------------------------------------
// ESTADO COMPARTILHADO ENTRE AS DUAS THREADS
// serialEvent() roda na thread da serial e draw() na thread do sketch. Tudo
// que cruza as threads passa por aqui, sempre sob o mesmo lock.
// ---------------------------------------------------------------------------
final Object trava = new Object();

float sRoll, sPitch, sX, sY, sZ, sT, sDom, sNit;
int sEstado = MEDINDO;
int sUltimaLinha = 0, sLinhasOk = 0, sLinhasRuins = 0;

final int HIST = 200;                // 200 amostras a 50 Hz = 4 s
float[] sHistT = new float[HIST];
boolean[] sHistTerapia = new boolean[HIST];
int sHistIdx = 0;

// --- copias locais do draw ---
float roll, pitch, tX, tY, tZ, tTotal, pureza, nitidez;
int estado = MEDINDO;
int ultimaLinha = 0, linhasOk = 0, linhasRuins = 0;
float[] histT = new float[HIST];
boolean[] histTerapia = new boolean[HIST];
int histIdx = 0;

// --- suavizados, so para animacao ---
float rollS = 0, pitchS = 0, tTs = 0;

// --- CORES ---
color FUNDO       = color(14, 14, 22);
color FUNDO_PAINEL= color(22, 22, 34);
color PELE        = color(228, 178, 118);
color PELE_CLARA  = color(240, 196, 140);
color TREMOR_COR  = color(255, 70, 70);
color VERDE       = color(70, 220, 130);
color AMBAR       = color(255, 175, 60);
color AZUL        = color(120, 200, 255);
color TEXTO       = color(228, 228, 238);
color FRACO       = color(130, 130, 152);
color COR_X       = color(255, 95, 95);
color COR_Y       = color(95, 230, 130);
color COR_Z       = color(115, 155, 255);
color COR_TOTAL   = color(255, 200, 60);

final String[] CANAIS = {
  "Polegar", "Indicador", "Nervo frente", "Nervo tras", "Mindinho"
};

void setup() {
  size(1100, 700, P3D);
  textFont(createFont("Monospaced", 12));

  String[] portas = Serial.list();
  println("Portas disponiveis:");
  printArray(portas);

  if (portas.length == 0) {
    println("ERRO: nenhuma porta serial encontrada. Conecte o ESP32.");
    semPorta = true;
    return;
  }

  int idx = (PORTA_IDX >= 0 && PORTA_IDX < portas.length) ? PORTA_IDX : escolhePorta(portas);
  println("Usando porta: " + portas[idx]);

  // Abrir a porta pode falhar (ocupada pelo Monitor Serial do Arduino IDE,
  // sem permissao no grupo dialout...). Sem o try o sketch morre com stack trace.
  try {
    porta = new Serial(this, portas[idx], 115200);
    porta.clear();
    porta.bufferUntil('\n');
  } catch (Exception e) {
    println("ERRO ao abrir " + portas[idx] + ": " + e.getMessage());
    println("A porta esta ocupada? Feche o Monitor Serial do Arduino IDE.");
    semPorta = true;
  }
}

int escolhePorta(String[] portas) {
  String[] pistas = {"ttyUSB", "ttyACM", "tty.usb", "cu.usb", "COM"};
  for (String pista : pistas)
    for (int i = 0; i < portas.length; i++)
      if (portas[i].indexOf(pista) >= 0) return i;
  return 0;
}

// ============================================================================
void draw() {
  background(FUNDO);

  if (semPorta) { avisoSemPorta(); return; }

  copiaEstado();
  boolean conectado = (millis() - ultimaLinha) < SEM_DADOS_MS && linhasOk > 0;

  rollS  = suavizaAngulo(rollS,  roll,  0.15);
  pitchS = suavizaAngulo(pitchS, pitch, 0.15);
  tTs   += (tTotal - tTs) * 0.2;

  cena3D(conectado);

  camera();
  noLights();
  hint(DISABLE_DEPTH_TEST);
  textAlign(LEFT, BASELINE);
  painelEsquerda(conectado);
  grafico();
  hint(ENABLE_DEPTH_TEST);
}

void copiaEstado() {
  synchronized (trava) {
    roll = sRoll;  pitch = sPitch;
    tX = sX;  tY = sY;  tZ = sZ;  tTotal = sT;
    pureza = sDom;  nitidez = sNit;
    estado = sEstado;
    ultimaLinha = sUltimaLinha;
    linhasOk = sLinhasOk;  linhasRuins = sLinhasRuins;
    histIdx = sHistIdx;
    arrayCopy(sHistT, histT);
    arrayCopy(sHistTerapia, histTerapia);
  }
}

// ============================================================================
// CENA 3D  - ocupa toda a area a direita do painel
// ============================================================================
void cena3D(boolean conectado) {
  int cx = LARG_PAINEL + (width - LARG_PAINEL) / 2;
  int cy = (height - ALT_GRAFICO - 30) / 2;

  pushMatrix();
  translate(cx, cy, 0);

  stroke(34, 34, 54);
  strokeWeight(1);
  for (int i = -400; i <= 400; i += 50) line(i, 190, -250, i, 190, 250);
  for (int j = -250; j <= 250; j += 50) line(-400, 190, j, 400, 190, j);

  ambientLight(90, 90, 110);
  directionalLight(255, 255, 255, -0.5, 1, -1);

  rotateX(radians(-pitchS));
  rotateZ(radians(-rollS));

  // Vibracao visual. So treme quando a leitura e' AO VIVO: durante a terapia e
  // durante o reenchimento da janela o valor esta congelado, e fazer a mao
  // tremer com um numero velho passava a impressao de que a luva estava
  // medindo tremor com a mao parada.
  // A amplitude tambem e' modesta de proposito - antes ia a 6 px e o modelo
  // sacudia muito mais do que a mao real.
  if (conectado && estado == MEDINDO && tTs > LIMIAR_FORCA) {
    float s = map(constrain(tTs, LIMIAR_FORCA, T_MAX), LIMIAR_FORCA, T_MAX, 0.3, 2.5);
    translate(random(-s, s), random(-s, s), random(-s, s));
  }

  desenhaMao(conectado);

  strokeWeight(2);
  stroke(COR_X);  line(0, 0, 0, 180, 0, 0);
  stroke(COR_Y);  line(0, 0, 0, 0, -160, 0);
  stroke(COR_Z);  line(0, 0, 0, 0, 0, 160);
  strokeWeight(1);

  popMatrix();
}

// A mao inteira e' colorida por tTotal: com um unico acelerometro nao da para
// atribuir tremor a um dedo especifico, entao colorir dedo a dedo seria
// inventar informacao que a luva nao tem.
void desenhaMao(boolean conectado) {
  noStroke();

  boolean aoVivo = conectado && estado == MEDINDO;
  float intensidade = aoVivo ? norm01(tTs) : 0;
  color corMao   = lerpColor(PELE, TREMOR_COR, intensidade);
  color corPunho = lerpColor(PELE_CLARA, TREMOR_COR, intensidade * 0.7);
  if (!conectado) { corMao = color(88, 88, 100); corPunho = color(104, 104, 118); }
  if (estado == TERAPIA) {                      // terapia: mao marcada em azul
    corMao   = lerpColor(PELE, AZUL, 0.45);
    corPunho = lerpColor(PELE_CLARA, AZUL, 0.35);
  }

  fill(corPunho);
  pushMatrix(); translate(-110, 0, 0); box(60, 28, 74); popMatrix();

  fill(corMao);
  box(160, 24, 110);

  float curva = radians(7 + intensidade * 22);
  desenhaDedo(-40, 68, corMao, curva);
  desenhaDedo(-13, 88, corMao, curva);
  desenhaDedo( 13, 95, corMao, curva);
  desenhaDedo( 40, 82, corMao, curva);

  pushMatrix();
  translate(33, 0, 57);
  rotateY(radians(-48));
  fill(corMao);
  float[] segPol = {37, 33};
  for (int s = 0; s < 2; s++) {
    rotateZ(radians(6));
    translate(segPol[s] / 2, 0, 0);
    box(segPol[s], 17, 20);
    translate(segPol[s] / 2, 0, 0);
  }
  popMatrix();
}

void desenhaDedo(float baseZ, float comprimento, color cor, float curva) {
  float[] prop = {0.42, 0.32, 0.26};
  pushMatrix();
  translate(78, -1, baseZ);
  fill(cor);
  for (int s = 0; s < 3; s++) {
    rotateZ(curva);
    float seg = comprimento * prop[s];
    translate(seg / 2, 0, 0);
    box(seg, 14, 17);
    translate(seg / 2, 0, 0);
  }
  popMatrix();
}

// ============================================================================
// PAINEL DA ESQUERDA
// ============================================================================
void painelEsquerda(boolean conectado) {
  noStroke();
  fill(FUNDO_PAINEL);
  rect(0, 0, LARG_PAINEL, height - ALT_GRAFICO - 26);

  int x = 18, y = 30;

  // --- conexao ---
  if (!conectado) {
    fill(255, 120, 60);
    text("SEM DADOS", x, y);
    y += 15;
    fill(FRACO);
    text("verifique o cabo ou o reset", x, y);
  } else {
    fill(FRACO);
    text(linhasOk + " linhas" + (linhasRuins > 0 ? "  (" + linhasRuins + " ign.)" : ""), x, y);
  }
  y += 30;

  // --- inclinacao ---
  fill(TEXTO);
  text("roll  " + nf(rollS, 1, 1), x, y);
  text("pitch " + nf(pitchS, 1, 1), x + 140, y);
  y += 28;

  // --- estado do ciclo ---
  desenhaEstado(x, y);
  y += 44;

  // --- os tres criterios de disparo ---
  fill(FRACO);
  text("CRITERIOS DE DISPARO", x, y);
  y += 16;

  boolean vivo = conectado && estado != ENCHENDO;
  criterio(x, y, "forca",   tTotal,  LIMIAR_FORCA,   T_MAX, "g", vivo); y += 34;
  criterio(x, y, "pureza",  pureza,  LIMIAR_PUREZA,  1.0,   "",  vivo); y += 34;
  criterio(x, y, "nitidez", nitidez, LIMIAR_NITIDEZ, 8.0,   "",  vivo); y += 40;

  desenhaVeredito(x, y, conectado);
  y += 46;

  // --- eixos ---
  fill(FRACO);
  text("EIXOS DO ACELEROMETRO", x, y);
  y += 8;
  barraEixo(x, y, "X", tX, COR_X, vivo); y += 20;
  barraEixo(x, y, "Y", tY, COR_Y, vivo); y += 20;
  barraEixo(x, y, "Z", tZ, COR_Z, vivo); y += 28;

  // --- canais ---
  fill(FRACO);
  text("CANAIS (motor + LED)", x, y);
  y += 16;
  for (int i = 0; i < CANAIS.length; i++) {
    fill(estado == TERAPIA ? AZUL : color(74, 74, 92));
    ellipse(x + 5, y - 4, 8, 8);
    fill(estado == TERAPIA ? TEXTO : FRACO);
    text(CANAIS[i], x + 18, y);
    y += 17;
  }
}

void desenhaEstado(int x, int y) {
  String titulo, detalhe;
  color cor;
  if (estado == TERAPIA) {
    cor = AZUL;  titulo = "CONTRA-ESTIMULO ATIVO";
    detalhe = "motores vibrando - leitura congelada";
  } else if (estado == ENCHENDO) {
    cor = AMBAR; titulo = "ATUALIZANDO JANELA";
    detalhe = "valores abaixo sao da medida anterior";
  } else {
    cor = VERDE; titulo = "MEDINDO";
    detalhe = "motores parados - leitura ao vivo";
  }
  fill(cor);   text(titulo, x, y);
  fill(FRACO); text(detalhe, x, y + 15);
}

// Uma linha por criterio: marcador, nome, valor e barrinha com o limiar.
void criterio(int x, int y, String nome, float valor, float limiar,
              float escala, String unidade, boolean vivo) {
  boolean ok = vivo && valor >= limiar;

  fill(ok ? VERDE : (vivo ? FRACO : color(80, 80, 96)));
  text(ok ? "[ok]" : "[  ]", x, y);
  fill(vivo ? TEXTO : FRACO);
  text(nome, x + 40, y);
  text(nf(valor, 1, unidade.equals("g") ? 3 : 2) + unidade, x + 130, y);
  fill(FRACO);
  text("min " + nf(limiar, 1, 2), x + 205, y);

  final int LARG = 264, ALT = 7;
  int by = y + 7;
  noStroke();
  fill(36, 36, 54);
  rect(x, by, LARG, ALT, 2);
  fill(ok ? VERDE : (vivo ? color(150, 120, 90) : color(60, 60, 76)));
  rect(x, by, map(constrain(valor, 0, escala), 0, escala, 0, LARG), ALT, 2);
  stroke(255, 255, 255, 120);
  strokeWeight(1);
  float xl = x + map(limiar, 0, escala, 0, LARG);
  line(xl, by - 1, xl, by + ALT + 1);
  noStroke();
}

void desenhaVeredito(int x, int y, boolean conectado) {
  if (!conectado) return;

  if (estado == TERAPIA) {
    fill(AZUL);  text("contra-estimulo em andamento", x, y);
    return;
  }
  if (estado == ENCHENDO) {
    fill(FRACO); text("aguardando janela de 2,5 s", x, y);
    return;
  }

  boolean f = tTotal  >= LIMIAR_FORCA;
  boolean p = pureza  >= LIMIAR_PUREZA;
  boolean n = nitidez >= LIMIAR_NITIDEZ;

  if (f && p && n) { fill(TREMOR_COR); text("TREMOR DETECTADO", x, y); return; }

  fill(VERDE);
  text("sem tremor", x, y);
  fill(FRACO);
  // Diz o que faltou, na ordem em que costuma faltar na bancada.
  if (!f)      text("tremor fraco demais", x, y + 15);
  else if (!p) text("movimento lento junto (punho girando?)", x, y + 15);
  else if (!n) text("parece impacto, nao oscilacao (digitar?)", x, y + 15);
}

void barraEixo(int x, int y, String rot, float v, color cor, boolean vivo) {
  final int LARG = 180, ALT = 9;
  int bx = x + 22;
  noStroke();
  fill(36, 36, 54);
  rect(bx, y - 8, LARG, ALT, 2);
  fill(vivo ? cor : lerpColor(cor, FUNDO_PAINEL, 0.6));
  rect(bx, y - 8, map(constrain(v, 0, T_MAX), 0, T_MAX, 0, LARG), ALT, 2);
  fill(vivo ? TEXTO : FRACO);
  text(rot, x, y);
  text(nf(v, 1, 3), bx + LARG + 8, y);
}

// ============================================================================
void grafico() {
  int gx = 18, gy = height - ALT_GRAFICO - 10, gw = width - 36, gh = ALT_GRAFICO - 26;

  fill(20, 20, 33);
  stroke(46, 46, 74);
  strokeWeight(1);
  rect(gx, gy, gw, gh, 4);

  noStroke();
  fill(AZUL, 26);
  for (int i = 0; i < HIST; i++)
    if (histTerapia[(histIdx + i) % HIST])
      rect(gx + map(i, 0, HIST, 0, gw), gy + 1, gw / (float) HIST + 1, gh - 2);

  stroke(255, 255, 255, 55);
  float yl = gy + gh - map(LIMIAR_FORCA, 0, T_MAX, 0, gh);
  line(gx, yl, gx + gw, yl);

  noFill();
  stroke(COR_TOTAL);
  strokeWeight(1.4);
  beginShape();
  for (int i = 0; i < HIST; i++) {
    int idx = (histIdx + i) % HIST;
    vertex(gx + map(i, 0, HIST, 0, gw),
           gy + gh - map(constrain(histT[idx], 0, T_MAX), 0, T_MAX, 0, gh));
  }
  endShape();
  strokeWeight(1);

  noStroke();
  fill(FRACO);
  text("forca do tremor, ultimos 4 s   -   faixa azul = motores ligados, linha branca = limiar " +
       nf(LIMIAR_FORCA, 1, 2) + " g   -   escala 0 a " + nf(T_MAX, 1, 2) + " g",
       gx + 4, gy + gh + 16);
}

void avisoSemPorta() {
  fill(255, 90, 90);
  textAlign(CENTER, CENTER);
  text("Nenhuma porta serial disponivel.\n\n" +
       "Conecte o ESP32, feche o Monitor Serial do Arduino IDE\n" +
       "e reinicie este sketch.", width / 2, height / 2);
  textAlign(LEFT, BASELINE);
}

// ============================================================================
float norm01(float v) { return constrain(map(v, 0, T_MAX, 0, 1), 0, 1); }

// Interpola angulos pelo caminho mais curto (trata a descontinuidade em +-180)
float suavizaAngulo(float atual, float alvo, float fator) {
  float d = ((alvo - atual + 540) % 360) - 180;
  return atual + d * fator;
}

// ============================================================================
// SERIAL  (thread da serial - so toca no bloco protegido pelo lock)
// ============================================================================
void serialEvent(Serial p) {
  String linha = p.readStringUntil('\n');
  if (linha == null) return;
  linha = trim(linha);
  if (linha.length() == 0) return;

  String[] v = split(linha, ',');
  // O firmware manda 9 campos. Linhas menores sao mensagens de boot,
  // avisos de calibracao ou pacote cortado.
  if (v.length < 9) { synchronized (trava) { sLinhasRuins++; } return; }

  float r  = float(v[0]);
  float pt = float(v[1]);
  float x  = float(v[2]);
  float y  = float(v[3]);
  float z  = float(v[4]);
  float t  = float(v[5]);
  float dm = float(v[7]);
  float nt = float(v[8]);

  // float() do Processing devolve NaN em linha corrompida (nao lanca excecao).
  // Um unico NaN contaminaria a suavizacao para sempre - descarta a linha.
  if (Float.isNaN(r) || Float.isNaN(pt) || Float.isNaN(x) || Float.isNaN(y) ||
      Float.isNaN(z) || Float.isNaN(t)  || Float.isNaN(dm) || Float.isNaN(nt)) {
    synchronized (trava) { sLinhasRuins++; }
    return;
  }

  int est = MEDINDO;
  String e = trim(v[6]);
  if (e.equals("1")) est = TERAPIA;
  else if (e.equals("2")) est = ENCHENDO;

  synchronized (trava) {
    sRoll = r;  sPitch = pt;
    sX = x;  sY = y;  sZ = z;  sT = t;
    sDom = dm;  sNit = nt;
    sEstado = est;
    sUltimaLinha = millis();
    sLinhasOk++;

    sHistT[sHistIdx] = t;
    sHistTerapia[sHistIdx] = (est == TERAPIA);
    sHistIdx = (sHistIdx + 1) % HIST;
  }
}
