# TamaPoke desktop emulator

Runs the **real firmware** on your computer, in a window you can click.

![emulator](https://img.shields.io/badge/needs-SDL2-1793D1)

```bash
brew install sdl2          # macOS   (Debian: apt install libsdl2-dev)
bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60
```

It compiles `TamaPoke.ino`, `pet.cpp`, `i18n.cpp` and `party.cpp` **unmodified**.
Only the hardware layer is replaced, so what you see is what the panel draws —
the same 466×466 RGB565 framebuffer, the same 5×7 font, the same code paths.

## Why

The firmware is otherwise only testable by flashing a board and squinting at it.
This gives you a UI you can iterate on in seconds, and it makes layout mistakes
(text running off the round bezel, bars overflowing) obvious before they reach
hardware. The game logic can also be driven headlessly — see *Headless* below.

**It is not a substitute for the real thing.** Timing, touch behaviour, DMA
tearing, PSRAM pressure, audio and battery all only exist on the board.

## Using it

| | |
|---|---|
| **Click** | touch |
| **Drag** | swipe (gestures resolve on release, as on the device) |
| **Hold 3 s** on the pet | the release dialog |
| **Type in the terminal** | the serial console — `STATS`, `IV 31 31 31 31`, `EGG 150 1`, `PARTY`, `LVL 73`, `WIPE`… |
| **Esc** or close the window | quit (state is saved) |

Pixels outside the round bezel are **dimmed**, not hidden: the framebuffer is
square but the panel is a circle, so this shows you exactly what would be
clipped on real hardware.

### Options

| Flag | Meaning |
|---|---|
| `--scale N` | window zoom (default 2) |
| `--fast N` | run the clock N× faster — `--fast 60` turns an in-game minute into a second, so a full 3-day life takes about an hour |
| `--save FILE` | where to persist NVS (default `tamapoke.nvs` in the cwd) |
| `--wipe` | delete the save first |
| `--sprites DIR` | sprite directory (defaults to `tools/sdcard/mons`) |

### Headless captures

Renders one screen to a PPM and exits — no display needed, so it works over SSH
and in CI:

```bash
tools/emu/tamapoke-emu --shot battle --lvl 73 --iv 31 --dex 149 --out shot.ppm
sips -s format png shot.ppm --out shot.png     # macOS; or use ImageMagick
```

`--shot` accepts `main`, `battle`, `profile`, `medals`, `progress`, `gallery`,
`clock`, `menu`, `party`, `partyfull`, `egg`, `starter`. `--lvl`, `--iv` and
`--dex` set up the pet first.

## How it works

| File | Stands in for |
|---|---|
| `Arduino.h` | `millis`, `random`, `String`, and a `Serial` wired to stdin |
| `Preferences.h` | NVS, backed by a file so your pet survives restarts |
| `Arduino_GFX_Library.h` | the canvas — every primitive the sketch uses, into an RGB565 buffer |
| `TouchDrvCSTXXX.hpp` | the CST9217, fed by the mouse |
| `host_impl.cpp` | SD (reads sprites from disk), RTC, battery, audio |
| `font.cpp` | the classic 5×7 GFX glyphs, so text metrics match exactly |
| `genproto.py` | the prototypes the Arduino build normally generates for you |

Everything except `font.cpp` and `genproto.py` is a stub; the game itself is the
real thing.

`sprites` are read straight from `tools/sdcard/mons/`, so animation, shinies and
Pokédex thumbnails all work if you have generated them (`tools/pack_pmd.py`).
Without them you get the `S_NO_SPRITES` path, exactly as a board with no SD card
would.

## Credits

`font.cpp` is the classic 5×7 bitmap font from **Adafruit_GFX**, © 2012 Adafruit
Industries, BSD licence — full notice at the top of that file.
