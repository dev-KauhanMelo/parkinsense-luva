#!/usr/bin/env bash
#
# ParkinSense — abre os dois projetos de uma vez:
#   1) Firmware da luva  -> Arduino IDE 2 (o v2)
#   2) Visualização 3D   -> Processing
#
# Uso:  ./abrir-projeto.sh   (ou dê duplo clique -> "Executar")
#
set -u

# Raiz do repositório (pasta onde este script está)
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Caminhos dos programas ---
ARDUINO_IDE="$HOME/Documents/arduino-ide_2.3.8_Linux_64bit/arduino-ide"   # Arduino IDE 2 (v2)

# Processing: prefere o caminho do snap; cai no PATH se necessário
if [ -x "/snap/bin/processing" ]; then
  PROC_CMD="/snap/bin/processing"
elif command -v processing >/dev/null 2>&1; then
  PROC_CMD="processing"
else
  PROC_CMD=""
fi

# --- Caminhos dos projetos ---
SKETCH_INO="$DIR/sketch_tremor_FINAL222.ino"
SKETCH_PDE="$DIR/visualizacao/visuLuvinha/visuLuvinha.pde"

echo "======================================"
echo " ParkinSense — abrindo os projetos"
echo "======================================"

# 1) Arduino IDE 2 com o firmware da luva
if [ -x "$ARDUINO_IDE" ]; then
  echo "-> Arduino IDE 2  : $SKETCH_INO"
  nohup "$ARDUINO_IDE" "$SKETCH_INO" >/dev/null 2>&1 &
else
  echo "!! Arduino IDE 2 nao encontrado em:"
  echo "   $ARDUINO_IDE"
  echo "   (ajuste a variavel ARDUINO_IDE no topo deste script)"
fi

# 2) Processing com a visualizacao 3D
if [ -n "$PROC_CMD" ]; then
  echo "-> Processing     : $SKETCH_PDE"
  nohup "$PROC_CMD" "$SKETCH_PDE" >/dev/null 2>&1 &
else
  echo "!! Processing nao encontrado (esperado em /snap/bin/processing)"
fi

echo
echo "Pronto! As janelas abrem em alguns segundos."
echo "Dica: ligue o ESP32 antes de rodar o Processing para ele achar a porta."
