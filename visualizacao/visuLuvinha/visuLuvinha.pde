import processing.serial.*;

// ============================================================================
// visuLuvinha - visualizacao da luva ParkinSense
//
// Formato recebido pela serial, 50 linhas por segundo:
//   roll,pitch,tX,tY,tZ,tTotal,terapia
//
// IMPORTANTE sobre o que cada campo significa:
//   tX, tY, tZ  sao os EIXOS DO ACELEROMETRO, nao dedos. A luva tem um unico
//               MPU6050, entao ela mede o tremor da mao inteira e nao tem como
//               saber qual dedo esta tremendo. (A versao anterior deste sketch
//               rotulava as barras como "Indicador", "Medio" e "Anelar", o que
//               dava a impressao errada de medicao por dedo.)
//   tTotal      modulo do vetor (X,Y,Z): e' este valor que decide a terapia.
//   terapia     1 enquanto o padrao Coordinated Reset esta rodando.
//
// E sobre quando os valores mudam: durante a terapia o firmware PARA de
// alimentar o buffer de deteccao (blanking), porque o acelerometro estaria
// medindo a vibracao dos proprios motores. Nesse periodo tX/tY/tZ ficam
// CONGELADOS no ultimo valor medido - o painel avisa quando isso acontece.
// ============================================================================

Serial porta;
boolean semPorta = false;

// Indice da porta serial. -1 = deteccao automatica (prefere ttyUSB/ttyACM/COM);
// defina um numero fixo (0,1,2...) para forcar uma porta especifica.
final int PORTA_IDX = -1;

// --- ESCALA DOS DADOS (tem que bater com o firmware) ---
final float LIMIAR_TREMOR = 0.08;   // TREMOR_MIN_G   do .ino
final float LIMIAR_DOM    = 0.50;   // DOMINANCE_MIN  do .ino
final float T_MAX         = 1.00;   // fundo de escala das barras (um sacudir
                                    // forte chega perto de 1 g)
final int   SEM_DADOS_MS  = 1000;   // silencio maior que isso = cabo caiu

// ---------------------------------------------------------------------------
// ESTADO COMPARTILHADO ENTRE AS DUAS THREADS
// serialEvent() roda na thread da serial e draw() na thread do sketch. Antes
// as duas mexiam nas mesmas variaveis sem protecao, o que podia entregar ao
// desenho um quadro com metade dos campos do frame anterior. Tudo que cruza as
// threads agora passa por este bloco, sempre sob o mesmo lock.
// ---------------------------------------------------------------------------
final Object trava = new Object();

float sRoll, sPitch, sX, sY, sZ, sT, sDom;
boolean sTerapia = false;
int sUltimaLinha = 0;         // millis() da ultima linha valida
int sLinhasOk = 0, sLinhasRuins = 0;

final int HIST = 200;         // 200 amostras a 50 Hz = 4 s de historico
float[] sHistX   = new float[HIST];
float[] sHistY   = new float[HIST];
float[] sHistZ   = new float[HIST];
float[] sHistT   = new float[HIST];
boolean[] sHistTerapia = new boolean[HIST];
int sHistIdx = 0;

// --- copias locais do draw (nunca tocadas pela thread da serial) ---
float roll, pitch, tX, tY, tZ, tTotal, dominancia;
boolean terapiaAtiva = false;
int ultimaLinha = 0, linhasOk = 0, linhasRuins = 0;
float[] histX = new float[HIST], histY = new float[HIST];
float[] histZ = new float[HIST], histT = new float[HIST];
boolean[] histTerapia = new boolean[HIST];
int histIdx = 0;

// --- valores suavizados, so para a animacao ---
float rollS = 0, pitchS = 0;
float tXs = 0, tYs = 0, tZs = 0, tTs = 0;

