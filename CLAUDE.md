# TamaPoke

Gen-1-Pokémon tamagotchi firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75**.
Arduino/C++ firmware + a Python asset pipeline + a browser-based flasher.
Personal, non-commercial fan project. Code MIT; sprites CC BY-NC (PMD SpriteCollab).

## Layout

| Path | What |
|---|---|
| `TamaPoke.ino` | Main sketch (~2.3k LOC): UI, screens, touch, serial console, `FW_VERSION` |
| `pet.cpp/.h` | Game state machine: stats, tick, evolution, eggs, save/load, balance constants |
| `species.h` / `dex.h` | The 151: names, typings, evolution chains, base stats, rarity tiers, favourite berry |
| `types.h` | Type-effectiveness helpers over the generated 18x18 chart in `dex.h` |
| `party.cpp/.h` | The 6 retired pets banked by farewell/release (not runaway) |
| `i18n.cpp/.h` | 6-language string table (ES/EN/FR/DE/IT/PT) |
| `audio.cpp/.h` | ES8311 codec over I2S |
| `rtcbat.cpp/.h` | PCF85063 RTC + AXP2101 battery/PMU/PWR button |
| `sdmon.cpp/.h` | SD sprite streaming + USB `PUT` file transfer |
| `pin_config.h` | Board pinout — from the official Waveshare repo, don't invent values |
| `tools/*.py` | Sprite pipeline (PMD fetch/pack, thumbs, bundle, USB send) |
| `tools/emu/` | Desktop emulator: runs the real firmware in an SDL window |
| `web/` | ESP Web Tools installer page + prebuilt `tamapoke.bin` + `sprites.pak` |

## Build & flash

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile --fqbn "$FQBN" .
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "$FQBN" .

