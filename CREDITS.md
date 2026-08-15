# Credits

TamaPoke is a **non-commercial, personal-use** project. It does not sell or
commercially redistribute any copyrighted material. Pokémon and all related
names, designs and characters are trademarks and © of **Nintendo / Game Freak /
The Pokémon Company**.

This project is not affiliated with or endorsed by any of those companies.

## Sprites and data

| Resource | Source | Use in the project |
|---|---|---|
| **All sprites** (idle, walk, sleep, eat, hurt, attack…) | [PMD Sprite Collaboration (PMDCollab/SpriteCollab)](https://github.com/PMDCollab/SpriteCollab) | Mystery-Dungeon-style animated sprites used everywhere: main screen, stat card, minigame, and the Pokédex grid + detail view |
| **Gen 1 base stats** | [PokéAPI](https://pokeapi.co) | Real ATK/DEF/SPD/HP for each species |

The **SpriteCollab** sprites are the work of its community of artists under their
own terms (Creative Commons Attribution-NonCommercial 4.0). Per-species/per-author
credit is in the original repository's
[tracker.json](https://github.com/PMDCollab/SpriteCollab/blob/master/tracker.json).
Huge thanks to that whole community for an enormous amount of work.

> **Important if you reuse this repo:** the packaged sprite files
> (`tools/sdcard/mons/*.bin`) are derived from the sources above. Don't
> redistribute them commercially. If you publish the project, the clean approach
> is to distribute **only the code and scripts**, and have each user download and
> package the sprites from the original sources with `tools/pack_*.py` (or the web
> installer).

## Software / hardware

| Component | Author / source |
|---|---|
| GFX Library for Arduino | [moononournation](https://github.com/moononournation/Arduino_GFX) |
| SensorLib (CST9217 touch, PCF85063 RTC) | [Lewis He / lewisxhe](https://github.com/lewisxhe/SensorLib) |
| XPowersLib (AXP2101 PMU) | [Lewis He / lewisxhe](https://github.com/lewisxhe/XPowersLib) |
| Board and pinout | [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75) |
| Web installer | [ESP Web Tools](https://esphome.github.io/esp-web-tools/) (Nabu Casa) |
| 5x7 bitmap font in `tools/emu/font.cpp` | [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library) © 2012 Adafruit Industries, BSD licence |

TamaPoke's own code (firmware and tools) is original work.

## Battle backgrounds

`tools/backs/*.png` — twelve 240x112 scenes, one day and one night per biome,
packed into `backs.h` by `tools/gen_backs.py`.

**Provenance not established.** These were supplied for the project rather than
sourced by it. Before this repo goes public, confirm where they came from and
under what licence, the same way the PMD sprites are accounted for above — and
replace them if that cannot be answered.

## Gym badge artwork

**https://github.com/SteGriff/pokemon-badges** — SVG recreations of the first 40
league badges, Kanto through Unova, traced from Bulbapedia. Stephen Griffiths,
2011, **CC BY 3.0** (attribution required, commercial use permitted).

`svg/Kanto.svg` holds the eight badges, and `tools/gen_badges.py` renders it
with `rsvg-convert`, isolates each and packs them into `badges.h` at 32x32.

**CC BY requires this attribution to travel with anything built from it** --
firmware binaries included, not just the source.
