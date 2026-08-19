#!/bin/bash
# Headless tests. They compile the REAL firmware sources against the emulator's
# hardware stubs, so they assert against Pet/Party/Combatant themselves rather
# than restating their rules -- a harness that re-implements a formula only
# proves the transcription.
#
#   bash tools/emu/tests/run.sh          # everything
#   bash tools/emu/tests/run.sh battle   # just the ones matching "battle"
#
# Two of these exist because nothing else can catch what they catch:
#   flush_test  -- a screen that never calls gfx->flush() leaves the panel
#                  frozen, and headless screenshots CANNOT see it: --shot reads
#                  gfx->buffer() straight out and never consults frameReady.
#                  The training submenu shipped frozen exactly this way.
#   i18n_test   -- STRINGS is positional; a short language row is zero-padded by
#                  the compiler with no diagnostic, silently shifting every
#                  string after the gap in that language only.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
EMU="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$EMU/../.." && pwd)"
FILTER="${1:-}"

command -v sdl2-config >/dev/null || { echo "SDL2 not found (brew install sdl2)" >&2; exit 1; }

# sketch.cpp + proto.h come from the normal build; this also proves the emulator
# still compiles before anything is tested against it
bash "$EMU/build.sh" >/dev/null

# arrays, not a string: the sprite dir has to reach the compiler still quoted,
# and passing these through eval silently strips them
CORE=("$ROOT/gbsynth.cpp" "$ROOT/pet.cpp" "$ROOT/i18n.cpp" "$ROOT/party.cpp" "$ROOT/battle.cpp" "$ROOT/link.cpp" "$ROOT/save.cpp")
FLAGS=(-std=c++17 -O1 -w -I"$EMU" -I"$ROOT" -DSPRITE_DIR="\"$ROOT/tools/sdcard/mons\"")
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# these drive setup()/loop()/render(), so they need the sketch itself
needs_sketch() { case "$1" in touch_test|flush_test|joy_test|anim_test|swipe_test|lan_test|console_test|hit_test|starter_test) return 0;; *) return 1;; esac; }

# and these are standalone: gbsynth.cpp has no Arduino dependency at all, which
# is the point of it -- linking the game core in would only demand stubs for
# symbols the test never calls.
standalone() { case "$1" in synth_test) return 0;; *) return 1;; esac; }

pass=0; fail=0
for src in "$HERE"/*_test.cpp; do
  name="$(basename "$src" .cpp)"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  extra=()
  needs_sketch "$name" && extra=("$EMU/sketch.cpp" "$EMU/host_impl.cpp" "$EMU/font.cpp" "$EMU/clock.cpp")
  srcs=("${CORE[@]}")
  standalone "$name" && srcs=("$ROOT/gbsynth.cpp")
  # every test starts from a clean NVS so one cannot leak state into the next
  rm -f "$OUT/tamapoke.nvs"
  if ! g++ "${FLAGS[@]}" -o "$OUT/$name" "$src" "${srcs[@]}" "${extra[@]}" 2>"$OUT/$name.log"; then
    echo "=== $name: DID NOT COMPILE"; tail -5 "$OUT/$name.log"; fail=$((fail+1)); continue
  fi
  echo "=== $name"
  # pipefail matters: piping the test through grep would otherwise report
  # grep's exit status and every failure would be counted as a pass
  if (cd "$OUT" && set -o pipefail && "./$name" 2>&1 | grep -vE '^(TamaPoke fw|emu:|RTC )'); then
    pass=$((pass+1))
  else
    echo "    ^ $name FAILED"
    fail=$((fail+1))
  fi
done

echo
echo "suites passed: $pass, failed: $fail"
[ "$fail" -eq 0 ]