bash tools/build_web.sh   # recompiles + merges web/firmware/tamapoke.bin + repacks sprites.pak
```

**PSRAM (OPI) is mandatory** — the 466×466×16-bit framebuffer is ~434 KB and lives there.
Wrong partition scheme (no FAT) or PSRAM off = it builds and then fails on hardware.

## Hard rules

**No accents, ñ, or non-ASCII in any firmware string.** The bitmap font has no glyphs
for them. This applies to *all six* languages — French, German, Portuguese and Spanish
strings in `i18n.cpp` are deliberately written unaccented ("Esta", "bano", "Pokedex").
Adding a proper "é" silently renders as garbage on the panel.

**Adding a UI string is a two-file, order-sensitive edit.** Append the `StrId` to the
enum in `i18n.h`, then add the translation at the *same index* in all 6 rows of
`STRINGS[LANG_COUNT][STR_COUNT]` in `i18n.cpp`. The table is positional — a missing
entry in one language shifts every string after it in that language.

**Balance changes must update the README.** `README.md` § "Game manual (the actual
numbers)" documents exact drain rates, spawn odds and thresholds straight from the
code. It is the project's spec, not decoration — changing a constant in `pet.h`/
`pet.cpp` without updating that table makes the docs lie. Bump `FW_VERSION` in
`TamaPoke.ino:27` and the firmware badge at the top of the README in the same commit.

**Comments and commit messages are in English** — as of v1.5. Most of the existing
source is commented in unaccented Spanish (the original author's convention) and is
deliberately *not* being back-translated, so the codebase is mixed for now. Do not
match the surrounding Spanish: any comment you write or edit goes in English, even
in a file that is Spanish everywhere else. Never rewrite a Spanish comment purely to
translate it — only when you are already changing that code for another reason.

The default *UI* language is English (`LANG_DEFAULT LANG_EN`); UI strings live in
`i18n.cpp` and are a separate matter from source comments.

## Testing

**The game logic and the UI can both be tested without a board.** `pet.cpp`,
`party.cpp` and `i18n.cpp` are plain C++ with no hardware dependency beyond
`Arduino.h`/`Preferences.h`, so `tools/emu/` stubs those and compiles the *real*
sources — including `TamaPoke.ino` — into a clickable SDL app:

```bash
brew install sdl2 && bash tools/emu/build.sh
tools/emu/tamapoke-emu --scale 2 --fast 60      # clickable, serial on stdin
tools/emu/tamapoke-emu --shot battle --out s.ppm  # headless screenshot
```

Prefer this for anything about balance, save/load, migrations or layout: it runs
in seconds and can simulate a full 3-day life. Write assertions against the real
classes rather than re-implementing their formulas in the test — a harness that
restates the rules only proves the transcription, not the firmware.

What the emulator can NOT tell you: timing, touch behaviour, DMA tearing, PSRAM
pressure, audio, battery. Those still need the board.

On hardware, verify over the serial console (115200):

- `STATS` full state · `HEALTH` uptime + heap (soak test) · `WIPE` factory reset
- `SPEC <dex>` `LVL <n>` `IV <a> <d> <s> <h>` `HATCH` `SHINY` `EGGS` (20 eggs) `GAL`
- `PARTY` / `PARTY <dex>` / `PARTY CLEAR` inspect, fill and empty the party
- `EGG <dex> [shiny]` hatch a chosen species: the legendary (3 perfect IVs) and
  shiny (IV floor 20) guarantees fire only inside `hatch()`, so this is the only
  way to reach them — `SHINY` just flips the flag on an already-hatched pet
- `BYE` / `RUN` / `ABANDON` — the three endings · `BEEP` audio · `LS` / `PUT` SD files
- `TIME <epoch>` / `RTCSET <epoch>` — offline-progression and clock paths

To exercise long-horizon logic in minutes, temporarily lower `PET_TICK_MS`,
`MINUTES_PER_LEVEL` and `FAREWELL_AGE_MIN` in `pet.h`.

## Constraints worth remembering

- Sprites are ~40 MB and stream from microSD at runtime; they are never linked into
  the binary. Missing SD = the `S_NO_SPRITES` path, which must stay graceful.
- The pet keeps ageing while powered off via the RTC, catching up to 2 weeks max.
  Any change to tick/aging must survive a large `deltaMinutes` jump without overflow.
- `TamaPoke.ino` is one large sketch by design. Prefer small, surgical diffs over
  refactoring it into modules unless that's the explicit task.

## Git

Feature branches only, never commit straight to `main`.

## Roadmap

Wild encounters / battle (designed, unimplemented): resolution by ATK/DEF/SPD using
PMD Attack/Hurt animations, trainer rank as endgame. Battle style not yet chosen.
Plus a 24–48 h soak test using `HEALTH`.

## TODO

Working state, so it survives a closed session. Tick items off as they land.

### In flight — branch `fix/emu-touch-int`

- [x] Emulator: touch was dead (`attachInterrupt` was a no-op, so the `gTouchIrq`
      gate in `handleTouch` never opened). Committed.
- [x] Emulator: `--fast` scaled `millis()`, which the sketch also uses for gesture
      timing, shrinking the tap window (`dt < 1500`) to `1500/scale` real ms. Clock
      now runs 1x during a gesture. Committed, `tools/emu/clock.cpp`.
- [x] Menu: `STATS` row added as row 0, opens `cardOpen` at `cardPage 1`. Panel
      recentred (`MENU_Y 75`, `MENU_H 316`) — five rows at the old Y got clipped
      by the round bezel.
- [x] 5th icon (dumbbell, `SPR_ICON_TRAIN`) + `trainOpen` submenu: STRENGTH →
      `startSack()`, SPEED → `startGame()`, DEFENCE → informational only.
      Icon row re-spaced 62 → 54 to clear the ENE/HYG bars at y=361.
- [x] Bumped `FW_VERSION` to 1.8 and the README firmware badge.
- [ ] Optional: move the emulator touch/i18n test harnesses out of the scratchpad
      into `tools/emu/` as regression tests.

### Battle system — decided, not started

**Turn-based and move-based, like the real games.** This is not a fresh choice:
`moves.h` (78 moves: name, type, MC_PHYS/SPEC/STATUS, power, acc, effect, target,
plus stat stages, priority, multi-hit, recoil, drain, heal, charge/recharge) is
already a turn-based engine's data layer. `types.h` has integer `typeEffPct()`.
The old roadmap line about "resolution by ATK/DEF/SPD" is superseded — it would
discard all of it.

Settled:

- **Special split lives on the species, not the individual.** `dex_stats.py`
  already holds all 6 stats; `gen_dex.py:94` unpacks `spa, spd` and discards
  them because the struct on line 71 declares only 4. Fix = add `bSpa`/`bSpd`
  and regenerate. Special attack runs off `ivAtk/trAtk` vs `bSpA`, special
  defence off `ivDef/trDef` vs `bSpD` — **no new IVs, no NVS migration** (the
  rationale is already written up in `fetch_pokeapi.py:13-17`).
- **The party is the battle team.** `PartyMon` already carries full stats and
  `Party::atkOf/defOf/speOf/vitOf` exist. Retired pets are frozen at banking
  (level, training — and moves, once they exist), which is the level cap.
- **Moves are player-chosen.** On level-up the player picks which of the 4 to
  forget; it is never automatic. Frozen at banking, so the choice is permanent.
- **Status ailments are IN.** Requires a `MoveEntry` schema change: `effect` is
  a single slot already used by EF_RECOIL etc., so a damaging move cannot also
  carry a secondary status. Add `ailment` + `ailChance` fields, then author them
  onto `dex_moves.py` (hand-written, not fetched — PokeAPI ailment data was
  never pulled).
- **No PP.** No field in `MoveEntry`, and it stays that way.
- **Rewards are badges/rank, not XP.** `level() = 1 + ageMinutes/MINUTES_PER_LEVEL`
  — level is age. Granting XP would break real-time ageing. Learnsets are
  level-keyed, so moves unlock as the pet ages.

- **Ailments are battle-only.** They live in the battle state and clear when it
  ends — never in `Pet`, never saved, never ticked by offline catch-up.

**Endgame: 8 gym leaders + Elite 4, on two difficulties.** Easy is the ladder;
hard reruns it with better AI decision-making and opponents with strong IVs and
real movesets. Needs a trainer roster table (~13 trainers x 3-6 mons: species,
level, moves, IVs) and two AI tiers — easy picks naively, hard reads
`typeEffPct()`, STAB, stat stages and available KOs.

Level anchor for balancing the ladder: `MINUTES_PER_LEVEL 60` and farewell at
3 days means a fully-raised pet retires at **level 73**. Pets banked earlier are
weaker, so team strength reflects how long each one was raised.

- **The player picks the battle team.** Pool = the live pet plus the 6 banked;
  choose up to 6 per battle. The live pet is selectable, never compulsory.
  Battling costs the *live* pet energy (banked pets are retired, so they cost
  nothing) — it ties battle to the care sim and rate-limits grinding without a
  cooldown timer.
- **No gating on gyms.** Attrition is the gate: leaders always bring their full
  roster and you bring whoever you have, so one strong pet can sweep an early
  gym but cannot survive five opponents back-to-back later. Self-balancing, no
  badge checks or minimum-team rules, and it never walls the endgame off behind
  weeks of raising.
- **Level caps at 100** (`MAX_LEVEL`), reached at 4d 3h. `MINUTES_PER_LEVEL`
  stays 60 — compressing the curve to force 100 into a 3-day life would be a
  balance change that buys a number you can already reach by playing on.

Phase 1 turned out to be **already done**: `dex.h` has had `bSpA`/`bSpD` for all
151 all along. Only the accessors were missing (`Pet::spaStat/spdStat`,
`Party::spaOf/spdOf`) — added, so damage maths is unblocked.

Phase order: (1) ~~special stat accessors~~ · (2) ~~move storage on `Pet` +
`PartyMon`~~ · (3) ~~moveset UI~~ -- all **done**. Next: (4) `MoveEntry` ailment
fields · (5) damage + turn resolution, headless-testable in the emulator ·
(6) battle UI · (7) trainer roster + gyms + Elite 4 · (8) hard mode AI.

Note on (3): there is no level-up "you learned a move" prompt, and there should
not be -- 1907 of 2281 learnset entries are level 0, so it would almost never
fire. The moveset is edited on demand from card page 4 instead, which is both
simpler and closer to what was asked for.

### Training mechanics — deliberately unresolved

Training already exists and is already EV-shaped: `trAtk/trDef/trSpe` (`pet.h:47`)
capped by `trMaxFor(iv) = 70 + 30*iv/31`, feeding `calcStat()`. ATK trains via the
punching bag, SPE via the ball game, DEF passively (+1 per `DEF_TRAIN_TICKS` = 60
min of good wellbeing).

Open question: DEF has no active trainer, so its submenu row is currently inert.
Either give it a minigame or keep it passive and style the row as clearly
non-interactive.

### Swipe map redesign (requested, not started)

Target layout from the main screen:

| Gesture | Target | Status |
|---|---|---|
| Swipe up | **Player card** — player sprite (chosen by the user), badges earned | new, nothing exists |
| Swipe down | Current Pokemon status card | exists, but see conflict below |
| Swipe right | **Gym battles** | new, depends on the battle system |
| Swipe left | open — suggestion below | undecided |
| Swipe from the status card | **Moveset / moves known** | new; `moves.h` already has the move table + learnsets |

**Conflicts to resolve first** — the current bindings are not what the target
assumes (`onSwipeV`/`onSwipe` in `TamaPoke.ino`):

- Swipe **up** currently opens the Pokemon card; swipe **down** opens the clock.
  The target wants up = player card, down = Pokemon card, so the clock needs a new
  home (the menu `SETTINGS` row already opens it, so it may just lose the gesture).
- Swipe **left/right** currently page the Pokedex gallery. The gallery is now
  reachable from the menu, so the gesture is free — but `onSwipe` also handles
  card paging and gallery exit, so unpicking it needs care.

**Suggestion for swipe left: wild encounters.** It pairs with gyms on the right
(wild/grinding vs structured/progression), it is already half-designed in the
Roadmap above, and it gives the training stats somewhere to matter. Alternative if
that feels heavy: the berry/item bag, which currently has no home of its own.
