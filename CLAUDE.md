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
- `BATTLE <dex> [lvl]` start a fight (debug entry until gyms exist)
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

### Done (branch `feat/battle-foundations`, pushed)

Emulator: touch was dead (`attachInterrupt` was a no-op so the `gTouchIrq` gate
never opened) and `--fast` scaled `millis()`, which the sketch also uses for
gesture timing, shrinking the tap window to `1500/scale` ms. Clock now runs 1x
during a gesture; `millis()` lives in `clock.cpp` so the tests share it.

Firmware: level caps at 100 (which also closes a `uint8_t` overflow the RTC's
two-week catch-up could reach), special-stat accessors, move storage on `Pet`
and `PartyMon` with a length-checked party migration, the moves card page and
on-demand picker, real level gates in the learnsets, level-up learn prompts,
`MoveEntry` ailment fields, the battle engine, the battle screen, and the gym
ladder with badges.

UI fixes: BOND label collided with its bar (label at x=70 size 2 = 12px a
character, bar started at 112 -- three characters, and BOND/LIEN/LACO are four).
The training submenu froze the panel because `renderTrain()` was the one render
path with no `gfx->flush()`. Card pages reordered. Gestures settled: up = the
creature's card, down = the player card, left = the gym ladder, right free.

Minigames: SPEED moved off the ball game onto its own reaction test, so playing
is no longer a stat grind. Three bugs in a row came from the ball game quietly
discarding sessions -- the header tap forfeited (and the ball reaches y=28, well
inside the y<72 quit strip, so reaching for a high ball quit the game), then the
swipe exit forfeited too, and speed trained at `score/5` which integer-divides
to zero below 5 points. All three minigames now bank what was earned on exit.

**Two tests exist because screenshots could not catch these:**
`shotMode` reads `gfx->buffer()` directly and never consults `frameReady`, so a
missing flush is invisible to every capture. One test opens all 13 screens and
asserts each flushes; another checks the 6 x N i18n table for nulls and
non-ASCII. Both live in the scratchpad, NOT the repo -- see below.

### Next up, roughly in order

**0. Soak test -- the one item that can INVALIDATE work rather than add to it.**
24-48 h on hardware with `HEALTH`. Everything built this session is verified in
the emulator only, and the emulator explicitly cannot see timing, DMA tearing,
PSRAM pressure, audio or battery. Since anything last ran on a board this branch
added two streamed battle sprites (~270 KB PSRAM), 315 KB of backgrounds, and a
full-screen background redraw every frame at 10 fps. Needs the board; cannot be
done from here.

**A2. Smaller gaps worth closing.**
- ~~Move relearner~~ **not needed** -- checked: `learnableList()` already lists
  every move learnable at the current level, level-gated ones included, so the
  moves picker recovers a declined move. Verified by declining EMBER/GROWL/LEER
  and finding FLAMETHROWER and WING ATTACK still listed.
- ~~See a banked creature's moves~~ **done** -- tapping a party slot opens its
  sheet: moves with type and power, typing, and the four combat stats.
  `drawMoveRow()` takes a dex now rather than assuming the live pet, so STAB is
  coloured against the creature you are actually looking at.
- **A reason to rematch.** Beating a leader gives a badge and nothing else.
  Berries, or a training/IV reward, would make the ladder replayable instead of
  a checklist.
- **Save backup.** A run is weeks of real time in one NVS partition. `LS`/`PUT`
  already move files over USB, so an export/import command would be small.

**A. Audio and the win screen -- BACKEND DONE, one UI piece left.**

- ~~Win screen~~ **done**: the badge at 3x with its hard-mode halo, the leader
  named, NEW BADGE! when it is the first time, and the running count.
- ~~Attack sound effects~~ **done**: `SFX_HIT`/`BEAM`/`STATUS`/`SUPER`/`FAINT`/
  `VICTORY`, chosen from the `TurnLog` so the cue can never disagree with what
  happened.
