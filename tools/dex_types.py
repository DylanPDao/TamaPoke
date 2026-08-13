# -*- coding: utf-8 -*-
"""Typings and the type effectiveness chart, CURRENT (Gen 6+) rules.

Deliberately not the Gen 1 chart, even though the rest of the game is Gen 1
flavoured. Three reasons: the Gen 1 chart shipped real bugs (Ghost moves did
nothing at all to Psychic), Psychic was resisted only by Psychic and so ran
away with every battle, and -- decisively -- the base stats in dex_stats.py
were already pulled from PokeAPI at their CURRENT values, not their Gen 1 ones
(Pidgeot has 101 Speed here, not the 91 it had in Red/Blue). Modern stats with
an ancient chart is the inconsistent combination; this makes the data cohere.

Fairy also earns its place: Dragonite has the highest Attack in the dex, and
under pre-Gen-6 rules Dragon is resisted only by Steel. Dragon -> Fairy is 0x,
which hands the player a real answer to the strongest thing they can hatch.

Consumed by gen_dex.py, which emits dex.h.
"""

# Order matters: it is the index order of the effectiveness matrix and of the
# PkType enum in dex.h.
TYPE_ORDER = [
    'normal', 'fire', 'water', 'electric', 'grass', 'ice', 'fighting',
    'poison', 'ground', 'flying', 'psychic', 'bug', 'rock', 'ghost',
    'dragon', 'dark', 'steel', 'fairy',
]

# Attacker -> {defender: multiplier}. Anything unlisted is 1x.
# Current (Gen 6+) chart. Note Steel lost its Dark and Ghost resistances in
# Gen 6, so those cells are neutral here and not 0.5x.
CHART = {
    'normal':   {'rock': .5, 'ghost': 0, 'steel': .5},
    'fire':     {'fire': .5, 'water': .5, 'grass': 2, 'ice': 2, 'bug': 2,
                 'rock': .5, 'dragon': .5, 'steel': 2},
    'water':    {'fire': 2, 'water': .5, 'grass': .5, 'ground': 2, 'rock': 2,
                 'dragon': .5},
    'electric': {'water': 2, 'electric': .5, 'grass': .5, 'ground': 0,
                 'flying': 2, 'dragon': .5},
    'grass':    {'fire': .5, 'water': 2, 'grass': .5, 'poison': .5, 'ground': 2,
                 'flying': .5, 'bug': .5, 'rock': 2, 'dragon': .5, 'steel': .5},
    'ice':      {'fire': .5, 'water': .5, 'grass': 2, 'ice': .5, 'ground': 2,
                 'flying': 2, 'dragon': 2, 'steel': .5},
    'fighting': {'normal': 2, 'ice': 2, 'poison': .5, 'flying': .5,
                 'psychic': .5, 'bug': .5, 'rock': 2, 'ghost': 0, 'dark': 2,
                 'steel': 2, 'fairy': .5},
    'poison':   {'grass': 2, 'poison': .5, 'ground': .5, 'rock': .5,
                 'ghost': .5, 'steel': 0, 'fairy': 2},
    'ground':   {'fire': 2, 'electric': 2, 'grass': .5, 'poison': 2,
                 'flying': 0, 'bug': .5, 'rock': 2, 'steel': 2},
    'flying':   {'electric': .5, 'grass': 2, 'fighting': 2, 'bug': 2,
                 'rock': .5, 'steel': .5},
    'psychic':  {'fighting': 2, 'poison': 2, 'psychic': .5, 'dark': 0,
                 'steel': .5},
    'bug':      {'fire': .5, 'grass': 2, 'fighting': .5, 'poison': .5,
                 'flying': .5, 'psychic': 2, 'ghost': .5, 'dark': 2,
                 'steel': .5, 'fairy': .5},
    'rock':     {'fire': 2, 'ice': 2, 'fighting': .5, 'ground': .5,
                 'flying': 2, 'bug': 2, 'steel': .5},
    'ghost':    {'normal': 0, 'psychic': 2, 'ghost': 2, 'dark': .5},
    'dragon':   {'dragon': 2, 'steel': .5, 'fairy': 0},
    'dark':     {'fighting': .5, 'psychic': 2, 'ghost': 2, 'dark': .5,
                 'fairy': .5},
    'steel':    {'fire': .5, 'water': .5, 'electric': .5, 'ice': 2, 'rock': 2,
                 'steel': .5, 'fairy': 2},
    'fairy':    {'fire': .5, 'fighting': 2, 'poison': .5, 'dragon': 2,
                 'dark': 2, 'steel': .5},
}