// --- CORES ---
color FUNDO      = color(15, 15, 25);
color PELE       = color(228, 178, 118);
color PELE_CLARA = color(240, 196, 140);
color TREMOR_COR = color(255, 60, 60);
color COR_X      = color(255, 90,  90);
color COR_Y      = color(90,  255, 120);
color COR_Z      = color(110, 150, 255);
color COR_TOTAL  = color(255, 200, 50);
color COR_TERAPIA= color(120, 200, 255);
color TEXTO      = color(225, 225, 235);
color TEXTO_FRACO= color(140, 140, 160);

// Os cinco canais fisicos da luva, na ordem do firmware (PINOS[0..4]).
final String[] CANAIS = {
  "Polegar", "Indicador", "Nervo frente", "Nervo tras", "Mindinho"
};

PFont fonte;

void setup() {
  size(900, 640, P3D);
  fonte = createFont("Monospaced", 12);
  textFont(fonte);

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

  // Abrir a porta pode falhar (ocupada pelo Monitor Serial do Arduino IDE, sem
  // permissao no grupo dialout...). Sem o try o sketch morria com stack trace.
  try {
    porta = new Serial(this, portas[idx], 115200);
    porta.clear();                 // descarta o meio-pacote que ja estava no buffer
    porta.bufferUntil('\n');
  } catch (Exception e) {
    println("ERRO ao abrir " + portas[idx] + ": " + e.getMessage());
    println("A porta esta ocupada? Feche o Monitor Serial do Arduino IDE.");
    semPorta = true;
  }
}

// Detecta a porta do ESP32 (ttyUSB/ttyACM no Linux, tty.usb/cu.usb no macOS,
// COM no Windows). Cai no indice 0 se nao achar nenhuma pista.
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

  if (semPorta) {
    desenhaAvisoSemPorta();
    return;
  }

  copiaEstado();

  boolean conectado = (millis() - ultimaLinha) < SEM_DADOS_MS && linhasOk > 0;

  // Suavizacao so para a animacao; os numeros exibidos sao os valores crus.
  rollS  = suavizaAngulo(rollS,  roll,  0.15);
  pitchS = suavizaAngulo(pitchS, pitch, 0.15);
  tXs += (tX     - tXs) * 0.2;
  tYs += (tY     - tYs) * 0.2;
  tZs += (tZ     - tZs) * 0.2;
  tTs += (tTotal - tTs) * 0.2;

  desenhaCena3D(conectado);
  desenhaHUD(conectado);
}

// Le tudo o que veio da serial de uma vez so, sob o lock, e copia para as
// variaveis locais. Do resto do draw() em diante nada mais cruza threads.
void copiaEstado() {
  synchronized (trava) {
    roll = sRoll;  pitch = sPitch;
    tX = sX;  tY = sY;  tZ = sZ;  tTotal = sT;  dominancia = sDom;
    terapiaAtiva = sTerapia;
    ultimaLinha = sUltimaLinha;
    linhasOk = sLinhasOk;  linhasRuins = sLinhasRuins;
    histIdx = sHistIdx;
    arrayCopy(sHistX, histX);
    arrayCopy(sHistY, histY);
    arrayCopy(sHistZ, histZ);
    arrayCopy(sHistT, histT);
    arrayCopy(sHistTerapia, histTerapia);
  }
}

// ============================================================================
// CENA 3D
// ============================================================================
void desenhaCena3D(boolean conectado) {
  pushMatrix();
  translate(width / 2 + 10, height / 2 - 130, 0);

  // Grade do chao: fixa no mundo, nao gira com a mao
  stroke(40, 40, 65);
  strokeWeight(1);
  for (int i = -400; i <= 400; i += 50) line(i, 170, -250, i, 170, 250);
  for (int j = -250; j <= 250; j += 50) line(-400, 170, j, 400, 170, j);

  ambientLight(90, 90, 110);
  directionalLight(255, 255, 255, -0.5, 1, -1);

  rotateX(radians(-pitchS));
  rotateZ(radians(-rollS));

  // Vibracao visual proporcional ao tremor. Usa T_MAX como teto, igual as
  // barras e ao grafico - antes o teto aqui era 1.0 e o das barras 0.6, entao
  // a mao parecia tremer menos do que as barras indicavam.
  if (conectado && tTs > LIMIAR_TREMOR) {
    float shake = map(constrain(tTs, LIMIAR_TREMOR, T_MAX), LIMIAR_TREMOR, T_MAX, 0.5, 6.0);
    translate(random(-shake, shake), random(-shake, shake), random(-shake, shake));
  }

  desenhaMao(conectado);

  // Eixos do corpo, nas mesmas cores das barras do HUD
  strokeWeight(2);
  stroke(COR_X);  line(0, 0, 0, 170, 0, 0);
  stroke(COR_Y);  line(0, 0, 0, 0, -150, 0);
  stroke(COR_Z);  line(0, 0, 0, 0, 0, 150);
  strokeWeight(1);

  popMatrix();
}

