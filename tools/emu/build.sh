#!/bin/bash
# Builds the TamaPoke desktop emulator from the real firmware sources.
#
#   bash tools/emu/build.sh && tools/emu/tamapoke-emu
#
# Needs SDL2 (brew install sdl2 / apt install libsdl2-dev). Nothing else: the
# hardware is stubbed, so the Arduino toolchain is not required.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$HERE"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "SDL2 not found. macOS: brew install sdl2   Debian: apt install libsdl2-dev" >&2
  exit 1
fi

# The Arduino build generates function prototypes for the .ino automatically;
# a plain g++ build does not, and the sketch calls plenty of functions before
# defining them. Regenerated every build so it can never go stale.
python3 genproto.py "$ROOT/TamaPoke.ino"

# .ino is C++ but g++ will not compile that extension
{ echo '#include "proto.h"'; cat "$ROOT/TamaPoke.ino"; } > sketch.cpp

g++ -std=c++17 -O1 -w \
  -I. -I"$ROOT" \
  -DSPRITE_DIR="\"$ROOT/tools/sdcard/mons\"" \
  $(sdl2-config --cflags) \
  -o tamapoke-emu \
  sketch.cpp "$ROOT/pet.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/battle.cpp" \
  host_impl.cpp font.cpp clock.cpp main_sdl.cpp \
  $(sdl2-config --libs)

echo "built: $HERE/tamapoke-emu"
