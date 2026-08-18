# -*- coding: utf-8 -*-
"""The move list, hand-authored. Source of truth for moves.h via gen_moves.py.

86 moves, trimmed from Gen 1's 165 and then topped up with the cheap early
attacks the early game needs: 63 attacking (three or four per type, a
cheap one and a strong one), 11 stat-stage moves and 2 heals. STRUGGLE at the
end makes 77 -- it is a fallback, not a learnable move.

Not a pure Gen 1 list, and it can't be. dex_types.py gives the 151 their
CURRENT typings, so the dex contains Fairy (Clefable, Mr. Mime), Steel
(Magneton) and Dark types -- and Fairy moves are Gen 6, Steel and Dark moves
are Gen 2. Restricting to Red/Blue's move list would leave exactly those
species with no same-type move to their name. Entries marked LATER below are
in for that reason: they buy STAB for a typing the game already committed to.
Ghost is the same story -- Gen 1 Ghost had no usable attacking move at all,
hence SHADOW BALL.

Display names are capped at 12 characters: four move buttons have to fit
across a 466 px round panel at text size 2. ANCIENTPOWER is the games' own
Gen 3 spelling; DAZZLE GLEAM is ours.

No PP by design. Status conditions (burn/para/sleep/poison) are deliberately
absent for now -- the effect enum has room and they land in a later phase.
"""

# Move categories. There is no separate special-attack IV: special moves run
# off ivAtk/trAtk against the species' bSpA, and special defence off
# ivDef/trDef against bSpD. See gen_moves.py and the note in dex_stats.py.
MC_PHYS, MC_SPEC, MC_STATUS = 0, 1, 2

# Status ailments. Battle-only by design: they live in the battle state and are
# cleared when it ends, never written to the pet or replayed by the RTC's
# offline catch-up, so a burn can never grind against the care sim.
#
# There is no dedicated status move for these (no THUNDER WAVE, no SLEEP
# POWDER), so they ride along as a SECONDARY chance on a damaging move -- which
# is why they need their own two fields rather than reusing `effect`, whose one
# slot is already spent on things like EF_RECOIL.
AIL_NONE = 0
AIL_PARA = 1        # speed cut, and some turns are lost outright
AIL_BURN = 2        # chip damage each turn, physical attack cut
AIL_POISON = 3      # chip damage each turn
AIL_SLEEP = 4       # loses its turn for a few turns (no move inflicts it yet)
AIL_FREEZE = 5      # loses its turn until it thaws
AIL_CONFUSE = 6     # may hit itself instead

# Effects. Anything not listed here is a plain damaging move.
EF_NONE = 0
EF_STAGE = 1        # raise/lower stat stages (statMask + stages + target)
EF_RECOIL = 2       # param = denominator: 3 -> user takes 1/3 of damage dealt
EF_DRAIN = 3        # param = percent of damage dealt healed back
EF_FIXED_LVL = 4    # damage = user's level, ignores every stat
EF_FIXED = 5        # param = flat damage
EF_PRIORITY = 6     # param = priority bracket, moves before slower brackets
EF_NEVER_MISS = 7   # skips the accuracy roll entirely
EF_MULTI = 8        # hits 2-5 times, power is per hit
EF_HEAL = 9         # param = percent of max VIT restored
EF_RECHARGE = 10    # user loses the following turn, exposed
EF_CHARGE = 11      # turn 1 charges; param 1 = invulnerable while charging

# Stat bitmask for EF_STAGE. One delta applies to every bit set, which is how
# DRAGON DANCE (ATK+SPE) and BULK UP (ATK+DEF) get to be one table row.
ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE = 1, 2, 4, 8, 16

TG_SELF, TG_FOE = 0, 1