- ~~Battle music~~ **done**: `MUS_BATTLE` loops during a fight, `MUS_VICTORY`
  plays on a win. There is one square-wave voice and one blocking audio task, so
  music is NOT mixed with effects -- the task plays the tune a note at a time and
  hands the voice to any effect that arrives. Effects therefore cut through,
  which is the right priority anyway.
- ~~Volume~~ **done**, backend and UI. `audioSetVolume(0..10)` stored under
  `"vol"` and applied as the square wave's amplitude; the settings row is
  `SND ON | - | VOL n | + | EN >`, with a level bar and both ends clamped. The
  sound switch stays the master; volume is how loud it is when on, and 0 is
  silence without disabling the system.

**A3. Audio is UNHEARD.** All of the above is verified only by compiling and by
clicking through the emulator, which has no audio at all -- `host_impl.cpp`
stubs `sfxPlay` to nothing. Nobody has heard the music loop, the six cues, or
the volume curve. The amplitude scale in particular (`500 * vol`) is a guess at
what sounds linear. This is part of what the soak test is for.

**B. Storage and the box -- DONE.**
- The box is 18 slots (3 pages of 6) under its OWN NVS key, not a bigger party
  blob. That was deliberate: growing the party blob changes its stride, and the
  length-based migration in `begin()` cannot tell a stride change from a
  slot-count change, so an existing party would have been read back misaligned.
  A separate key is purely additive and cannot corrupt anything -- `box_test`
  checks a pre-box save keeps its whole party and comes up with an empty box.
- `swapPartyBox()` is one call for deposit, withdraw and exchange, since any of
  the two slots may be empty. Reached by tapping a party slot then BOX.
- Room to grow: 6 + 18 records is 720 B against a ~4000 B single-blob limit, so
  the box could reach ~120 before it would need splitting.

**B2. Multi-region, once the Gen 2/3 expansion is untabled.** These four hang
together and should be designed as one thing, not bolted on separately:
- **Region egg switcher** -- choose which generation your eggs come from, so a
  player can run a Kanto game, a Johto game, or mix. Touches `pickEggSpecies()`
  and the rarity tiers, both of which currently assume one flat 1-151 pool.
- **Badges per region** -- `badges`/`badgesHard` are `uint16_t` bitmasks with
  room for 16, so a second region fits, but a third needs widening or an array.
  `badges.h` is Kanto-only; `gen_badges.py` already handles any of the five
  regional SVGs upstream, so the art side is a re-run.
- **Gyms and an Elite 4 per region** -- pure data in the `trainers.h` shape.
- **Swipe left becomes a chooser**: LAN battle or gym battle; gyms then go
  region -> leader. That replaces today's flat list and is what makes multiple
  regions navigable at all.

**C. Multiplayer.** See the peer-to-peer section below -- ESP-NOW, one
authoritative device, forced by the 11 `random()` calls a turn.

**D. Licensing, before this repo goes public.** `CREDITS.md` records that the
twelve battle backgrounds in `tools/backs/` have **no established provenance**.
Everything else is accounted for (PMD sprites CC BY-NC, badges CC BY 3.0). Either
confirm their licence or replace them.


0. **Fight UI polish** toward the mainline look: HP plates with an `HP` label,
   numeric HP on your own side, and a platform ellipse under each creature.
   Reference supplied by the user; the current layout already matches the
   mainline arrangement (foe top-left info / top-right sprite, you bottom-right
   / bottom-left).
1b. ~~Kanto badge art~~ **done**. `tools/gen_badges.py` needs `rsvg-convert`
   (`brew install librsvg`) and regenerates `badges.h` from the upstream SVG.
   A hard-mode clear draws a golden halo behind its badge.

1. ~~Battle animations~~ **done**, including real PMD playback. `btlPmd[2]`
   streams both combatants (~135 KB PSRAM each, freed when the fight ends) and
   plays `PMD_ATTACK` while lunging, `PMD_HURT` while flinching, `PMD_IDLE`
   otherwise -- each guarded by `has()`, since not every species ships every
   action, falling back to idle and then to the flat thumbnail with no SD.
   The player's slot is NOT the global `pmd`: the active creature may be a
   banked party member rather than the live pet.
