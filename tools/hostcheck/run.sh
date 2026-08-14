#!/usr/bin/env bash
# Uebersetzt die Erkennungslogik der Firmware fuer den PC und laesst sie auf
# synthetischen Zaehlerbildern laufen. Braucht nur g++, python3 und Pillow -
# keinen ESP32 und keinen Arduino-Toolchain.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$HERE/../../firmware/WaterMeterCam"
OUT="${TMPDIR:-/tmp}/wmc-hostcheck"
mkdir -p "$OUT"

echo "== uebersetzen =="
g++ -std=c++17 -O2 -Wall -Wextra \
    -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers \
    -I "$HERE/shim" -I "$FW" \
    "$HERE/test_recognize.cpp" \
    "$FW/util.cpp" "$FW/imgproc.cpp" "$FW/config.cpp" \
    "$FW/templates.cpp" "$FW/recognize.cpp" "$FW/meter.cpp" \
    -o "$OUT/test_recognize"

echo
echo "== Testbilder erzeugen =="
GEN="$HERE/make_test_image.py"
ARGS=()
add() { # reading, rotation, extra args...
  local reading="$1"; local rot="$2"; shift 2
  local name="$OUT/$(echo "$reading" | tr -d '.')_$rot.pgm"
  python3 "$GEN" --reading "$reading" --rotation "$rot" --out "$name" "$@" >/dev/null
  ARGS+=("$name" "$reading" "$rot")
}

add 01234.567 0
add 00000.000 0 --last-frac 0.05     # Nulldurchgang auf allen Stellen
add 09999.999 0 --last-frac 0.95     # alle Stellen kurz vor dem Ueberlauf
add 00099.900 0 --last-frac 0.40     # zwei Stellen mitten im Umsprung
add 12345.678 2.5                    # leicht schief montierte Kamera
add 54321.098 0 --noise 8 --blur 1.2 # schlechtes Licht, unscharfes Objektiv
add 77777.777 -1.5 --noise 5         # andere Drehrichtung, mehr Rauschen

echo
echo "== Testlauf =="
"$OUT/test_recognize" "${ARGS[@]}"