# name (<=12 chars), pokeapi slug, type, category, power, accuracy,
# effect, param, statMask, stages, target
#
# accuracy 0 means the move cannot miss (only ever paired with EF_NEVER_MISS
# or a status move, which never rolls).
MOVES = [
    # --- NORMAL -----------------------------------------------------------
    # The cheap early attacks. Without these a young creature had NOTHING of its
    # own: about 15% of species reached level 15 with no attacking move at all,
    # because their real early moves were missing here and they were quietly
    # leaning on TMs to fill the gap. That only became visible once TMs were
    # gated -- a level 1 Squirtle had been opening with SURF instead.
    ("TACKLE",       "tackle",        'normal',   MC_PHYS,  40, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("SCRATCH",      "scratch",       'normal',   MC_PHYS,  40, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("POUND",        "pound",         'normal',   MC_PHYS,  40, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("FURY ATTACK",  "fury-attack",   'normal',   MC_PHYS,  15,  85, EF_MULTI,     0, 0, 0, TG_FOE),
    ("QUICK ATTACK", "quick-attack",  'normal',   MC_PHYS,  40, 100, EF_PRIORITY,  1, 0, 0, TG_FOE),
    ("SWIFT",        "swift",         'normal',   MC_SPEC,  60,   0, EF_NEVER_MISS, 0, 0, 0, TG_FOE),
    ("BODY SLAM",    "body-slam",     'normal',   MC_PHYS,  85, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("DOUBLE-EDGE",  "double-edge",   'normal',   MC_PHYS, 120, 100, EF_RECOIL,    3, 0, 0, TG_FOE),
    ("HYPER BEAM",   "hyper-beam",    'normal',   MC_SPEC, 150,  90, EF_RECHARGE,  0, 0, 0, TG_FOE),
    # --- FLYING (the early ones; WING ATTACK and up are further down) ------
    ("PECK",         "peck",          'flying',   MC_PHYS,  35, 100, EF_NONE,      0, 0, 0, TG_FOE),
    # --- FIRE -------------------------------------------------------------
    ("EMBER",        "ember",         'fire',     MC_SPEC,  40, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_BURN, 10),
    ("FIRE PUNCH",   "fire-punch",    'fire',     MC_PHYS,  75, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_BURN, 10),
    ("FLAMETHROWER", "flamethrower",  'fire',     MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_BURN, 10),
    ("FIRE BLAST",   "fire-blast",    'fire',     MC_SPEC, 110,  85, EF_NONE,      0, 0, 0, TG_FOE, AIL_BURN, 10),
    # --- WATER ------------------------------------------------------------
    ("BUBBLE",       "bubble",        'water',    MC_SPEC,  30, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("WATER GUN",    "water-gun",     'water',    MC_SPEC,  40, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("WATERFALL",    "waterfall",     'water',    MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("SURF",         "surf",          'water',    MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("HYDRO PUMP",   "hydro-pump",    'water',    MC_SPEC, 110,  80, EF_NONE,      0, 0, 0, TG_FOE),
    # --- ELECTRIC ---------------------------------------------------------
    ("SPARK",        "spark",         'electric', MC_PHYS,  35, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 20),
    ("THUNDERSHOCK", "thunder-shock", 'electric', MC_SPEC,  40, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 10),
    ("THUNDERPUNCH", "thunder-punch", 'electric', MC_PHYS,  75, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 10),
    ("THUNDERBOLT",  "thunderbolt",   'electric', MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 10),
    ("THUNDER",      "thunder",       'electric', MC_SPEC, 110,  70, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 30),
    # --- GRASS ------------------------------------------------------------
    ("ABSORB",       "absorb",        'grass',    MC_SPEC,  20, 100, EF_DRAIN,    50, 0, 0, TG_FOE),
    ("VINE WHIP",    "vine-whip",     'grass',    MC_PHYS,  45, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("RAZOR LEAF",   "razor-leaf",    'grass',    MC_PHYS,  55,  95, EF_NONE,      0, 0, 0, TG_FOE),
    ("MEGA DRAIN",   "mega-drain",    'grass',    MC_SPEC,  40, 100, EF_DRAIN,    50, 0, 0, TG_FOE),
    # charges in the open: the one 2-turn move that really does cost you a hit
    ("SOLAR BEAM",   "solar-beam",    'grass',    MC_SPEC, 120, 100, EF_CHARGE,    0, 0, 0, TG_FOE),
    # --- ICE --------------------------------------------------------------
    ("AURORA BEAM",  "aurora-beam",   'ice',      MC_SPEC,  65, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("ICE PUNCH",    "ice-punch",     'ice',      MC_PHYS,  75, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_FREEZE, 10),
    ("ICE BEAM",     "ice-beam",      'ice',      MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_FREEZE, 10),
    ("BLIZZARD",     "blizzard",      'ice',      MC_SPEC, 110,  70, EF_NONE,      0, 0, 0, TG_FOE, AIL_FREEZE, 10),
    # --- FIGHTING ---------------------------------------------------------
    ("KARATE CHOP",  "karate-chop",   'fighting', MC_PHYS,  50, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("SEISMIC TOSS", "seismic-toss",  'fighting', MC_PHYS,   0, 100, EF_FIXED_LVL, 0, 0, 0, TG_FOE),
    ("SUBMISSION",   "submission",    'fighting', MC_PHYS,  80,  80, EF_RECOIL,    4, 0, 0, TG_FOE),
    # 130 in the modern games; 100 here so one move can't decide a gym
    ("HI JUMP KICK", "high-jump-kick",'fighting', MC_PHYS, 100,  90, EF_NONE,      0, 0, 0, TG_FOE),
    # --- POISON -----------------------------------------------------------
    ("POISON STING", "poison-sting",  'poison',   MC_PHYS,  15, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_POISON, 30),
    ("ACID",         "acid",          'poison',   MC_SPEC,  40, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_POISON, 10),
    ("SLUDGE",       "sludge",        'poison',   MC_SPEC,  65, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_POISON, 30),
    ("SLUDGE BOMB",  "sludge-bomb",   'poison',   MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_POISON, 30),  # LATER (Gen 2)
    # --- GROUND -----------------------------------------------------------
    ("BONE CLUB",    "bone-club",     'ground',   MC_PHYS,  65,  85, EF_NONE,      0, 0, 0, TG_FOE),
    ("DIG",          "dig",           'ground',   MC_PHYS,  80, 100, EF_CHARGE,    1, 0, 0, TG_FOE),
    ("EARTHQUAKE",   "earthquake",    'ground',   MC_PHYS, 100, 100, EF_NONE,      0, 0, 0, TG_FOE),
    # --- FLYING -----------------------------------------------------------
    ("WING ATTACK",  "wing-attack",   'flying',   MC_PHYS,  60, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("DRILL PECK",   "drill-peck",    'flying',   MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("FLY",          "fly",           'flying',   MC_PHYS,  90,  95, EF_CHARGE,    1, 0, 0, TG_FOE),
    # --- PSYCHIC ----------------------------------------------------------
    ("PSYWAVE",      "psywave",       'psychic',  MC_SPEC,  30, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("CONFUSION",    "confusion",     'psychic',  MC_SPEC,  50, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_CONFUSE, 10),
    ("PSYBEAM",      "psybeam",       'psychic',  MC_SPEC,  65, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_CONFUSE, 10),
    ("PSYCHIC",      "psychic",       'psychic',  MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE),
    # --- BUG --------------------------------------------------------------
    ("BUG BITE",     "bug-bite",      'bug',      MC_PHYS,  30, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("PIN MISSILE",  "pin-missile",   'bug',      MC_PHYS,  25,  95, EF_MULTI,     0, 0, 0, TG_FOE),
    # 20 power in Gen 1, 80 since Gen 7 -- the modern value, like the stats
    ("LEECH LIFE",   "leech-life",    'bug',      MC_PHYS,  80, 100, EF_DRAIN,    50, 0, 0, TG_FOE),
    ("MEGAHORN",     "megahorn",      'bug',      MC_PHYS, 120,  85, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 2)
    # The three above reach almost no Gen 1 bug: BUTTERFREE and PINSIR learn
    # none of them, and would have gone to a gym with no same-type move at
    # all. These two are the standard modern Bug attacks and fix exactly that.
    ("BUG BUZZ",     "bug-buzz",      'bug',      MC_SPEC,  90, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 4)
    ("X-SCISSOR",    "x-scissor",     'bug',      MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 4)
    # --- ROCK -------------------------------------------------------------
    ("ROCK SMASH",   "rock-smash",    'fighting', MC_PHYS,  40, 100, EF_NONE,      0, 0, 0, TG_FOE),
    ("ROCK THROW",   "rock-throw",    'rock',     MC_PHYS,  50,  90, EF_NONE,      0, 0, 0, TG_FOE),
    ("ROCK SLIDE",   "rock-slide",    'rock',     MC_PHYS,  75,  90, EF_NONE,      0, 0, 0, TG_FOE),
    ("ANCIENTPOWER", "ancient-power", 'rock',     MC_SPEC,  60, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 2)
    # --- GHOST ------------------------------------------------------------
    ("LICK",         "lick",          'ghost',    MC_PHYS,  30, 100, EF_NONE,      0, 0, 0, TG_FOE, AIL_PARA, 30),
    ("NIGHT SHADE",  "night-shade",   'ghost',    MC_SPEC,   0, 100, EF_FIXED_LVL, 0, 0, 0, TG_FOE),
    ("SHADOW BALL",  "shadow-ball",   'ghost',    MC_SPEC,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 2)
    # --- DRAGON -----------------------------------------------------------
    ("DRAGON RAGE",  "dragon-rage",   'dragon',   MC_SPEC,   0, 100, EF_FIXED,    40, 0, 0, TG_FOE),
    ("DRAGON CLAW",  "dragon-claw",   'dragon',   MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 3)
    ("OUTRAGE",      "outrage",       'dragon',   MC_PHYS, 120, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 2)
    # --- DARK -------------------------------------------------------------
    # BITE existed in Gen 1 but as a Normal move; Dark from Gen 2 on
    ("BITE",         "bite",          'dark',     MC_PHYS,  60, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER
    ("CRUNCH",       "crunch",        'dark',     MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 2)
    # --- STEEL ------------------------------------------------------------
    ("IRON HEAD",    "iron-head",     'steel',    MC_PHYS,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 4)
    ("FLASH CANNON", "flash-cannon",  'steel',    MC_SPEC,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 4)
    # --- FAIRY ------------------------------------------------------------
    ("DAZZLE GLEAM", "dazzling-gleam",'fairy',    MC_SPEC,  80, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 6)
    ("PLAY ROUGH",   "play-rough",    'fairy',    MC_PHYS,  90,  90, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 6)
    ("MOONBLAST",    "moonblast",     'fairy',    MC_SPEC,  95, 100, EF_NONE,      0, 0, 0, TG_FOE),  # LATER (Gen 6)

    # --- stat stages, self ------------------------------------------------
    # The player's answer to a gym leader with perfect IVs: you are behind on
    # raw stats by design, so you win by setting up or by type advantage.
    # Stages reset on switch out, which is what makes switching a decision.
    ("SWORDS DANCE", "swords-dance",  'normal',   MC_STATUS, 0,   0, EF_STAGE,     0, ST_ATK,          2, TG_SELF),
    ("AGILITY",      "agility",       'psychic',  MC_STATUS, 0,   0, EF_STAGE,     0, ST_SPE,          2, TG_SELF),
    ("BARRIER",      "barrier",       'psychic',  MC_STATUS, 0,   0, EF_STAGE,     0, ST_DEF,          2, TG_SELF),
    ("AMNESIA",      "amnesia",       'psychic',  MC_STATUS, 0,   0, EF_STAGE,     0, ST_SPD,          2, TG_SELF),
    ("NASTY PLOT",   "nasty-plot",    'dark',     MC_STATUS, 0,   0, EF_STAGE,     0, ST_SPA,          2, TG_SELF),  # LATER (Gen 4)
    ("DRAGON DANCE", "dragon-dance",  'dragon',   MC_STATUS, 0,   0, EF_STAGE,     0, ST_ATK | ST_SPE, 1, TG_SELF),  # LATER (Gen 2)
    ("BULK UP",      "bulk-up",       'fighting', MC_STATUS, 0,   0, EF_STAGE,     0, ST_ATK | ST_DEF, 1, TG_SELF),  # LATER (Gen 3)
    # --- stat stages, foe -------------------------------------------------
    ("GROWL",        "growl",         'normal',   MC_STATUS, 0,   0, EF_STAGE,     0, ST_ATK,         -1, TG_FOE),
    ("LEER",         "leer",          'normal',   MC_STATUS, 0,   0, EF_STAGE,     0, ST_DEF,         -1, TG_FOE),
    ("SCREECH",      "screech",       'normal',   MC_STATUS, 0,   0, EF_STAGE,     0, ST_DEF,         -2, TG_FOE),
    ("STRING SHOT",  "string-shot",   'bug',      MC_STATUS, 0,   0, EF_STAGE,     0, ST_SPE,         -1, TG_FOE),
    # --- healing ----------------------------------------------------------
    ("RECOVER",      "recover",       'normal',   MC_STATUS, 0,   0, EF_HEAL,     50, 0, 0, TG_SELF),
    ("SOFT-BOILED",  "soft-boiled",   'normal',   MC_STATUS, 0,   0, EF_HEAL,     50, 0, 0, TG_SELF),

    # --- fallback ---------------------------------------------------------
    # Not in any learnset (slug None keeps the fetcher off it): the engine
    # substitutes this when a species has nothing else. METAPOD, KAKUNA and
    # DITTO learn none of the 70 above and would otherwise be unable to act.
    # For the first two that is a few hours before they evolve; DITTO is a
    # final form and stays this way, since its entire movepool is TRANSFORM.
    ("STRUGGLE",     None,            'normal',   MC_PHYS,  50,   0, EF_RECOIL,    4, 0, 0, TG_FOE),
]

# Accuracy drops (SAND ATTACK, SMOKESCREEN, FLASH) are deliberately left out:
# accuracy stages are a whole second stage system for one marginal effect.

SLUG_TO_NAME = {slug: name for name, slug, *_ in MOVES if slug}