// Mao direita deitada, palma para baixo, dedos apontando para +X.
// A mao inteira e' colorida por tTotal: com um unico acelerometro nao da para
// atribuir tremor a um dedo especifico, entao colorir dedo a dedo (como fazia
// a versao anterior) seria inventar informacao que a luva nao tem.
void desenhaMao(boolean conectado) {
  noStroke();

  float intensidade = conectado ? norm01(tTs) : 0;
  color corMao   = lerpColor(PELE, TREMOR_COR, intensidade);
  color corPunho = lerpColor(PELE_CLARA, TREMOR_COR, intensidade * 0.7);
  if (!conectado) { corMao = color(90, 90, 100); corPunho = color(105, 105, 115); }

  // Punho
  fill(corPunho);
  pushMatrix();
  translate(-100, 0, 0);
  box(55, 26, 68);
  popMatrix();

  // Palma
  fill(corMao);
  box(145, 22, 100);

  // Quatro dedos (indice 1..4 dos canais). O polegar vem depois, a parte.
  float curva = radians(7 + intensidade * 22);
  desenhaDedo(-36, 62, corMao, curva);   // mindinho
  desenhaDedo(-12, 80, corMao, curva);   // nervo tras
  desenhaDedo( 12, 86, corMao, curva);   // nervo frente
  desenhaDedo( 36, 74, corMao, curva);   // indicador

  // Polegar: 2 falanges, saindo da lateral da palma em diagonal.
  // (Antes ficava sempre cor de pele, destoando do resto da mao.)
  pushMatrix();
  translate(30, 0, 52);
  rotateY(radians(-48));
  fill(corMao);
  float[] segPol = {34, 30};
  for (int s = 0; s < 2; s++) {
    rotateZ(radians(6));
    translate(segPol[s] / 2, 0, 0);
    box(segPol[s], 16, 18);
    translate(segPol[s] / 2, 0, 0);
  }
  popMatrix();
}

