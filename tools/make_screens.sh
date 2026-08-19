#!/bin/bash
# Regenerates the screenshots in docs/screens/ that the README shows.
#
#   bash tools/make_screens.sh
#
# They come out of the emulator's headless --shot mode, so they are exactly what
# the firmware draws on the 466x466 panel -- not mockups, and not photographs of
# a screen. Re-run this after changing any screen, or the README slowly starts
# advertising a version of the UI that no longer exists.
set -e
cd "$(dirname "$0")/.."

OUT=docs/screens
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

# Keep this list in step with the tables in README.md § Screens.
SHOTS="main region starter starterj btlmenu btlmoves gympick gymsj dexpick gallery gallery2 player player2 box egg lanready pick moves win"

echo "Building the emulator..."
bash tools/emu/build.sh >/dev/null

for s in $SHOTS; do
  tools/emu/tamapoke-emu --shot "$s" --out "$TMP/$s.ppm" >/dev/null 2>&1 || {
    echo "  $s: FAILED"; continue; }
  if command -v sips >/dev/null; then
    sips -s format png "$TMP/$s.ppm" --out "$OUT/$s.png" >/dev/null
  elif command -v convert >/dev/null; then
    convert "$TMP/$s.ppm" "$OUT/$s.png"
  else
    echo "need sips (macOS) or ImageMagick to convert"; exit 1
  fi
  printf '  %-12s %s KB\n' "$s" "$(( $(wc -c < "$OUT/$s.png") / 1024 ))"
done

echo "wrote $(ls "$OUT" | wc -l | tr -d ' ') screenshots to $OUT"