2. **Trainer name.** There is no player name -- the player card is titled with
   the generic `S_TRAINER`. Needs `char trainerName[12]` on `Pet` (player-wide,
   so `newEgg()` must not clear it, like `badges` and the streak) plus save/load.
   The keyboard exists and is reached by tapping the pet's name on card page 0,
   but commit hardcodes `pet.rename(nameBuf)` (`TamaPoke.ino:3156`), so it needs
   a target flag before a second caller can share it. Entry: tap TRAINER.
3. **Box 6 -> 18** (3 pages of 6). `S_PARTY_FMT` hardcodes "%u/6" in all six
   languages, the party screen needs paging, and **`Party::begin()` must be
   re-keyed off `sizeof(PartyMon)` first** -- it infers the old record size as
   `stored / PARTY_SLOTS`, right when the stride grows and wrong when the slot
   count does, so 180 bytes over 18 slots would infer a 10-byte record.
4. **Peer-to-peer** (see below) -- the biggest, and the only one needing a
   hardware subsystem that has never been brought up.

### Done since the battle plan

Hard mode + battle AI. Both ladders cap your level to the leader's best (without
it a raised team walks everything at 100% and the type chart never matters);
hard also caps team SIZE, uses `HARD_IV` 31 and switches the AI on. Caps are
applied while building the combatants, so the stored creature is never touched.
`aiChooseMove` beats the random chooser **74%** of mirror matches -- `ai_test`
measures that rather than assuming it.

Team select, so which creature you bring is a real decision now that hard mode
caps the size. Player card paging (badges + avatar, then medals). The reaction
test for SPEED, splitting stat training out of the joy game.

### Expanding past Kanto (TABLED -- analysis kept so it need not be redone)

Feasible, and cheaper than it looks. **SpriteCollab already covers Gen 2 and 3**
under the same CC BY-NC licence already in use -- dex 152, 252 and 384 all
return HTTP 200 from the existing `pack_pmd.py` URL. There is no need for
ripped assets from elsewhere; `ZeChrales/PogoAssets` is Niantic art with no
clear licence and would undo the care in CREDITS.md.

What changes for 386 species:

- `dex.h`, `moves.h` and the learnsets **regenerate** -- `gen_dex.py` already
  loops `range(1, DEX_COUNT + 1)` and `fetch_pokeapi.py` fetches by number.
- `dexReg[19]`/`dexShinyReg[19]` -> `[49]`. This migrates safely on its own: a
  shorter stored blob reads into the front of the bigger array, so bits 1-151
  keep their meaning.
- **Eight places use the literal `151` instead of `DEX_COUNT`** -- `pet.cpp`
  lines ~243, 256, 283, 295, 572, 592 and `pet.h` `isRegistered`/
  `isShinyRegistered`. These are the ones that will bite.
- `"POKEDEX %u/151"` is hardcoded in all six languages.
- Sprites on the SD go 40 MB -> ~100 MB. Fine on a card.
- **The blocker is the web installer**: `sprites.pak` goes 58 MB -> ~150 MB,
  which is not practical to flash through a browser. Split it by region or make
  the sprite load optional before attempting this.
- `dex_moves.py` is 77 moves hand-picked so every *Kanto* typing has a STAB
  option; Hoenn adds species that would need coverage added.
- Johto/Hoenn gyms are pure data, in the shape `trainers.h` already uses.

### Player-wide vs per-creature state

Badges (easy and hard), the avatar, the daily streak, the Pokedex bitmaps and
`totalMedals` belong to the PLAYER and outlive every pet. `newEgg()` must never
clear them -- `persist_test` proves they survive all three endings plus a
reload, so adding a reset there will now fail the suite rather than quietly
erase a run.

The one thing that does take them is `WIPE` (`factoryReset()` -> `prefs.clear()`),
which is a factory reset and is meant to.

### Tests

```bash
bash tools/emu/tests/run.sh          # all 10 suites
bash tools/emu/tests/run.sh battle   # just matching ones
```

