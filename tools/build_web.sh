#!/bin/bash
# Regenera web/firmware/tamapoke.bin (firmware combinado) para el instalador web.
# Uso: bash tools/build_web.sh
set -e
cd "$(dirname "$0")/.."
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"

echo "Compilando..."
arduino-cli compile --fqbn "$FQBN" --export-binaries .

B=build/esp32.esp32.esp32s3
echo "Fusionando binarios..."
# esptool is not on PATH; the Arduino core ships one and that is the version
# that matches the build we just made.
ESPTOOL="$(ls ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)"
[ -z "$ESPTOOL" ] && ESPTOOL="$(command -v esptool.py || command -v esptool)"
[ -z "$ESPTOOL" ] && { echo "no esptool found"; exit 1; }

"$ESPTOOL" --chip esp32s3 merge-bin -o web/firmware/tamapoke.bin \
  0x0     "$B/TamaPoke.ino.bootloader.bin" \
  0x8000  "$B/TamaPoke.ino.partitions.bin" \
  0xe000  "$B/boot_app0.bin" \
  0x10000 "$B/TamaPoke.ino.bin"

echo "OK -> web/firmware/tamapoke.bin ($(du -h web/firmware/tamapoke.bin | cut -f1))"

# The manifest version is what the installer shows people; keeping it in step
# with FW_VERSION by hand is exactly the sort of thing that silently rots.
FW="$(grep -o '"[0-9.]*"' TamaPoke.ino | head -1 | tr -d '"')"
python3 - "$FW" <<'PYEOF'
import json, sys
m = json.load(open('web/manifest.json'))
m['version'] = sys.argv[1]
json.dump(m, open('web/manifest.json', 'w'), indent=2)
print('manifest version -> ' + sys.argv[1])
PYEOF

echo "Empaquetando sprites..."
python3 tools/pack_bundle.py
