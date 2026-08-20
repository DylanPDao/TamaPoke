# TamaPoke — where things stand

Written 2026-08-20, mid-session, so work can resume after a restart.
Read this with `CLAUDE.md`; this file is the *current* state, that one is the
permanent knowledge.

---

## 1. What is live right now

| | |
|---|---|
| Published firmware | **v3.0**, live at https://dylanpdao.github.io/TamaPoke/web/ |
| Repo version | **v3.1** in `TamaPoke.ino` — bumped, **not published** |
| Your board | on **v3.0**, port moves around (`/dev/cu.usbmodem*`) |
| Live creature | Dratini, `iv=31/31/31/31 tr=100/100/100`, Charizard\* L100 banked |
| Branch | `feat/dex-expansion-phase0`, pushed, **no PR** |
| Tests | 33 suites; **3 failing right now** — see §4, they are expected |

**Everything merged and published today:** RETIRE, the move-picker TM gate, type
chips, the Hoenn sprite overflow, crash breadcrumbs, the egg region pill, the
sleep mechanic, `MISS`, and the `LVL` off-by-one.

### Save backups (all in `backups/`, gitignored)

    save-2026-08-20-dratini-maxed.txt          <- the current one, use this
    save-2026-08-20-pre-3.0.txt
    save-2026-08-19-charizard-MIGRATE.txt      <- the migration block
    save-2026-08-19-board2-marshtomp.txt       <- board 2's original game
    save-2026-08-18.txt

Restore = paste the whole block into the serial console at 115200, **including
the bare `IMPORT` line at the end**, which is the commit. Without it nothing is
written.

---

## 2. The dex expansion — exactly where it stopped

**Goal:** `DEX_COUNT` 386 → 493 (Sinnoh). Chosen because it is the only
generation where every piece exists: 100% sprite coverage, a `pret` disassembly
for verifiable gym rosters, and badge art already reachable.

### Done and working

- **Phase 0, committed.** `--check` over the whole table, `tools/check_sprites.py`,
  and `dexdata_test`. See `CLAUDE.md` § "Adding a generation".
- **Six Gen 1 evolutions linked** (committed): GOLBAT→CROBAT, ONIX→STEELIX,
  CHANSEY→BLISSEY, SEADRA→KINGDRA, SCYTHER→SCIZOR, PORYGON→PORYGON2.
- **The generators are now idempotent** — three consecutive `--emit` runs produce
  byte-identical files. They were append-only before and would have duplicated
  everything on a second run.
- **Data regenerated to 493** (uncommitted, in the working tree):
  `dex_data.py`, `dex_types.py`, `dex_stats.py`, `dex_learnsets.py`, `dex.h`,
  `moves.h`. `--check` reports **0 unexpected differences over 1..493**.
- **`--link` picked up Sinnoh's cross-generation evolutions by itself**:
  LICKITUNG→LICKILICKY, RHYDON→RHYPERIOR, TANGELA→TANGROWTH,
  ELECTABUZZ→ELECTIVIRE, MAGMAR→MAGMORTAR and one more. That is the rule working
  as intended — it needs re-running after every expansion.

### The pipeline, in the order it must run

    python3 tools/gen_dex_data.py --emit 493   # dex_data.py + dex_types.py
    python3 tools/fetch_pokeapi.py             # dex_stats.py + dex_learnsets.py
    python3 tools/gen_dex.py                   # dex.h        (needs the stats)
    python3 tools/gen_moves.py                 # moves.h      (needs learnsets)
    python3 tools/gen_dex_data.py --link       # link new cross-gen evolutions
    python3 tools/gen_dex.py                   # again, so the links land
    python3 tools/gen_dex_data.py --check      # must say 0 unexpected

`REGIONS` in `tools/dex_data.py` needs its row added by hand — Sinnoh's is
already in (`('SINNOH', 387, 493, [387, 390, 393])`).

---

## 3. Not started

- **Phase 2 — region gating.** Hide regions whose sprite pack is not on the SD:
  filter the Pokédex chooser, the egg region chooser and the egg pool. Agreed
  rules: creatures you already own always display; **no SD at all keeps today's
  behaviour** rather than an empty game; species with no art anywhere stay in the
  dex at their own number but are kept out of the egg pool.
- **Phase 3 — Sinnoh sprites, gyms, badges.** Pack with `pack_pmd.py`, verify
  rosters against `pret/pokeplatinum`, re-run `gen_badges.py`.
- **Trading.** Discussed, not started. `linkMonFrom`/`linkMonTo` already
  serialise a creature both ways, so the exchange is cheap; the hard part is
  atomicity (both sides commit or neither). **Do the radio bring-up first** —
  `linknow.cpp` has never executed on hardware, and building trading on an
  unproven transport means debugging two unknowns at once.

---

## 4. The three failing tests (expected, and each is a real task)

    dexdata_test   DARKRAI (491) has no same-type attack
    hit_test       "does not silently change the region either"
    roster_test    "one ladder per region, and none for ALL"