void desenhaDedo(float baseZ, float comprimento, color cor, float curva) {
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

// ============================================================================
// HUD 2D
// ============================================================================
void desenhaHUD(boolean conectado) {
  camera();
  noLights();
  hint(DISABLE_DEPTH_TEST);
  textAlign(LEFT, BASELINE);

  int x = 20, y = 28;

  // --- conexao ---
  if (!conectado) {
    fill(255, 120, 60);
    text("SEM DADOS - verifique o cabo ou o reset do ESP32", x, y);
  } else {
    fill(TEXTO_FRACO);
    text("Conectado  " + linhasOk + " linhas" +
         (linhasRuins > 0 ? "  (" + linhasRuins + " descartadas)" : ""), x, y);
  }
  y += 26;

  // --- inclinacao ---
  fill(TEXTO);
  text("Roll  " + nf(rollS,  1, 1) + " graus", x, y);       y += 18;
  text("Pitch " + nf(pitchS, 1, 1) + " graus", x, y);       y += 28;

  // --- os DOIS criterios de disparo ---
  // O firmware so aciona a terapia quando amplitude E dominancia passam.
  // Mostrar apenas a amplitude (como este painel fazia antes) anunciava
  // "TREMOR ACIMA DO LIMIAR" em situacoes que o firmware descartava, o que
  // dava a impressao de que os motores estavam falhando.
  boolean ampOk = conectado && tTotal     >= LIMIAR_TREMOR;
  boolean domOk = conectado && dominancia >= LIMIAR_DOM;

  fill(ampOk ? color(60, 255, 120) : TEXTO_FRACO);
  text((ampOk ? "[ok]   " : "[nao]  ") + "forca      " +
       nf(tTotal, 1, 3) + " g  (min " + nf(LIMIAR_TREMOR, 1, 2) + ")", x, y);
  y += 18;

  fill(domOk ? color(60, 255, 120) : TEXTO_FRACO);
  text((domOk ? "[ok]   " : "[nao]  ") + "pureza     " +
       nf(dominancia, 1, 2) + "    (min " + nf(LIMIAR_DOM, 1, 2) + ")", x, y);
  y += 22;

  if (ampOk && domOk) {
    fill(TREMOR_COR);
    text("TREMOR DETECTADO", x, y);
  } else if (ampOk) {
    // O caso que mais confunde na bancada: forca sobra, o que falta e' pureza.
    fill(255, 170, 60);
    text("nao aciona: movimento lento demais junto", x, y);
    y += 16;
    fill(TEXTO_FRACO);
    text("(o punho girando joga a gravidade na faixa 0,5-3 Hz)", x, y);
  } else {
    fill(60, 255, 120);
    text("em repouso", x, y);
  }
  y += 24;

  // --- estado do firmware ---
  // A flag da terapia diz em qual fase do ciclo medir/tratar a luva esta.
  if (terapiaAtiva) {
    fill(COR_TERAPIA);
    text("TERAPIA CR ATIVA", x, y);
    y += 16;
    fill(TEXTO_FRACO);
    text("leitura congelada (motores ligados)", x, y);
  } else {
    fill(TEXTO_FRACO);
    text("MEDINDO (motores parados)", x, y);
    y += 16;
    fill(TEXTO_FRACO);
    text("leitura ao vivo", x, y);
  }
  y += 30;

  // --- barras por eixo ---
  fill(TEXTO_FRACO);
  text("Amplitude do tremor por eixo do acelerometro (3,5-7 Hz)", x, y);
  y += 8;
  desenhaBarra("X", tXs, tX, COR_X,     x, y);       y += 24;
  desenhaBarra("Y", tYs, tY, COR_Y,     x, y);       y += 24;
  desenhaBarra("Z", tZs, tZ, COR_Z,     x, y);       y += 24;
  desenhaBarra("|T|", tTs, tTotal, COR_TOTAL, x, y); y += 30;

  desenhaBarraPureza(x, y);  y += 30;

  fill(TEXTO_FRACO);
  text("|T| = modulo dos tres eixos. Aciona a terapia so com pureza ok.", x, y);
  y += 16;
  text("A luva tem um MPU so: mede a mao inteira, nao dedo a dedo.", x, y);

  desenhaLegendaCanais();
  desenhaGrafico();

  hint(ENABLE_DEPTH_TEST);
}

// Barra com marca do limiar. Mostra o valor cru (nao o suavizado) no texto,
// para bater com o que o firmware realmente mandou.
void desenhaBarra(String rotulo, float suave, float cru, color cor, int x, int y) {
  final int LARG = 300, ALT = 14;
  int bx = x + 52;

  noStroke();
  fill(38, 38, 56);
  rect(bx, y, LARG, ALT, 3);

  float w = map(constrain(suave, 0, T_MAX), 0, T_MAX, 0, LARG);
  // Durante a terapia o valor esta congelado: barra mais apagada para nao
  // passar a impressao de que continua sendo medido ao vivo.
  fill(terapiaAtiva ? lerpColor(cor, FUNDO, 0.45) : cor);
  rect(bx, y, w, ALT, 3);

  stroke(255, 255, 255, 130);
  strokeWeight(1);
  float xLim = bx + map(LIMIAR_TREMOR, 0, T_MAX, 0, LARG);
  line(xLim, y - 1, xLim, y + ALT + 1);
  noStroke();

  fill(TEXTO);
  text(rotulo, x, y + ALT - 2);
  fill(cru >= LIMIAR_TREMOR ? cor : TEXTO_FRACO);
  text(nf(cru, 1, 3) + " g", bx + LARG + 10, y + ALT - 2);
}

// Quanto da energia esta na faixa de tremor (3,5-7 Hz) e nao na de movimento
// voluntario (0,5-3 Hz). E' o segundo criterio de disparo.
void desenhaBarraPureza(int x, int y) {
  final int LARG = 300, ALT = 14;
  int bx = x + 52;

  noStroke();
  fill(38, 38, 56);
  rect(bx, y, LARG, ALT, 3);

  boolean ok = dominancia >= LIMIAR_DOM;
  fill(ok ? color(120, 220, 160) : color(150, 120, 90));
  rect(bx, y, map(constrain(dominancia, 0, 1), 0, 1, 0, LARG), ALT, 3);

  stroke(255, 255, 255, 130);
  strokeWeight(1);
  float xLim = bx + map(LIMIAR_DOM, 0, 1, 0, LARG);
  line(xLim, y - 1, xLim, y + ALT + 1);
  noStroke();

  fill(TEXTO);
  text("pureza", x, y + ALT - 2);
  fill(ok ? color(120, 220, 160) : TEXTO_FRACO);
  text(nf(dominancia, 1, 2), bx + LARG + 10, y + ALT - 2);
}

// Os cinco canais existem no HARDWARE (um motor e um LED por posicao), mas o
// firmware nao manda nada por canal: no Coordinated Reset todos pulsam, em
// ordem sorteada. Por isso aqui eles aparecem como referencia de montagem, e
// nao como cinco medidas independentes.
void desenhaLegendaCanais() {
  int x = width - 250, y = 40;
  fill(TEXTO_FRACO);
  text("Canais da luva (motor + LED)", x, y);
  y += 20;
  for (int i = 0; i < CANAIS.length; i++) {
    if (terapiaAtiva) fill(COR_TERAPIA); else fill(90, 90, 110);
    ellipse(x + 6, y - 4, 9, 9);
    fill(terapiaAtiva ? TEXTO : TEXTO_FRACO);
    text(CANAIS[i], x + 20, y);
    y += 19;
  }
  y += 6;
  fill(TEXTO_FRACO);
  text(terapiaAtiva ? "pulsando em ordem sorteada" : "em repouso", x, y);
}

void desenhaGrafico() {
  int gx = 20, gy = height - 120, gw = width - 40, gh = 88;

  fill(20, 20, 35);
  stroke(50, 50, 80);
  strokeWeight(1);
  rect(gx, gy, gw, gh, 4);

  // Faixas em que a terapia esteve ativa, ao fundo
  noStroke();
  fill(COR_TERAPIA, 30);
  for (int i = 0; i < HIST; i++) {
    int idx = (histIdx + i) % HIST;
    if (histTerapia[idx]) rect(gx + map(i, 0, HIST, 0, gw), gy + 1, gw / (float) HIST + 1, gh - 2);
  }

  // Linha do limiar
  stroke(255, 255, 255, 60);
  float yLim = gy + gh - map(LIMIAR_TREMOR, 0, T_MAX, 0, gh);
  line(gx, yLim, gx + gw, yLim);

  desenhaSerie(histX, gx, gy, gw, gh, COR_X);
  desenhaSerie(histY, gx, gy, gw, gh, COR_Y);
  desenhaSerie(histZ, gx, gy, gw, gh, COR_Z);
  desenhaSerie(histT, gx, gy, gw, gh, COR_TOTAL);

  noStroke();
  fill(TEXTO_FRACO);
  text("Ultimos 4 s   (fundo azul = terapia ativa, linha branca = limiar)", gx + 4, gy - 6);

  int lx = gx + 4, ly = gy + gh + 18;
  fill(COR_X);     text("X",   lx,       ly);
  fill(COR_Y);     text("Y",   lx + 30,  ly);
  fill(COR_Z);     text("Z",   lx + 60,  ly);
  fill(COR_TOTAL); text("|T|", lx + 90,  ly);
  fill(TEXTO_FRACO);
  text("escala 0 a " + nf(T_MAX, 1, 2) + " g", lx + 140, ly);
}

void desenhaSerie(float[] hist, int gx, int gy, int gw, int gh, color cor) {
  noFill();
  stroke(cor);
  strokeWeight(1.2);
  beginShape();
  for (int i = 0; i < HIST; i++) {
    int idx = (histIdx + i) % HIST;
    float px = gx + map(i, 0, HIST, 0, gw);
    float py = gy + gh - map(constrain(hist[idx], 0, T_MAX), 0, T_MAX, 0, gh);
    vertex(px, py);
  }
  endShape();
  strokeWeight(1);
}

void desenhaAvisoSemPorta() {
  fill(255, 90, 90);
  textAlign(CENTER, CENTER);
  text("Nenhuma porta serial disponivel.\n\n" +
       "Conecte o ESP32, feche o Monitor Serial do Arduino IDE\n" +
       "e reinicie este sketch.",
       width / 2, height / 2);
  textAlign(LEFT, BASELINE);
}

// ============================================================================
// AUXILIARES
// ============================================================================
float norm01(float v) {
  return constrain(map(v, 0, T_MAX, 0, 1), 0, 1);
}

// Interpola angulos pelo caminho mais curto (trata a descontinuidade em +-180)
float suavizaAngulo(float atual, float alvo, float fator) {
  float d = ((alvo - atual + 540) % 360) - 180;
  return atual + d * fator;
}

// ============================================================================
// SERIAL  (roda na thread da serial - so toca no bloco protegido pelo lock)
// ============================================================================
void serialEvent(Serial p) {
  String linha = p.readStringUntil('\n');
  if (linha == null) return;
  linha = trim(linha);
  if (linha.length() == 0) return;

  String[] v = split(linha, ',');
  // O firmware manda 7 campos. Linhas com menos campos sao lixo de boot
  // (mensagens de DEBUG, avisos de calibracao) ou pacote cortado.
  if (v.length < 7) {
    synchronized (trava) { sLinhasRuins++; }
    return;
  }

  float r  = float(v[0]);
  float pt = float(v[1]);
  float x  = float(v[2]);
  float y  = float(v[3]);
  float z  = float(v[4]);
  float t  = float(v[5]);

  // float() do Processing devolve NaN em linha corrompida (nao lanca excecao).
  // Um unico NaN contaminaria a suavizacao para sempre - descarta a linha.
  if (Float.isNaN(r) || Float.isNaN(pt) || Float.isNaN(x) ||
      Float.isNaN(y) || Float.isNaN(z)  || Float.isNaN(t)) {
    synchronized (trava) { sLinhasRuins++; }
    return;
  }

  // A flag de terapia vem do firmware. Nao da mais para deduzi-la de
  // "tTotal > 0": desde as correcoes o firmware manda a amplitude medida
  // sempre, inclusive abaixo do limiar, entao tTotal e' quase sempre > 0.
  boolean terapia = (trim(v[6]).equals("1"));

  // 8o campo (dominancia) e' opcional: firmware antigo mandava so 7.
  float dom = 0;
  if (v.length >= 8) {
    dom = float(v[7]);
    if (Float.isNaN(dom)) dom = 0;
  }

  synchronized (trava) {
    sRoll = r;  sPitch = pt;
    sX = x;  sY = y;  sZ = z;  sT = t;
    sTerapia = terapia;
    sDom = dom;
    sUltimaLinha = millis();
    sLinhasOk++;

    sHistX[sHistIdx] = x;
    sHistY[sHistIdx] = y;
    sHistZ[sHistIdx] = z;
    sHistT[sHistIdx] = t;
    sHistTerapia[sHistIdx] = terapia;
    sHistIdx = (sHistIdx + 1) % HIST;
  }
}
