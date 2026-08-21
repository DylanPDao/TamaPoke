# TamaPoke — where things stand

Written 2026-08-20, updated 2026-08-21, so work can resume after a restart.
Read this with `CLAUDE.md`; this file is the *current* state, that one is the
permanent knowledge.

---

## 1. What is live right now

| | |
|---|---|
| Published firmware | **v3.0**, live at https://dylanpdao.github.io/TamaPoke/web/ |
| Repo version | **v3.2** in `TamaPoke.ino` — bumped, **not published** |
| Your board | on **v3.0**, needs the v3.2 flash (see §4a) |
| Live creature | Dragonair L45, `iv=31/31/31/31 tr=100/100/100`, Charizard\* L100 banked |
| Branch | `feat/dex-expansion-phase0`, **2 commits ahead of the push**, no PR |
| Tests | 33 suites, **33 passing** — first fully green run on this branch |

**Everything merged and published today:** RETIRE, the move-picker TM gate, type
chips, the Hoenn sprite overflow, crash breadcrumbs, the egg region pill, the
sleep mechanic, `MISS`, and the `LVL` off-by-one.

### Save backups (all in `backups/`, gitignored)

    save-2026-08-21-dragonair-L45.txt          <- the current one, use this
    save-2026-08-20-dratini-maxed.txt          <- Lv 14, an EARLIER state
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
- **Data regenerated to 493 — COMMITTED** in `486bacb`, not sitting in the
  working tree as an earlier draft of this file said: `dex_data.py`,
  `dex_types.py`, `dex_stats.py`, `dex_learnsets.py`, `dex.h`, `moves.h`.
  `--check` reports **0 unexpected differences over 1..493**.
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

## 4. The three failing tests — ALL FIXED (`739a41c`)

Suite is **33/33**. Kept here because two of them are worth knowing about.

**`dexdata_test` — DARKRAI.** Fixed by adding **DARK PULSE** to `dex_moves.py`.
BITE and CRUNCH were the only Dark moves and both are PHYSICAL, so it was never
just Darkrai — every special-attacking Dark type had no special STAB. Darkrai is
SpA 135 / Atk 90 and learns DARK PULSE at level 27. Not added to `NO_ATTACK`,
which stays reserved for Ditto/Unown/cocoons.

**A move's index is its position in `MOVES`, and saves store it RAW.** DARK
PULSE is appended *after* STRUGGLE, not filed under DARK, because inserting
mid-table shifts every later move by one and silently rewrites the moveset of
every saved creature. `dex_moves.py` now carries an `APPEND-ONLY BELOW HERE`
marker. **Any future move goes at the end.**

**`roster_test`.** Asserted `GYM_REGIONS == REGION_COUNT - 1`, which the dex
outgrows the moment a region has data but no roster — the normal state mid
expansion. Now asserts what matters: no ladder for ALL, the table is exactly
`GYM_REGIONS` long, every ladder has a roster and a name.

**`hit_test` — the one worth remembering.** Its "near miss" taps on the egg
region pill were 8 px from the graphic, *inside* the 16 px hit area. They were
direct hits cycling the region twelve times, and it passed only because
`12 % REGION_COUNT(4) == 0` came full circle. Sinnoh made it 5 and it broke. The
dead guard band CLAUDE.md says this test protects **had never been exercised** —
trap 3, a test proving arithmetic rather than firmware. It now taps in the real
band, derives the offset from the rects it already queries instead of copying
`EGGREG_PAD`/`EGGREG_GUARD` into the test, and checks after every tap.

---

## 4a. The overnight runaway, and the flash that is still owed (`91aa43c`)

A player woke to a Dragonair that had run away **after a night of correct
auto-sleep**. It went to bed with all four bars at zero, which armed
`neglectTicks` at 60 *before* the screen went off. The sleeping branch of
`tick()` returns before the neglect block, so the counter was neither counted
nor **cleared** for eight hours, and `canRunawayNow()` read it alone. On waking,
a creature at 100 energy was one tap from gone — and `FAR_BTN` (y176-234) sits
inside `inPetZone` (y95-310) and is checked before the caress, unconfirmed. The
next tick cleared it 60 s later.

`tick()` and `canRunawayNow()` now both ask `Pet::inTotalNeglect()`, so the
counter cannot outlive the state that earned it. `sleep_test` has the
morning-after repro plus a guard that a genuinely empty creature on waking is
still ready to leave. FW_VERSION and the README badge moved to **3.2**.

**The board is still on v3.0, so the bug is still live on it:**

    arduino-cli upload -p /dev/cu.usbmodem1101 \
      --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" .

Not done: `FAR_BTN` still overlaps `inPetZone`. The fix removes the cause, not
the delivery mechanism — it can now only fire on a creature genuinely empty
*right now*, which was judged the right line, but the geometry is still the
shape CLAUDE.md §4 warns about.

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

- **A move's index is its POSITION in `MOVES` (`dex_moves.py`), and saves store
  that index raw.** `gen_moves.py` does `idx = i + 1`; `Pet.moves[]` and
  `PartyMon.moves[]` write it straight to NVS. Inserting a move into its type
  section shifts every move after it by one and silently rewrites the moveset of
  every creature already saved — the live pet and every banked member, on every
  player's device. **New moves go at the end**, after STRUGGLE, under the
  `APPEND-ONLY BELOW HERE` marker. Same family as the box getting its own NVS key
  and badges being stored additively: never reinterpret bytes that already exist.
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