**`dexdata_test` — DARKRAI.** This is precisely the trap Phase 0 predicted:
`dex_moves.py` is 77 hand-picked moves, and a new generation's typings can leave
a creature unable to attack with its own type. Fix by adding a Dark attacking
move to `dex_moves.py` that Darkrai actually learns, then re-running
`gen_moves.py`. Do **not** just add Darkrai to the `NO_ATTACK` exception list —
that list is for species with no attacks in the real games either (cocoons,
Ditto, Unown, Wobbuffet, Smeargle). Darkrai is not one of those.

**`roster_test`.** `GYM_REGIONS` is 3 while `REGION_COUNT` is now 5. The test
asserts one ladder per region; Sinnoh has no roster yet and Kalos never will
under the current sourcing rule. The test needs to say "a ladder for each of the
first `GYM_REGIONS`" instead. This is a test that outgrew its assumption, not a
bug.

**`hit_test`.** Needs looking at properly — it is the egg region pill case, and
`REGION_COUNT` going 4 → 5 changed what cycling through the regions does.
`REGION_ALL` is generated as `4` now (`dex.h:572`) and is correct; check the
test's assumption rather than assuming the firmware is wrong.

---

## 5. Pitfalls — the ones that have actually cost time

### Build and test

- **A green emulator build does NOT mean the firmware compiles.** `build.sh`
  generates a `proto.h` with every prototype at the top; `arduino-cli` relies on
  auto-prototyping and rejects a function used above its declaration. This shipped
  once with 32 green suites. `tests/run.sh` now runs the real `arduino-cli
  compile` first — do not remove that.
- **Declare new sketch functions above their first use.** Same cause.
- **`const` at namespace scope is internal linkage in C++.** A table the tests
  need must be `extern const`, or the link fails with an undefined symbol.
- **Tests share one NVS store within a process.** A `Pet` built after another one
  slept will LOAD that sleep. Start each case from a known state (`sleep_test`
  has a `fresh()` helper for exactly this).
- **Negative-check every guard.** Break it on purpose and watch the test fail.
  This has caught a bad test roughly as often as a good one — `sprite_test`'s
  first version passed with the bug restored.

### Data and the dex

- **Anything holding a dex number is `int16_t`.** Never `uint8_t`, never the
  literal 151. This trap has fired five times: `evolvesTo`, `TrainerMon::dex`,
  `gen_moves.py`'s own `DEX_COUNT`, the Pokédex page cap, and `PmdMon::load` —
  which drew Ivysaur for Marshtomp and shipped in v2.8.
- **What hides it:** neighbouring code is usually already right, so the screen
  looks half-correct. `SdThumbs::get()` takes an `int16_t`, which is why the
  gallery was perfect while the creature on the main screen was somebody else.
- **A rule enforced in one path but not its twin** is the single most repeated
  mistake here. The TM gate (twice), the swipe paging bug (four times), the
  installer erase, the evolution threshold the card recomputed. Make every caller
  ask ONE function, and **test the caller, not just the rule**.

### The installer (both of these destroyed real saves)

- **The manifest must ship four parts at their own offsets**, never one merged
  image at `0x0` — `merge-bin` pads the gaps and writes 0xFF straight over NVS.
- **`new_install_prompt_erase` must be TRUE.** The name is the exact opposite of
  what it does: `false` means "do not ask, just erase the whole chip".
- `tools/check_installer.py` fails the build on either. `build_web.sh` runs it.

### Hardware

- **A stock board does not appear as a serial port at all.** Hold BOOT, tap
  RESET, release BOOT.
- **The board's RTC is months out** unless set (SETTINGS, or `RTCSET`). The sleep
  window depends on it; an unset clock never auto-sleeps, which fails safe.
- **Chrome holds the serial port** until the page that opened it closes. If
  `send_sd.py` says "Resource busy", close the installer tab.
- **Flashing over USB never touches NVS**; the web installer only leaves it alone
  because of the two fixes above.
- `EXPORT` before anything irreversible. It has been the safety net twice.

### The emulator

- `--save <path>` and `--wipe` keep experiments off your real save.
- `PANIC` / `WDT` at its console fake a crash so the boot report can be seen.
- It is launched here as a background process, so **it has no stdin you can type
  into** — drive it through a FIFO if commands are needed.
- It cannot see: timing, DMA tearing, PSRAM pressure, audio, battery, the radio,
  **touch accuracy** (synthetic taps are exact coordinates) or a missing
  `gfx->flush()`.

---

## 6. Older work still outstanding

- **LAN on two boards.** `linknow.cpp` has never executed. Treat the first
  session as debugging; `linkNowStats()` exists for it.
- **Battle background provenance** in `CREDITS.md` — the last licensing loose end
  before this goes public.
- **Audio by ear.** Nobody has judged the battle loop, the six cues, or whether
  `500 * vol` sounds linear.
- **A 24–48 h soak** on the current build. The last one passed at ~12 h.
