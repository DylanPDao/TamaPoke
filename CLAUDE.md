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

The battle system is BUILT -- turn- and move-based, with the gym ladder and the
Elite 4 on two difficulties. The old line here ("resolution by ATK/DEF/SPD,
battle style not yet chosen") described a design that was superseded and is kept
only in the § "Battle system" note explaining why.

What is left is the 24-48 h soak test on `HEALTH`, and **wild encounters**, which
were never built: there is no way to meet a creature outside a gym. That is the
one substantial piece of unimplemented game left, and it now has nowhere obvious
to live -- every gesture from the main screen is taken (see below).

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

~~**Trainer avatars need redrawing.**~~ **done** -- and the licensing call was
made deliberately, so do not quietly reverse it. `avatars.h` is now eight real
Gen 1 overworld sprites generated by `tools/gen_avatars.py` from the
`pret/pokered` disassembly, replacing the four hand-drawn char maps (removed
from `species.h`, along with a comment that had become false).

**This is unlicensed Nintendo art**, unlike the PMD sprites (CC BY-NC) and the
badges (CC BY 3.0). The owner asked for the real sprites after being shown the
trade-off. `CREDITS.md` records it explicitly and recommends shipping
`gen_avatars.py` rather than `avatars.h` if the repo is published -- the script
needs only `curl`, so users fetch the art themselves, exactly the arrangement
already recommended for the PMD sprites.

Notes for anyone regenerating: the sheets are 2bpp greyscale (shade 3 = white =
transparent), six frames stacked, and only frame 0 is used. **Do not pick
`swimmer`** -- the character is in water so the sprite sits low in its cell and
reads as misaligned once it is out of context; `sailor` replaced it. `avatar`
is a `uint8_t` under `"avtr"`, now taken modulo `AVATAR_COUNT` in all three
places that used to mask it with `& 3`, and `pet.cpp` clamps an out-of-range
value so a save from the four-avatar era still loads.

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
- ~~A reason to rematch~~ **done**. A gym win now trains the creature that
  fought: 3-5 points on easy, 6-10 on hard, +1 per three leaders deeper into the
  ladder. `Pet::rewardTraining()`.

  Two rules make it work rather than annoy. The stat is random, but **only among
  stats that still have headroom** -- a random grant landing on a maxed stat
  would silently evaporate and read as a bug rather than as luck, and
  `reward_test` fails if the choice is widened. And it never crosses the
  IV-bound ceiling, since a mediocre individual not reaching as far is the point
  of `trMaxFor()`.

  It goes to the LIVE pet only, and only if it was in the squad (`btlPetIn`):
  banked members are frozen at what they were banked with. No cooldown was added
  -- battling already costs the live pet energy, which is the designed
  rate-limit. A fully trained creature is told so instead of seeing nothing
  happen. Balance change, so `README.md` and `FW_VERSION` moved to 2.3 with it.
- ~~Save backup~~ **done**. `EXPORT` prints the whole save as a block of
  `IMPORT <hex>` lines, and pasting that block back is the restore -- there is
  no second format to get wrong and no 2000-character line for a terminal to
  mangle. About 1.2 KB, 16 lines.

  `save.cpp` is KEY-DRIVEN: `SAVE_FIELDS` lists all 51 keys with their types and
  both directions walk that one table through the ordinary `Preferences` API, so
  the identical code runs on the board and in the emulator. A struct of fields
  would have been a second description of the save that drifts the moment
  somebody adds one. `save_test` compares the table against the keys actually
  present after a save, so a forgotten key fails a test instead of silently
  vanishing from every player's backup -- and that check has to run against a
  store the FIRMWARE wrote, not one a restore produced, or it validates itself
  (it did exactly that until it was moved).

  An import VALIDATES THE WHOLE BLOB before touching NVS: magic, version,
  checksum, and every length. A half-applied restore over a good save would be
  worse than having no backup. It also clears first, so a restore replaces a
  save rather than merging with it. `console_test` drives the real hex out and
  back through `handleSerial()`, including a mistyped digit, an odd-length line
  and a bare commit.

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
- A farewell now falls through party -> box, and only a full party AND a full
  box makes the player choose who to replace. That is what the box is for.

**B3. Bringing a banked creature back -- DONE, frozen.** `Pet::reviveFrom()`
makes a banked creature the live pet as a permanent companion: it does not age,
cannot evolve, and is never offered a farewell or able to run away. Its cost is
that its level never rises again.

`ageMinutes` is simply set to match the banked level rather than adding a second
source of truth, so `level()` needs no special case. Offered ONLY while an egg
is waiting -- otherwise it would silently destroy whatever creature is alive,
and the button says why when it is greyed.

Note for anyone reading the farewell timing: 3 days is when it is first OFFERED
(level 73), not when the creature is finished -- 100 comes at 4d 3h, and
declining re-offers a day later.

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

**C. Multiplayer -- WRITTEN END TO END, RADIO NEVER RUN.**

`link.h`/`link.cpp` hold the whole state machine: hello with a version check,
squad exchange, a guest move, a host result, an end. The transport is a function
pointer, NOT a direct ESP-NOW call, so `link_test` cross-wires two `Link`s in one
process and exercises the entire handshake without a radio. That paid for itself
immediately -- see below.

**Hardened against a real radio (the "bulletproof" pass).** ESP-NOW is best
effort, and the first version assumed delivery everywhere. Three rules now cover
it, all in `link.h`'s header comment: every exchange is stamped with a turn
number so a resend can never be read as a new choice; whatever we last said is
resent until superseded; and `LinkResult` carries ABSOLUTE state, never deltas,
so a guest that misses a turn entirely still lands on the right numbers from the
next one. Every wait has a deadline -- `LINK_LOST` with a message, never a hang.

`lossy_test` is what makes this provable without boards: it drops, duplicates
and silences frames on purpose and asserts twelve turns still complete. It found
two bugs that reading could not:

- **A deadlock.** A side that reached READY stopped answering a peer still
  assembling its squad, and only the side that is behind resends -- so one lost
  SQUAD packet stalled the pair permanently. A finished side now answers a late
  hello with its squad (and NOT another hello, or the two volley forever).
- **A livelock.** Pairing settles into a burst of a fixed length; with one frame
  in three dropped, the same POSITION in the burst died every time and the
  resends never helped -- ten attempts, same packet, always. Squad packets are
  now sent in a ROTATING order so no slot can stay unlucky. Jittering the timer
  does not fix this on its own: the loss is per packet, not per millisecond.
  Real interferers (beacons, microwaves) are periodic, so this is not academic.
  Pairing went from 35 frames to 11 as a side effect.

Also settled: two hosts (or two guests) resolve by id, the higher one hosting,
so the buttons are a preference rather than a trap -- identical ids refuse
rather than guess. A build fingerprint of the table sizes rides in the hello, so
two builds whose `MOVE_TBL` differs refuse instead of narrating different moves.
And NOTHING off the wire is trusted to index a table: `linkMonTo()` clamps dex,
level and every move index, and terminates a name that arrived without a NUL.
`MOVE_TBL[r.hostMove]` on the guest was a straight out-of-bounds read before.

`linknow.cpp` now locks onto the first peer's MAC and unicasts to it, which
stops two pairs of players in one room from joining each other's fights and buys
a real transmit-status callback (a broadcast always reports success). Received
packets are parked in a ring by the WiFi-task callback and drained by
`linkNowPoll()` on the main loop, so protocol state is never touched from
another task and nothing sends from inside the receive callback.

UI: the host LATCHES its own action instead of discarding it when the rival has
not chosen (that made you jab at the move until the timing lined up), both sides
show "waiting for the rival", the guest can now switch by ASKING -- a switch
rides the same message as a move -- and a finished fight returns to the LAN
screen where AGAIN rematches with the squads both sides already hold. Leaving
sends a goodbye so the peer reports at once rather than waiting out a timeout.

Everything around the radio is now built and tested: `linknow.cpp` (ESP-NOW
broadcast), the LAN screen (`renderLan`/`lanOffer`/`lanTap`, reached from a
button on the gym list), and the battle wiring. `btlResolve()` takes the foe's
move from `lan.pendingMove` instead of the AI when `btlLink` is set, ships a
`LinkResult` to the guest, and `btlLinkPoll()` applies it on the guest's side
once a frame. `lan_test` covers that half; `link_test` covers the protocol.

Design points worth not undoing:
- **The squad is rebuilt from `lan.mine`, not from `squadMask`.** What you fight
  with must be exactly what the peer was told you have. Rebuilding from the
  party would silently diverge if anything changed between offering and
  starting -- `lan_test` fails if you switch it back.
- **The peer's team is held as live `Combatant`s** (`btlFoeSquad`), not rebuilt
  from `lan.theirs` each time. A trainer's replacements only ever arrive once; a
  linked opponent can switch out and back, so its creatures must remember how
  battered they are or switching would heal them.
- **An action is one message**, a move slot or a switch with the high bit set,
  because turn matching and resend must have a single path. A move is stored as
  slot+1 so that 0 can keep meaning "nothing chosen yet" -- storing it raw made
  move slot 0 indistinguishable from silence.

Still to do:
- **Run it on two boards.** `linknow.cpp` has never executed. Channel choice,
  delivery, the WiFi/PSRAM interaction and the current draw are all unverified.
  Treat first bring-up as debugging, not as confirmation. `linkNowStats()` was
  added for exactly that first session: rx, tx, tx failures, packets dropped as
  another pair's, and ring overflows, printed on `linkNowEnd()`.
- ~~Team select~~ **done**. HOST/JOIN now opens the same picker the gym ladder
  uses, with `PICK_LAN` (0xFF) as the trainer index -- `squadCap()` already
  returns an uncapped six for anything past the roster, so a LAN battle is
  uncapped by construction rather than by a special case. **Uncapped is the
  decision**: two players who know each other should be able to bring what they
  like, unlike hard mode where the caps are the point.

  `lanOffer()` builds the squad BEFORE bringing the radio up, so what is
  advertised is exactly what was just chosen whether or not the radio comes up.
  `lan_test` drives the picker's FIGHT button and asserts `lan.mine` holds the
  chosen two rather than the whole party -- it fails if the offer is rebuilt
  from anything but `squadMask`.

**The synchronous test transport caught a real re-entrancy bug.** `start()` and
the hello handler both SENT before updating their state, so a reply that arrived
during the call found the sender still `LISTENING` and it answered again --
forever. A real radio is asynchronous and might have hidden this until two
devices with a fast link met. State is now set before sending, and that ordering
matters anywhere `put()` can re-enter.

**D. Licensing, before this repo goes public.** `CREDITS.md` records that the
twelve battle backgrounds in `tools/backs/` have **no established provenance**.
Everything else is accounted for (PMD sprites CC BY-NC, badges CC BY 3.0). Either
confirm their licence or replace them.


0. ~~Fight UI polish~~ **done**. HP plates carry the `HP` label and your own
   side shows numeric HP (`btlSide`). The platform ellipse was dropped on the
   user's call, not forgotten. The layout matches the mainline arrangement:
   foe info top-left / sprite top-right, you bottom-right / bottom-left.
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
2. ~~Trainer name~~ **done**. `Pet::trainerName` is player-wide and outlives
   every ending; tap the title on the player card to set it. The keyboard now
   takes a target (`KB_PET` / `KB_TRAINER`) rather than hardcoding
   `pet.rename()` on commit, so two callers can share it.
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

### Expanding past Kanto -- PHASE 1 DONE (the dex is 386)

Feasible, and cheaper than it looks. **SpriteCollab already covers Gen 2 and 3**
under the same CC BY-NC licence already in use -- dex 152, 252 and 384 all
return HTTP 200 from the existing `pack_pmd.py` URL. There is no need for
ripped assets from elsewhere; `ZeChrales/PogoAssets` is Niantic art with no
clear licence and would undo the care in CREDITS.md.

**Phase 1 landed: the data.** `DEX_COUNT` is 386, generated end to end.
`tools/gen_dex_data.py` derives dex_data/dex_types from PokeAPI and its
`--check` reproduces the hand-written 151 with zero unexpected differences,
which is what makes it trustworthy for the 235 new ones. Gen 1's entries are
copied through byte for byte and never regenerated -- they are not internally
consistent (stone evolutions vary 30 vs 36 with no rule) and they are already
live in people's Pokedex.

Rarity for the new species is `capture_rate <= 45` among base forms, which
reproduces 23 of the 27 the Gen 1 set picks by hand; the four it misses
(Growlithe, Ponyta, Grimer, Rhyhorn) were chosen for being uncommon in game,
which no data can tell you. Legendary comes straight from PokeAPI.

Three bugs the expansion exposed, all of which had been silently fine at 151:
- `DexEntry::evolvesTo` was `uint8_t`, so every evolution target above 255
  overflowed. Now `uint16_t`.
- `gen_moves.py` had its own `DEX_COUNT = 151`, so it emitted a Kanto-sized
  `LEARN_OFS` while dex.h had grown -- every lookup past 151 would have read
  off the end. It derives the count from `dex_data` now.
- **The Pokedex was capped at 10 pages** (`if (np > 9) np = 9`), which hid
  everything past dex 160. `swipe_test` now walks to the last page and fails if
  any species is unreachable, and the dot row became a page number because 25
  dots do not fit the round panel.

**Phase 2 landed: the egg region.** See § "Choose which region your egg comes
from" in the README for the two anti-farming rules.

**Phase 3 landed: three ladders.** `trainers.h` holds `TRAINERS_KANTO/JOHTO/
HOENN` behind `TRAINER_SETS[GYM_REGIONS]`, and the gym screen changes ladder on
a vertical swipe -- the same gesture the Pokedex uses, and the swipe-left
chooser that was once planned is not needed. `TrainerMon::dex` had to widen to
`uint16_t` (Hoenn runs past 255, the same trap `evolvesTo` fell into).

Badges are stored ADDITIVELY: Kanto keeps `badges`/`badgesHard` under the keys
it has always used, and Johto/Hoenn live in `badgesX`/`badgesHardX` under new
ones. Widening the originals would have meant reinterpreting an existing save;
this cannot. It is the same reasoning that put the box under its own key rather
than growing the party blob. Every read goes through `badgeMask(region, hard)`,
and the running fight keeps `btlRegion` separately from `gymRegion` so that
leaving the gym list mid-battle cannot retarget the badge.

**All three rosters are VERIFIED against the games.** `tools/verify_rosters.py`
diffs Johto and Hoenn against `pret/pokecrystal` and `pret/pokeemerald` -- the
disassemblies, which are the games' own tables and so beat any wiki. It reports
**0 differences across all 26 trainers**. Re-run it after touching a roster.

It found ten, which is why it exists: Lance was missing his Charizard and had
Dragonair where Dragonite belongs, Roxanne was two levels high, Norman and
Winona were the wrong games' teams entirely, and four trainers had their teams
in the wrong ORDER, which matters because the first slot is who leads.

**Hoenn is EMERALD throughout**, and that follows from Juan being the eighth
leader: in Ruby/Sapphire that seat is Wallace's and Steven is champion, while in
Emerald Juan takes the gym and Wallace the title. Mixing them would have given a
ladder that exists in neither game. Emerald's Steven is a post-game rematch at
level 77 and is deliberately absent.

Two findings worth keeping: Johto's leaders really are Kanto-heavy (13 of 49
creatures are Johto natives, against Hoenn's 47 of 57), and Pryce really is
weaker than Jasmine in Gold/Silver, so the ladder dips there on purpose.

**Badge art is done for all three.** `gen_badges.py` now fetches Kanto, Johto
and Hoenn from upstream itself and emits `BADGES_ART[BADGE_REGIONS][8]`; the
player card gained a badge page per region (`PLAYER_PAGES` is `GYM_REGIONS + 1`,
so the page you are on IS the region and no extra control was needed). Sinnoh
and Unova are one line away in `REGIONS` there.

One fix was needed to isolate them: the column finder assumed Kanto's cleanly
separated layout, and Johto's and Hoenn's sheets have neighbours that touch into
a single wide span. It now splits anything much wider than the median column
rather than demanding a layout only Kanto has.

Still to do: phase 4 (sprites, and the web-installer size problem below).

What changed for 386 species:

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

**`swipe_test` exists because the same bug was shipped four times** -- the move
picker, the player card, the gym list and the box each closed on a horizontal
swipe instead of paging, and each was found by hand rather than by a test. It
now drives `onSwipe(-1)` against every paged screen and asserts the page
advanced AND the screen stayed open. **Any new paged screen must be added to
it**, or this will happen a fifth time.

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

~~Open question: DEF has no active trainer~~ **settled** -- kept passive and
styled for it. `renderTrain()` draws the DEF row flat in `UI_TRACK` and the tap
handler skips it (`passive = (i == 2)`), so it reads as information rather than
a dead button. DEF still trains by itself, +1 per `DEF_TRAIN_TICKS` of good
wellbeing.

### Swipe map -- DONE, do not re-plan it

This section used to hold a target layout and a list of conflicts. All four
gestures are now bound, and the conflicts were resolved; it is recorded here as
fact so nobody redesigns it from the old notes.

| Gesture | From the main screen |
|---|---|
| Up | the creature's card (4 pages: profile, battle, moves, progress) |
| Down | the player card (badges + avatar, then medals) |
| Left | the gym ladder -- which is also where the LAN battle button lives |
| Right | the party |

The clock lost its gesture on purpose: the menu's SETTINGS row already opens it,
and the player card is reached far more often. The Pokedex lost its horizontal
gesture for the same reason -- it has a menu row, and a gesture is worth more
spent on a screen without one.

**Swipe left is spoken for.** The old suggestion of wild encounters there is
superseded twice over: the gym ladder took it, and the multi-region plan (B2)
turns it into a region/LAN chooser. Wild encounters, if they happen, need
another home -- a menu row is the obvious one.

Every paged screen reached this way must be added to `swipe_test`; the same
paging bug shipped four times before that test existed.