# Typing of the 151 under current (Gen 6+) rules. Seven differ from Gen 1:
# Magnemite/Magneton gained Steel in Gen 2, and Clefairy, Clefable, Jigglypuff,
# Wigglytuff and Mr. Mime gained Fairy in Gen 6 (the first two becoming PURE
# Fairy, losing Normal entirely).
TYPES = {
    1: ('grass', 'poison'), 2: ('grass', 'poison'), 3: ('grass', 'poison'),
    4: ('fire', None), 5: ('fire', None), 6: ('fire', 'flying'),
    7: ('water', None), 8: ('water', None), 9: ('water', None),
    10: ('bug', None), 11: ('bug', None), 12: ('bug', 'flying'),
    13: ('bug', 'poison'), 14: ('bug', 'poison'), 15: ('bug', 'poison'),
    16: ('normal', 'flying'), 17: ('normal', 'flying'), 18: ('normal', 'flying'),
    19: ('normal', None), 20: ('normal', None),
    21: ('normal', 'flying'), 22: ('normal', 'flying'),
    23: ('poison', None), 24: ('poison', None),
    25: ('electric', None), 26: ('electric', None),
    27: ('ground', None), 28: ('ground', None),
    29: ('poison', None), 30: ('poison', None), 31: ('poison', 'ground'),
    32: ('poison', None), 33: ('poison', None), 34: ('poison', 'ground'),
    35: ('fairy', None), 36: ('fairy', None),  # pure Fairy from Gen 6
    37: ('fire', None), 38: ('fire', None),
    39: ('normal', 'fairy'), 40: ('normal', 'fairy'),
    41: ('poison', 'flying'), 42: ('poison', 'flying'),
    43: ('grass', 'poison'), 44: ('grass', 'poison'), 45: ('grass', 'poison'),
    46: ('bug', 'grass'), 47: ('bug', 'grass'),
    48: ('bug', 'poison'), 49: ('bug', 'poison'),
    50: ('ground', None), 51: ('ground', None),
    52: ('normal', None), 53: ('normal', None),
    54: ('water', None), 55: ('water', None),
    56: ('fighting', None), 57: ('fighting', None),
    58: ('fire', None), 59: ('fire', None),
    60: ('water', None), 61: ('water', None), 62: ('water', 'fighting'),
    63: ('psychic', None), 64: ('psychic', None), 65: ('psychic', None),
    66: ('fighting', None), 67: ('fighting', None), 68: ('fighting', None),
    69: ('grass', 'poison'), 70: ('grass', 'poison'), 71: ('grass', 'poison'),
    72: ('water', 'poison'), 73: ('water', 'poison'),
    74: ('rock', 'ground'), 75: ('rock', 'ground'), 76: ('rock', 'ground'),
    77: ('fire', None), 78: ('fire', None),
    79: ('water', 'psychic'), 80: ('water', 'psychic'),
    81: ('electric', 'steel'), 82: ('electric', 'steel'),  # Steel from Gen 2
    83: ('normal', 'flying'), 84: ('normal', 'flying'), 85: ('normal', 'flying'),
    86: ('water', None), 87: ('water', 'ice'),
    88: ('poison', None), 89: ('poison', None),
    90: ('water', None), 91: ('water', 'ice'),
    92: ('ghost', 'poison'), 93: ('ghost', 'poison'), 94: ('ghost', 'poison'),
    95: ('rock', 'ground'),
    96: ('psychic', None), 97: ('psychic', None),
    98: ('water', None), 99: ('water', None),
    100: ('electric', None), 101: ('electric', None),
    102: ('grass', 'psychic'), 103: ('grass', 'psychic'),
    104: ('ground', None), 105: ('ground', None),
    106: ('fighting', None), 107: ('fighting', None),
    108: ('normal', None),
    109: ('poison', None), 110: ('poison', None),
    111: ('ground', 'rock'), 112: ('ground', 'rock'),
    113: ('normal', None), 114: ('grass', None), 115: ('normal', None),
    116: ('water', None), 117: ('water', None),
    118: ('water', None), 119: ('water', None),
    120: ('water', None), 121: ('water', 'psychic'),
    122: ('psychic', 'fairy'),
    123: ('bug', 'flying'), 124: ('ice', 'psychic'),
    125: ('electric', None), 126: ('fire', None), 127: ('bug', None),
    128: ('normal', None), 129: ('water', None), 130: ('water', 'flying'),
    131: ('water', 'ice'), 132: ('normal', None), 133: ('normal', None),
    134: ('water', None), 135: ('electric', None), 136: ('fire', None),
    137: ('normal', None),
    138: ('rock', 'water'), 139: ('rock', 'water'),
    140: ('rock', 'water'), 141: ('rock', 'water'),
    142: ('rock', 'flying'), 143: ('normal', None),
    144: ('ice', 'flying'), 145: ('electric', 'flying'), 146: ('fire', 'flying'),
    147: ('dragon', None), 148: ('dragon', None), 149: ('dragon', 'flying'),
    150: ('psychic', None), 151: ('psychic', None),
}