They compile the REAL sources against the emulator stubs, so they assert against
`Pet`/`Party`/`Combatant` themselves rather than restating their rules. Two exist
because nothing else can catch what they catch:

- `flush_test` -- a screen with no `gfx->flush()` leaves the panel frozen, and
  screenshots are structurally blind to it: `--shot` reads `gfx->buffer()`
  directly and never consults `frameReady`. The training submenu shipped frozen
  exactly this way.
- `i18n_test` -- `STRINGS` is positional, so a short language row is zero-padded
  by the compiler with no diagnostic, shifting every later string in that
  language only.

Run them after touching `pet.cpp`, `battle.cpp`, `i18n.cpp` or any render path.

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
- **Moves are player-chosen**, and editable on a banked creature too. On
  level-up the player picks which of the 4 to forget; it is never automatic.
  Moves were originally FROZEN at banking to give the farewell weight, but that
  left a creature banked with a poor set useless forever -- which fights hard
  mode, where coverage decides the run. A banked one is edited from its party
  sheet, and is limited to what it could have learned at its frozen level.
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
- **The ladder is sequential** (this REVERSES the earlier "no gating, attrition
  is the gate" rule). A leader opens once the previous is beaten, tracked
  separately per difficulty so hard mode is its own run. The original rule was
  written before both ladders were level-capped; once they were, nothing stopped
  you opening on Lance and simply losing, which reads as a dead end rather than
  a challenge. Attrition still does the work WITHIN a fight.
- **Level caps at 100** (`MAX_LEVEL`), reached at 4d 3h. `MINUTES_PER_LEVEL`
  stays 60 — compressing the curve to force 100 into a 3-day life would be a
  balance change that buys a number you can already reach by playing on.

Phase 1 turned out to be **already done**: `dex.h` has had `bSpA`/`bSpD` for all
151 all along. Only the accessors were missing (`Pet::spaStat/spdStat`,
`Party::spaOf/spdOf`) — added, so damage maths is unblocked.

Phase order: (1) ~~special stat accessors~~ · (2) ~~move storage on `Pet` +
`PartyMon`~~ · (3) ~~moveset UI~~ -- all **done**. Next: (4) `MoveEntry` ailment
~~fields~~ **done** · (5) damage + turn resolution, headless-testable in the
~~emulator~~ **done** (`battle.h`/`battle.cpp`) · (6) battle UI ·
~~(6) battle UI~~ · ~~(7) trainer roster + gyms + Elite 4~~ -- **done**.
Next: (8) hard mode AI, and a team-select screen.

The ladder uses the real FireRed/LeafGreen teams and levels, unrescaled, and
they land almost perfectly on this game's curve. Measured solo win-rate for one
perfect-IV Charizard, 40 runs each:

| your level | gyms 1-4 | gyms 5-7 | Giovanni | Elite 4 | Champion |
|---|---|---|---|---|---|
| 40  | 90-100% | 0%      | 0%     | 0%     | 0%  |
| 60  | 97-100% | 50-75%  | 0%     | 0-15%  | 0%  |
| 73 (a full 3-day life) | 100% | 82-97% | 7% | 0-32% | 0% |
| 100 | 100%    | 100%    | 52%    | 55-92% | 42% |

So one creature clears the eight gyms over a normal life and still cannot take
the Elite 4 -- exactly what "no gating, attrition is the gate" was meant to do.
A banked team is the answer, which makes farewells matter.

Team-select is NOT built: the squad is the live pet plus the first five banked
members in order. It only bites when you hold 7 candidates.

### Hard mode (designed, not built)

Not "the AI cheats". Hard mode **removes overlevelling as a strategy** so the
fight is about type matchups, movesets and decisions:

- **Team size is capped to the opponent's.** Brock brings 2, so you bring 2.
- **Levels are capped to the opponent's highest.** A level 100 creature fights
  Brock at 14.
- Opponents roll `HARD_IV` (31) instead of `EASY_IV` (16), already in trainers.h.
- The AI actually chooses (see phase 8) instead of `random()`.

Both caps are applied when building the squad, not to the stored creature --
nothing is written back, exactly like ailments. `badgesHard` already exists on
Pet and is tracked separately from `badges`.

### Peer-to-peer battles (designed, not built)

The S3 has WiFi and BLE; neither is currently brought up anywhere in the
firmware. **ESP-NOW** is the fit: peer-to-peer, no router, ~250-byte payloads,
and a `Combatant` is only ~40 bytes.

**One device is authoritative.** The host owns the whole battle state and runs
`battleAct()`; the guest only sends a move index and renders what it is told.
This is not a preference -- `battle.cpp` makes **11 `random()` calls per turn**
(crit, damage spread, accuracy, ailment procs, multi-hit count, confusion,
thaw, wake, speed ties). Two devices resolving independently desync inside a
single turn, and a shared seed only papers over it until the builds differ by
one `random()` call. Sending resolved outcomes cannot drift.

Wire format, roughly:

1. `HELLO` -- firmware version + protocol version. Refuse a mismatch loudly;
   a silent desync is far worse than a refusal.
2. `SQUAD` -- each side sends its team as `Combatant`s (dex, level, the five
   stats, 4 moves, name). ~40 bytes each, up to 6.
3. Per turn: guest sends `MOVE <slot>`; host resolves and replies with the
   `TurnLog`s plus both HP/ailment states. `TurnLog` already carries everything
   needed to narrate, which is why the guest needs no game logic at all.
4. `END` -- winner.

Costs to weigh before starting: the WiFi stack is ~40-50 KB RAM (there is
headroom -- currently 10% used), meaningful current draw on a battery device,
and pairing UX on a touch-only screen.

### Box size (if the party grows past 6)

`sizeof(PartyMon)` is 30 bytes and the NVS partition is 20 KB (`0x5000`). A box
of 100 is 3000 bytes and still fits one NVS blob; 151 (4530) exceeds the ~4000
byte single-blob limit and would need splitting across two keys. RAM is a
non-issue.

**Fix the migration first.** `Party::begin()` infers the old record size as
`stored / PARTY_SLOTS`, which is right when the stride grows but wrong when the
slot count does: 180 stored bytes over 30 slots would infer a 6-byte record and
destroy the party. Key it off `sizeof(PartyMon)` before changing PARTY_SLOTS.

Note on (6): the battle screen is a 2x2 move grid, not four stacked rows --
the round panel has to fit both creatures, both HP bars and the menu. The only
way into a battle right now is the serial command `BATTLE <dex> [level]`; it
gets a real home in (7). The foe is built through `Pet` so it uses the same stat
formula and the same learnset-driven moveset as the player, rather than
special-cased enemy maths that could quietly diverge. Foe move choice is
`random()` for now -- that IS phase 8.

**Fight length was checked and is NOT a problem** (an earlier note here claimed
otherwise; it was wrong). 240 simulated L50 fights average 3.1 turns, and that
matches the real games: a 1v1 at equal level there is also 3-5 turns. HP already
lands exactly on the canonical value (Charizard L50 = 153 both ways), and while
the other stats sit ~40% high -- the deliberate deviation `calcStat()` documents,
so newborns do not show single digits -- damage depends on the A/D *ratio*, which
is preserved: 1.02 ours vs 1.03 real. Do not "fix" this; it would make combat
less faithful, not more. Length in a gym comes from fighting six in a row.

Note on (4): `MoveEntry` gained `ailment` + `ailChance`, and the pair is
OPTIONAL in `dex_moves.py` -- only the 17 moves that inflict one spell it out,
`gen_moves.py:unpack()` defaults the rest. Adding an ailment is a one-line edit.
There is no dedicated status move (no THUNDER WAVE, no SLEEP POWDER), so
ailments ride as secondary chances on damaging moves. `AIL_SLEEP` exists in the
enum but nothing inflicts it yet -- adding TOXIC/THUNDER WAVE/SLEEP POWDER would
mean new rows in `dex_moves.py` plus a re-run of `fetch_pokeapi.py`, which is now
cheap since `tools/pokeapi_cache/` is warm.

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
