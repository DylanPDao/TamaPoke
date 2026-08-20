#include "pet.h"
#include "avatars.h"
#include "dex.h"
#include "moves.h"
#include "audio.h"

void Pet::begin() {
  prefs.begin("tamapoke", false);
  // Zeroed BEFORE the branch below, not inside load(): getBytes() leaves its
  // destination untouched when the key is missing, and the fresh-install path
  // returns without ever calling load(). Without this a begin() after a factory
  // reset keeps the old Pokedex alive in RAM -- the firmware reboots on WIPE so
  // it never showed there, but anything calling begin() twice would see it, and
  // Party::begin() already guards the same way for the same reason.
  memset(badgesX, 0, sizeof(badgesX));
  memset(badgesHardX, 0, sizeof(badgesHardX));
  memset(dexReg, 0, sizeof(dexReg));
  memset(dexShinyReg, 0, sizeof(dexShinyReg));
  for (int i = 0; i < REGION_COUNT; i++) eggByRegion[i] = 0;
  if (!prefs.getBool("init", false)) {
    prefs.putBool("init", true);
    newEgg();
  } else {
    load();
  }
  lastTick = millis();
}

void Pet::newEgg() {
  ceremony = CER_NONE;
  neglectTicks = 0;
  weight = 0;
  speciesId = -1;
  prevSpeciesId = -1;
  for (int i = 0; i < REGION_COUNT; i++) eggByRegion[i] = 0;
  eggTarget = pickEggSpecies();  // especie oculta segun rareza y pokedex
  eggByRegion[region % REGION_COUNT] = eggTarget;
  starterPick = (registeredCount() == 0);  // primera partida: el jugador elige inicial
  // sorteo shiny: 1/48 base, mejor con despedida y con racha/vinculo altos
  int shinyBase = (lastEnd == CER_FAREWELL ? 24 : 48) - careBonus();
  if (shinyBase < 8) shinyBase = 8;
  eggShiny = (random(shinyBase) == 0);
  // The debt lands on the creature about to hatch, and is spent doing so --
  // it is a one-day penalty, not a running total that compounds each retire.
  evoPen = retirePending ? EVO_PENALTY_LEVELS : 0;
  retirePending = false;
  eggTaps = 0;
  fullness = 80;
  joy = 80;
  energy = 80;
  hygiene = 100;
  poops = 0;
  ageMinutes = 0;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  frozen = false;
  save();
}

// progresion offline: el tiempo paso aunque estuviera apagado, pero con
// piedad — las barras bajan con suelo en 15 (vuelve hambriento, no muerto),
// sin descuidos ni escapadas en ausencia
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

void Pet::setClock(uint32_t nowEpoch) {
  lastSeenEpoch = nowEpoch;
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
  lastSeenEpoch = nowEpoch;
  if (nowEpoch == 0) return;
  uint32_t mins = (seen && nowEpoch > seen) ? (nowEpoch - seen) / 60 : 0;
  if (mins < 2 || ceremony != CER_NONE || starterPick) {
    save();  // primera vez, sin tiempo que aplicar o aun eligiendo inicial
    return;
  }
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // tope: 2 semanas

  for (uint32_t i = 0; i < mins; i++) {
    ageMinutes++;
    if (isEgg()) {
      if (ageMinutes >= 3) hatch();  // eclosiona en tu ausencia
      continue;
    }
    if (sleeping) {  // descanso: baja lento y con suelo, igual que en vivo
      energy = clamp100(energy + 6);
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      continue;
    }
    fullness = dropTo(fullness, 2, 15);
    energy = dropTo(energy, 1, 15);
    hygiene = dropTo(hygiene, 1, 15);
    joy = dropTo(joy, 1, 15);
  }
  if (!isEgg()) {
    if (!sleeping) {  // durmiendo no ensucia
      uint8_t p = poops + mins / 240;
      poops = p > 3 ? 3 : p;
    }
    // la evolucion NO se aplica offline: queda lista y la dispara el usuario
    // tocando al bicho cuando vuelve (para que vea la transformacion)
  }
  Serial.printf("offline: %u min aplicados (nv.%u)\n", mins, level());
  save();
}

void Pet::update(uint32_t nowMs) {
  // fin de ceremonia: la criatura se va y queda un huevo nuevo
  if (ceremony != CER_NONE && millis() > ceremonyUntil) {
    snapshotForParty();  // hand it over BEFORE newEgg() erases everything
    newEgg();
    return;
  }
  while (nowMs - lastTick >= PET_TICK_MS) {
    lastTick += PET_TICK_MS;
    tick();
  }
}

void Pet::tick() {
  if (ceremony != CER_NONE) return;  // el tiempo se detiene en la despedida
  if (starterPick) return;  // la partida no empieza hasta elegir inicial: si el
                            // tiempo corriera aqui, el huevo eclosionaria solo a
                            // los 3 min con la especie sorteada y se perderia la
                            // eleccion del jugador
  if (!frozen) ageMinutes++;   // a revived companion does not age

  if (isEgg()) {
    if (ageMinutes >= 3) hatch();  // si no lo tocas, eclosiona solo a los 3 min
    return;
  }

  applyAutoSleep();   // put down at 21:00 still goes to bed at 22:00


  // el sueño es descanso: la energia se recupera y las necesidades bajan MUCHO
  // mas lento que despierto y con suelo (amanece pidiendo algo de mimo, no a
  // cero, sin descuidos ni escapadas). despierto: comida -2/min, hig/joy -1/min.
  // El peso aun se quema y el descanso cuenta para la DEF (ver defTick).
  if (sleeping) {
    energy = clamp100(energy + 6);
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    defTick(true);  // descansar tambien es bienestar: cuenta para la DEF
    checkMedals();  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  if (ageMinutes % MINUTES_PER_LEVEL == 0) sfxPlay(SFX_LEVEL);  // subio de nivel (despierto)

  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  defTick(false);  // la calma forja la defensa

  int dJoy = -1;
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);

  // Descuido: dejar una estadistica por los suelos cuenta como error de
  // cuidado (con enfriamiento para no contar el mismo descuido cada minuto)
  if (mistakeCooldown > 0) mistakeCooldown--;
  if (lowestStat() <= 10 && mistakeCooldown == 0) {
    careMistakes++;
    mistakeCooldown = 60;
    if (bond > 1) bond--;  // el descuido enfria el vinculo, pero sin arrasarlo:
                           // a -3 cada 30 min se perdia mucho mas de lo que se
                           // podia ganar en todo un dia y el vinculo se atascaba
  }

  checkMedals();  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick
  checkLearnGates();

  // abandono total: con TODO a cero durante una hora queda lista para escaparse;
  // NO se va sola, la dispara el usuario con el boton (final triste, lo presencia)
  if (fullness == 0 && joy == 0 && energy == 0 && hygiene == 0) {
    if (neglectTicks < RUNAWAY_TICKS) neglectTicks++;
  } else {
    neglectTicks = 0;  // un solo cuidado la salva
  }

  // ciclo completo (forma final + 7 dias): la despedida NO salta sola; queda
  // lista (canFarewellNow) y la dispara el usuario con el boton, para que la vea

  // autoguardado periodico: NO escribir a flash aqui (corre dentro del loop,
  // mientras se anima); solo marcar y dejar que el loop lo vuelque al atenuar
  if (++ticksSinceSave >= 5) pendingSave = true;
}

// Copies the creature into endedMon so it can be offered a party slot, since
// newEgg() is about to wipe every field. Only the two endings the player CHOSE
// qualify: a runaway ran off after an hour of total neglect, and letting it
// come back on the team would remove the cost from the one ending that has any.
// Brings a banked creature back as the live pet, frozen.
void Pet::reviveFrom(const PartyMon &m) {
  if (m.empty()) return;
  ceremony = CER_NONE;
  neglectTicks = 0;
  speciesId = m.dex;
  prevSpeciesId = -1;
  eggTaps = 0;
  starterPick = false;
  shiny = m.shiny != 0;
  ivAtk = m.ivAtk; ivDef = m.ivDef; ivSpe = m.ivSpe; ivHp = m.ivHp;
  trAtk = m.trAtk; trDef = m.trDef; trSpe = m.trSpe;
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = m.moves[i];
  // its banked level expressed as an age, so level() needs no special case
  ageMinutes = (uint32_t)(m.level ? m.level - 1 : 0) * MINUTES_PER_LEVEL;
  lastLearnLevel = level();     // do not replay every gate it already passed
  learnQCount = 0;
  medals = m.medals;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  bond = 0;
  bondToday = 0;
  berryKnown = false;
  weight = 0;
  fullness = joy = energy = 80;
  hygiene = 100;
  poops = 0;
  frozen = true;
  strncpy(nick, m.nick, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  registerSpecies(speciesId);
  save();
}

void Pet::snapshotForParty() {
  endedKind = CER_NONE;
  if (isEgg()) return;
  if (ceremony != CER_FAREWELL && ceremony != CER_RELEASE) return;
  endedMon = PartyMon();
  endedMon.dex = speciesId;
  endedMon.level = level();
  endedMon.medals = medals;
  endedMon.ivAtk = ivAtk;
  endedMon.ivDef = ivDef;
  endedMon.ivSpe = ivSpe;
  endedMon.ivHp = ivHp;
  endedMon.trAtk = trAtk;
  endedMon.trDef = trDef;
  endedMon.trSpe = trSpe;
  endedMon.shiny = shiny ? 1 : 0;
  for (int i = 0; i < MOVE_SLOTS; i++) endedMon.moves[i] = moves[i];  // frozen too
  strncpy(endedMon.nick, nick, sizeof(endedMon.nick) - 1);
  endedMon.nick[sizeof(endedMon.nick) - 1] = 0;
  endedKind = ceremony;
}

// vuelca el guardado periodico pendiente (lo llama el loop en un momento sin
// animacion para que el paron de la escritura a flash no se vea)
void Pet::flushSave() {
  if (pendingSave) save();
}

// La calma forja la defensa: cada hora de bienestar (descansando, o despierto
// con todo >= 40) da +1 de DEF, hasta el tope que permita el IV.
//
// Antes pedia 12 h SEGUIDAS y CUALQUIER desliz ponia el contador a cero, ademas
// de no contar el sueno. Simulando una vida entera (3 dias) eso daba 1 punto al
// jugador teoricamente perfecto (uno que actue cada minuto durante 72 h) y 0 a
// todos los demas, incluido uno que atienda cada 15 min: la comida cae 2/min,
// asi que quien no pase por el bicho cada media hora esta SIEMPRE por debajo de
// 40 y el contador no arrancaba nunca. La DEF era, en la practica, inentrenable.
// Ahora acumula en vez de resetear: un descuido cuesta los minutos malos, no
// todo el progreso.
void Pet::defTick(bool resting) {
  if (!resting && lowestStat() < 40) return;
  if (++goodTicks < DEF_TRAIN_TICKS) return;
  goodTicks = 0;
  if (trDef < trMaxDef()) trDef++;
}

// quedan miembros sin registrar en la linea evolutiva de esta base?
bool Pet::lineHasUnregistered(int16_t base) const {
  int16_t cur = base;
  for (int guard = 0; cur >= 1 && cur <= DEX_COUNT && guard < 6; guard++) {
    if (!isRegistered(cur)) return true;
    if (cur == DEX_EEVEE) {
      for (int16_t b = 134; b <= 136; b++)
        if (!isRegistered(b)) return true;
      return false;
    }
    cur = DEX_TBL[cur].evolvesTo;
  }
  return false;
}

uint8_t Pet::eggRarity() const {
  return (eggTarget >= 1 && eggTarget <= DEX_COUNT) ? DEX_TBL[eggTarget].rarity : R_COMUN;
}

// elige la especie del huevo: tirada de rareza (mejorada por una despedida
// completa, castigada por una escapada) y sesgo hacia lineas incompletas
// Room for the candidate list. A whole rarity tier of a 386-species dex is far
// more than the 80 the Kanto-only build needed.
#define CAND_MAX 260

int16_t Pet::pickEggSpecies() {
  const RegionInfo &rg = REGIONS[region % REGION_COUNT];
  // primera partida: inicial clasico -- del region elegida, so a Johto run
  // starts with a Johto starter rather than a Kanto one
  if (registeredCount() == 0) {
    return rg.starters[random(rg.starterCount)];
  }

  uint8_t tier = R_COMUN;
  if (lastEnd != CER_RUNAWAY) {
    bool blessed = (lastEnd == CER_FAREWELL);
    int rare = (blessed ? 45 : 27) + careBonus();
    int leg = (registeredCount() >= 25) ? (blessed ? 10 : 3) + careBonus() / 3 : 0;
    int r = random(100);
    if (r < leg) tier = R_LEGENDARIO;
    else if (r < leg + rare) tier = R_RARO;
  }

  // candidatos del tier con linea incompleta; si no hay, baja de tier;
  // si la pokedex del tier esta completa, vale cualquiera del tier
  for (int pass = 0; pass < 2; pass++) {
    for (int t = tier; t >= R_COMUN; t--) {
      int16_t cand[CAND_MAX];
      int n = 0;
      for (int16_t d = rg.lo; d <= rg.hi && n < CAND_MAX; d++) {
        if (DEX_TBL[d].rarity != t) continue;
        if (pass == 0 && !lineHasUnregistered(d)) continue;
        cand[n++] = d;
      }
      if (n > 0) return cand[random(n)];
    }
  }
  return rg.starters[random(rg.starterCount)];  // inalcanzable, por si acaso
}

// Rolls a species of a GIVEN tier inside a region. Used when the player changes
// region while an egg is waiting: the rarity they were granted is kept and only
// the region changes, so switching cannot be farmed for a legendary.
int16_t Pet::rollInRegion(uint8_t r, uint8_t tier) {
  const RegionInfo &rg = REGIONS[r % REGION_COUNT];
  for (int t = tier; t >= R_COMUN; t--) {
    int16_t cand[CAND_MAX];
    int n = 0;
    for (int16_t d = rg.lo; d <= rg.hi && n < CAND_MAX; d++)
      if (DEX_TBL[d].rarity == t) cand[n++] = d;
    if (n) return cand[random(n)];
  }
  return rg.starters[0];      // a region with nothing in it cannot happen
}

// Changing region swaps the WAITING egg to that region's creature.
//
// Without this the setting would look broken: you would pick Johto and still
// hatch a Rattata, because the species is rolled when the egg appears and not
// when it cracks. Two rules stop it becoming a re-roll button:
//
//   1. The rarity tier is kept. Only which species of that tier changes, so
//      toggling can never be farmed for a legendary.
//   2. Each region's answer is REMEMBERED for this egg. Switching back shows
//      the same creature again, so there is nothing to gain by flipping.
//
// A hatched creature is untouched -- this only ever moves an egg.
void Pet::setRegion(uint8_t r) {
  r %= REGION_COUNT;
  if (r == region) return;
  uint8_t old = region;
  region = r;
  if (isEgg() && eggTarget >= 1) {
    if (old < REGION_COUNT) eggByRegion[old] = eggTarget;
    int16_t known = eggByRegion[r];
    eggTarget = known >= 1 ? known : rollInRegion(r, eggRarity());
    eggByRegion[r] = eggTarget;
  }
  save();
}

void Pet::registerSpecies(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return;
  dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (shiny) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
}

// la racha y el vinculo mejoran el sorteo del huevo (0..~14)
int Pet::careBonus() const {
  int s = streak > 30 ? 30 : streak;
  return s / 3 + bond / 25;
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == lastCareDay) return;  // sin reloj, o ya conto hoy
  if (lastCareDay == 0 || d == lastCareDay + 1) {
    streak++;
  } else {
    streak = 1;        // hubo un hueco de dias
    lastMilestone = 0;
  }
  lastCareDay = d;
  bondToday = 0;
  if (streak > bestStreak) bestStreak = streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (streak >= 100) ? 100 : (streak >= 30) ? 30
              : (streak >= 7)   ? 7   : (streak >= 3)  ? 3 : 0;
  if (ms > lastMilestone) {
    lastMilestone = ms;
    milestoneUntil = millis() + 4500;
  }
  checkMedals();
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 20) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

void Pet::checkMedals() {
  if (isEgg()) return;
  uint16_t before = medals;
  if (level() >= 10) medals |= MED_LV10;
  if (level() >= 25) medals |= MED_LV25;
  if (level() >= 50) medals |= MED_LV50;
  if (berryKnown) medals |= MED_BERRY;
  if (streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (DEX_TBL[speciesId].evolvesTo == 0) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5 && careMistakes == 0) medals |= MED_FIT;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) totalMedals++;
    newMedal = gained;
    medalUntil = millis() + 4000;
    if (!sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
}

void Pet::rename(const char *name) {
  strncpy(nick, name, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  save();
}

// La aportacion del IV (IV x nivel / 100) es exactamente la de los juegos de
// 3a generacion en adelante: un IV perfecto vale +31 a nivel 100. El resto de
// la formula es la de TamaPoke (base plana + nivel) y no la de los juegos: con
// el x nivel/100 canonico sobre la base, un bicho recien nacido mostraria
// stats de un solo digito, que en una pantalla de mascota parece un error.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint8_t lvl, uint8_t tr) {
  return (uint16_t)base + lvl + (uint16_t)iv * lvl / 100 + tr;
}

uint16_t Pet::atkStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bAtk, ivAtk, level(), trAtk);
}
uint16_t Pet::defStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bDef, ivDef, level(), trDef);
}
uint16_t Pet::speStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpe, ivSpe, level(), trSpe);
}
// la vitalidad no se entrena (no hay nada que la suba), asi que lleva un +10
// fijo en lugar del entrenamiento, igual que el +Nivel+10 del HP en los juegos
uint16_t Pet::vitStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bHp, ivHp, level(), 10);
}
// Special reuses the physical IV and training against the species' special base
// stat, which is what keeps Alakazam (50 Atk / 135 SpA) a real attacker without
// adding IVs or migrating saves.
uint16_t Pet::spaStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpA, ivAtk, level(), trAtk);
}
uint16_t Pet::spdStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpD, ivDef, level(), trDef);
}

// ---------- moves ----------

uint8_t Pet::moveCount() const {
  uint8_t n = 0;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i]) n++;
  return n;
}

bool Pet::knowsMove(uint8_t mv) const {
  if (!mv) return false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i] == mv) return true;
  return false;
}

// Picks a sensible default set, best first.
//
// NOT "the newest four": 1907 of the 2281 learnset entries sit at level 0 and
// another 237 at level 1, so for most species level orders nothing and taking
// the last four in table order is arbitrary -- it handed a level 100 Charizard
// GROWL and LEER. So score instead: attacks over status, stronger over weaker,
// STAB ahead of equal power, and a bonus for the handful of moves that really
// are gated behind a level, since those are meant to be upgrades.
// TMs unlock at one level, for everything.
//
// This replaced a power/2 curve, which was impossible for a player to predict
// (SURF at 45, ROCK SLIDE at 37) and dribbled unlocks out one at a time so none
// of them felt like anything. A single number is explainable in one sentence and
// lands on a seam the game already has: the first five leaders sit at 14-43, so
// you fight the early ladder on what your species actually learns, and TMs
// arrive as you enter the back half. A creature retires at 73 and caps at 100.
//
// It only works because dex_moves.py now carries the cheap early attacks --
// SCRATCH, PECK, POISON STING, BUBBLE and the rest. Without those, gating TMs
// this hard would leave young creatures with nothing at all, which is exactly
// what the power/2 version was papering over.
#define TM_LEVEL 40

static uint8_t tmLevelFor(const MoveEntry &m) {
  (void)m;
  return TM_LEVEL;
}

// THE single answer, used by relearnFromLevel(), by the STAB fallback and by
// the move picker in the sketch. Three call sites once had three opinions.
uint8_t moveUnlockLevel(int16_t dex, uint8_t idx) {
  uint8_t at = learnLevel(dex, idx);
  if (at > 0) return at;                 // a real level-up move
  uint8_t mv = learnMove(dex, idx);
  if (!mv || mv >= MOVE_COUNT) return 255;
  return tmLevelFor(MOVE_TBL[mv]);       // a TM: no natural level, so the gate
}

void Pet::relearnFromLevel() {
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = 0;
  if (isEgg()) return;
  const DexEntry &d = DEX_TBL[speciesId];
  uint8_t lvl = level(), n = learnCount(speciesId);
  int16_t score[MOVE_SLOTS] = { 0, 0, 0, 0 };
  // Two passes. Level-up moves (level >= 1) are what a creature grows into, so
  // they fill the set first; TMs (level 0, no gate) only top up the slots left
  // over. Without this a just-hatched pet opens with FIRE BLAST and SOLAR BEAM,
  // because every TM is legal at level 1.
  for (int pass = 0; pass < 2; pass++) {
  bool tmPass = (pass == 1);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (at > lvl) continue;
    if (tmPass != (at == 0)) continue;
    uint8_t mv = learnMove(speciesId, i);
    if (!mv || mv >= MOVE_COUNT || knowsMove(mv)) continue;
    const MoveEntry &m = MOVE_TBL[mv];
    // A TM carries no level requirement in the data, which is true of the games
    // but wrong here: with only one or two level-up moves early on, the spare
    // slots were filled with the strongest TMs in the table and a NEWBORN opened
    // with SURF, BLIZZARD and OUTRAGE. That, not the damage formula, is why a
    // level 1 Squirtle could beat Brock.
    //
    // So a TM is gated by its own power: roughly power/2, which puts the 40s
    // around level 20 and the 110s out past 50 where a creature is genuinely
    // built. Level-up moves are untouched -- they already have real gates.
    if (tmPass && lvl < tmLevelFor(m)) continue;
    int16_t sc = (m.cat == MC_STATUS) ? 10 : (int16_t)m.power + 20;
    // STAB outweighs raw power, or every species defaults to the same two
    // generic sledgehammers and the roster loses its identity.
    if (m.cat != MC_STATUS && (m.type == d.type1 || m.type == d.type2)) sc += 40;
    if (m.effect == EF_RECHARGE) sc -= 35;   // a free turn for the opponent
    if (m.effect == EF_RECOIL) sc -= 20;
    sc += at;
    if (sc < 1) sc = 1;
    int slot = -1;
    for (int s = 0; s < MOVE_SLOTS; s++)
      if (sc > score[s]) { slot = s; break; }
    if (slot < 0) continue;
    for (int s = MOVE_SLOTS - 1; s > slot; s--) {
      score[s] = score[s - 1];
      moves[s] = moves[s - 1];
    }
    score[slot] = sc;
    moves[slot] = mv;
  }
  if (moveCount() >= MOVE_SLOTS) break;   // level-up moves already filled it
  }
  // Guarantee one same-type move. Machamp's only Fighting options are weak or
  // recoil-laden, so pure scoring left it with four generic attacks and nothing
  // that reads as a Machamp. If the set came out with no STAB, the weakest slot
  // gives way to the best same-type attack the species can actually learn.
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!moves[i]) continue;
    const MoveEntry &m = MOVE_TBL[moves[i]];
    if (m.cat != MC_STATUS && (m.type == d.type1 || m.type == d.type2)) return;
  }
  uint8_t best = 0;
  int16_t bestSc = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (at > lvl) continue;
    uint8_t mv = learnMove(speciesId, i);
    if (!mv || mv >= MOVE_COUNT) continue;
    const MoveEntry &m = MOVE_TBL[mv];
    if (m.cat == MC_STATUS || (m.type != d.type1 && m.type != d.type2)) continue;
    // The same TM gate as above. This fallback used to ignore it, which is how
    // a level 1 Squirtle ended up holding SURF: it had no Water move, so the
    // guarantee reached past every check and handed it the best one in the
    // table. A creature with no STAB it can legally use simply has none yet.
    if (at == 0 && lvl < tmLevelFor(m)) continue;
    int16_t sc = (int16_t)m.power;
    if (m.effect == EF_RECHARGE) sc -= 35;
    if (m.effect == EF_RECOIL) sc -= 20;
    if (sc > bestSc) { bestSc = sc; best = mv; }
  }
  if (best) moves[MOVE_SLOTS - 1] = best;
}

// Queues every level-up move unlocked since the last check. A free slot is
// filled silently -- the games do not ask when there is room either -- and only
// a full moveset produces an offer the player has to answer.
void Pet::checkLearnGates() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint8_t lvl = level();
  if (lvl <= lastLearnLevel) return;
  uint8_t n = learnCount(speciesId);
  for (uint8_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(speciesId, i);
    if (at == 0 || at <= lastLearnLevel || at > lvl) continue;  // 0 = TM, no gate
    uint8_t mv = learnMove(speciesId, i);
    if (!mv || mv >= MOVE_COUNT || knowsMove(mv)) continue;
    int freeSlot = -1;
    for (int s = 0; s < MOVE_SLOTS; s++)
      if (!moves[s]) { freeSlot = s; break; }
    if (freeSlot >= 0) { moves[freeSlot] = mv; continue; }
    if (learnQCount >= sizeof(learnQueue)) continue;
    bool dup = false;
    for (uint8_t q = 0; q < learnQCount; q++)
      if (learnQueue[q] == mv) dup = true;
    if (!dup) learnQueue[learnQCount++] = mv;
  }
  lastLearnLevel = lvl;
  pendingSave = true;
}

static void popLearn(uint8_t *q, uint8_t &n) {
  if (!n) return;
  for (uint8_t i = 0; i + 1 < n; i++) q[i] = q[i + 1];
  q[--n] = 0;
}

void Pet::acceptLearn(uint8_t slot) {
  if (!learnQCount || slot >= MOVE_SLOTS) return;
  moves[slot] = learnQueue[0];
  popLearn(learnQueue, learnQCount);
  save();
}

void Pet::declineLearn() {
  popLearn(learnQueue, learnQCount);
  save();
}

uint8_t Pet::pendingLearnables(uint8_t *out, uint8_t max) const {
  if (isEgg() || !out || !max) return 0;
  uint8_t lvl = level(), n = learnCount(speciesId), w = 0;
  for (uint8_t i = 0; i < n && w < max; i++) {
    if (learnLevel(speciesId, i) > lvl) break;
    uint8_t mv = learnMove(speciesId, i);
    if (knowsMove(mv)) continue;
    bool dup = false;                     // do not offer the same move twice
    for (uint8_t j = 0; j < w; j++)
      if (out[j] == mv) { dup = true; break; }
    if (!dup) out[w++] = mv;
  }
  return w;
}

// Tirada de un IV: 8-31. El suelo en 8 es deliberado — en los juegos un 0 es
// posible porque puedes criar cientos de huevos, aqui cada crianza dura 3 dias
// y un individuo de desecho seria un castigo desproporcionado. La racha y el
// vinculo del bicho ANTERIOR empujan la tirada: cuidar bien mejora la camada.
uint8_t Pet::rollIV(int bonus) const {
  int v = 8 + (int)random(24) + bonus / 2;  // bonus 0..14 -> +0..7
  return (uint8_t)(v > 31 ? 31 : v);
}

// Guardados con el sistema viejo de genes (90-110%): se convierten al rango de
// IV que se sortea hoy (8-31) para que nadie salga perdiendo con la
// actualizacion. gene 0 = mascota anterior incluso a los genes.
uint8_t Pet::ivFromGene(uint8_t gene) const {
  if (gene == 0) return rollIV(0);
  if (gene < 90) gene = 90;
  if (gene > 110) gene = 110;
  return 8 + (uint8_t)(((uint16_t)(gene - 90) * 23) / 20);
}

void Pet::rollIVs() {
  int bonus = careBonus();
  ivAtk = rollIV(bonus);
  ivDef = rollIV(bonus);
  ivSpe = rollIV(bonus);
  ivHp = rollIV(bonus);
  // los legendarios nacen con 3 de 4 IV perfectos, como en los juegos
  if (speciesId >= 1 && speciesId <= DEX_COUNT && DEX_TBL[speciesId].rarity == R_LEGENDARIO) {
    uint8_t *p[4] = { &ivAtk, &ivDef, &ivSpe, &ivHp };
    for (int k = 3; k > 0; k--) {  // baraja para elegir cuales 3
      int j = random(k + 1);
      uint8_t *t = p[k]; p[k] = p[j]; p[j] = t;
    }
    for (int k = 0; k < 3; k++) *p[k] = 31;
  }
  // en la 2a generacion el shiny ERA un patron de DV concreto: un shiny nunca
  // era mediocre. Aqui se traduce como un suelo de 20 en todos los IV.
  if (shiny) {
    if (ivAtk < 20) ivAtk = 20;
    if (ivDef < 20) ivDef = 20;
    if (ivSpe < 20) ivSpe = 20;
    if (ivHp < 20) ivHp = 20;
  }
}

uint16_t Pet::registeredCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= DEX_COUNT; i++)
    if (isRegistered(i)) n++;
  return n;
}

// forma final que ya cumplio su ciclo (7 dias): lista para despedirse. La
// despedida la dispara el usuario con el boton (no salta sola, para que la vea)
bool Pet::canFarewellNow() const {
  if (frozen) return false;     // a companion cannot be lost; that is the point
  return !isEgg() && !sleeping && ceremony == CER_NONE &&
         DEX_TBL[speciesId].evolvesTo == 0 && ageMinutes >= FAREWELL_AGE_MIN;
}

// abandono total durante 1h: lista para escaparse. La dispara el usuario con el
// boton (final triste); cuidarla un solo tick la salva (neglectTicks se resetea)
bool Pet::canRunawayNow() const {
  if (frozen) return false;
  return !isEgg() && !sleeping && ceremony == CER_NONE && neglectTicks >= RUNAWAY_TICKS;
}

bool Pet::canRetireNow() const {
  if (frozen) return false;     // a companion is never given up
  return !isEgg() && !sleeping && ceremony == CER_NONE && !starterPick;
}

// The ceremony is the same one; only the debt differs. Marked BEFORE the
// ceremony starts and spent by newEgg(), so a reset mid-ceremony loses the
// penalty rather than applying it to a creature that never got retired.
void Pet::startRetire() {
  if (!canRetireNow()) return;
  retirePending = !canFarewellNow();
  save();
  startFarewell();
}

void Pet::startFarewell() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_FAREWELL;
  ceremony = CER_FAREWELL;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;  // corazones durante toda la despedida
  sfxPlay(SFX_BYE);
  save();
}

void Pet::startRunaway() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RUNAWAY;
  ceremony = CER_RUNAWAY;
  ceremonyUntil = millis() + CEREMONY_MS;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::release() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::hatch() {
  speciesId = eggTarget;
  shiny = eggShiny;
  // IV del individuo (cada crianza es unica). Se tiran ANTES de resetear el
  // vinculo a proposito: el careBonus que los empuja es el del bicho anterior.
  rollIVs();
  trAtk = trDef = trSpe = 0;
  goodTicks = 0;
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  medals = 0;
  newMedal = 0;
  nick[0] = 0;
  registerSpecies(speciesId);  // criado = registrado en la pokedex
  // Start empty: checkLearnGates() fills the level-1 moves. Seeding from TMs
  // instead would hand a newborn FIRE BLAST, which no level 1 creature knows.
  for (int i = 0; i < MOVE_SLOTS; i++) moves[i] = 0;
  learnQCount = 0;
  lastLearnLevel = 0;
  checkLearnGates();
  checkMedals();     // por si nace ya en forma final (legendario)
  sfxPlay(SFX_HATCH);
  save();
}

// ¿se dan ya las condiciones para evolucionar? Cada descuido retrasa la
// evolucion 1 nivel, y ademas tiene que estar bien cuidado en ese momento
// (ninguna estadistica por debajo de 40). NO evoluciona sola: la dispara el
// usuario tocando al bicho (evolve()), para que vea la transformacion.
bool Pet::canEvolveNow() const {
  if (frozen) return false;     // frozen at the form it was banked in
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
  const DexEntry &d = DEX_TBL[speciesId];
  if (d.evolvesTo == 0) return false;
  // evoPen is the day owed for retiring the PREVIOUS creature early. It rides
  // on the same threshold careMistakes already moves, so there is one rule for
  // "this creature evolves later" rather than two that can disagree.
  return level() >= (uint16_t)(d.evolveLevel + careMistakes + evoPen) &&
         lowestStat() >= 40;
}

void Pet::evolve() {
  if (!canEvolveNow()) return;
  const DexEntry &d = DEX_TBL[speciesId];
  prevSpeciesId = speciesId;
  int16_t next = d.evolvesTo;
  if (speciesId == DEX_EEVEE) {
    // rama de Eevee: prefiere la evolucion que falte en la pokedex
    int16_t opts[3];
    int n = 0;
    for (int16_t b = 134; b <= 136; b++)
      if (!isRegistered(b)) opts[n++] = b;
    next = n > 0 ? opts[random(n)] : (int16_t)(134 + random(3));
  }
  speciesId = next;
  registerSpecies(speciesId);
  checkLearnGates();   // the new form may gate a move at this very level
  sfxPlay(SFX_EVOLVE);
  evolveUntil = millis() + EVOLVE_ANIM_MS;
  save();
}

void Pet::feed() {
  feedBerry(0);
}

void Pet::feedBerry(uint8_t color) {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  if (lovesBerry(color)) {
    fullness = clamp100(fullness + 35);
    joy = clamp100(joy + 10);
    heartUntil = millis() + HEART_MS;  // "le encanta!"
    berryKnown = true;                 // descubierto: se muestra en la ficha
    addBond(2);
  } else {
    fullness = clamp100(fullness + 25);
  }
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
}

void Pet::feedCandy() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  fullness = clamp100(fullness + 10);
  joy = clamp100(joy + 12);
  weight = clamp100(weight + 12);  // las chuches pasan factura
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  save();
}

uint8_t Pet::playResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  // The ball game is DEFENCE's trainer now. It used to train SPEED, which was
  // moved to its own reaction test to stop playing being a stat grind -- and
  // that left DEF with no active trainer at all, only the slow passive tick.
  // Keeping the ball on the defensive stat fits it: you are stopping something
  // from getting past you.
  uint8_t before = trDef;
  uint8_t gain = score / 2;
  if (gain > 18) gain = 18;          // the same per-session ceiling as the bag
  uint16_t v = (uint16_t)trDef + gain;
  trDef = v > trMaxDef() ? trMaxDef() : (uint8_t)v;
  gain = trDef - before;
  joy = clamp100(joy + 5 + (score > 15 ? 30 : score * 2));
  energy = dropTo(energy, 10 + score / 2, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - score * 2;  // el ejercicio quema peso
  weight = burn > 0 ? burn : 0;
  if (score >= 5) heartUntil = millis() + HEART_MS;
  if (score > gameHi) gameHi = score;  // nuevo record
  // Training bonds, and it scales with the session: a token effort is worth the
  // base, a full one is worth more. The daily cap in addBond() still stops it
  // being farmed -- this changes how fast a good session gets there, not the
  // ceiling.
  addBond((uint8_t)(2 + gain / 6));
  registerCare();
  save();
  return gain;
}

// saco de entrenamiento: los golpes entrenan la fuerza. Devuelve la subida.
uint8_t Pet::rewardTraining(uint8_t amount, uint8_t &which) {
  which = 0;
  if (ceremony != CER_NONE || isEgg() || !amount) return 0;
  // Only the stats with headroom are candidates.
  uint8_t room[3], n = 0;
  if (trAtk < trMaxAtk()) room[n++] = 0;
  if (trDef < trMaxDef()) room[n++] = 1;
  if (trSpe < trMaxSpe()) room[n++] = 2;
  if (!n) return 0;                     // nothing left to train
  which = room[random(n)];
  uint8_t before, capped;
  switch (which) {
    case 0: before = trAtk; capped = trMaxAtk(); trAtk = (uint8_t)min<uint16_t>(before + amount, capped); amount = trAtk - before; break;
    case 1: before = trDef; capped = trMaxDef(); trDef = (uint8_t)min<uint16_t>(before + amount, capped); amount = trDef - before; break;
    default: before = trSpe; capped = trMaxSpe(); trSpe = (uint8_t)min<uint16_t>(before + amount, capped); amount = trSpe - before; break;
  }
  // The IV-bound ceiling is never crossed: a mediocre individual not reaching
  // as far is the whole point of trMaxFor().
  save();
  return amount;
}

uint8_t Pet::trainSpeed(uint16_t hits) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = hits / 2;          // ~2 reactions = 1 point
  if (gain > 18) gain = 18;         // same per-session ceiling as the bag
  uint8_t before = trSpe;
  uint8_t v = trSpe + gain;
  trSpe = v > trMaxSpe() ? trMaxSpe() : v;   // el IV pone el techo
  gain = trSpe - before;
  energy = dropTo(energy, 10, 5);
  fullness = dropTo(fullness, 4, 5);
  int burn = (int)weight - hits / 2;
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 4);
  if (hits > spdHi) spdHi = hits;
  // Training bonds, and it scales with the session: a token effort is worth the
  // base, a full one is worth more. The daily cap in addBond() still stops it
  // being farmed -- this changes how fast a good session gets there, not the
  // ceiling.
  addBond((uint8_t)(2 + gain / 6));
  registerCare();
  save();
  return gain;
}

uint8_t Pet::trainStrength(uint16_t hits) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = hits / 4;          // ~4 golpes = 1 punto de entrenamiento
  if (gain > 18) gain = 18;         // tope por sesion: la FUE se forja a fuego lento
  uint8_t before = trAtk;
  uint8_t v = trAtk + gain;
  trAtk = v > trMaxAtk() ? trMaxAtk() : v;  // el IV pone el techo
  gain = trAtk - before;            // lo que de verdad subio (puede topar)
  energy = dropTo(energy, 12, 5);   // cansa
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - hits / 3;  // tambien quema peso
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 6);
  if (hits >= 20) heartUntil = millis() + HEART_MS;
  if (hits > strHi) strHi = hits;   // record de golpes
  // Training bonds, and it scales with the session: a token effort is worth the
  // base, a full one is worth more. The daily cap in addBond() still stops it
  // being farmed -- this changes how fast a good session gets there, not the
  // ceiling.
  addBond((uint8_t)(2 + gain / 6));
  registerCare();
  save();
  return gain;
}

void Pet::play() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 25);
  energy = clamp100(energy - 10);
  fullness = clamp100(fullness - 5);
  heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

// The hour off the RTC, the same source the scene uses. With no clock at all
// there is no night, so a board that has never been set never auto-sleeps.
bool Pet::isNightHour() const {
  if (!lastSeenEpoch) return false;
  int h = (int)((lastSeenEpoch / 3600) % 24);
  // The window may or may not cross midnight, and it must keep working either
  // way: with NIGHT_START 0 the old `h >= START || h < END` was true for every
  // hour of the day, which put the creature to sleep the moment the screen went
  // off at noon. sleep_test catches it, and did.
  if (NIGHT_START < NIGHT_END) return h >= NIGHT_START && h < NIGHT_END;
  return h >= NIGHT_START || h < NIGHT_END;
}

// Auto-sleep needs the screen off AND the night hours, and is re-checked every
// tick rather than only on the button, so a device put down at 21:00 nods off
// at 22:00 and gets up at 06:00 without anyone touching it.
//
// Both halves earn their place. Screen-off alone paused the game whenever you
// put the device down, and the creature is meant to get hungry during the day.
// The hour alone sent it to bed while you were still playing with it.
//
// Only what this put to sleep is woken by it: a creature the player sent to bed
// with the light button stays there until the player says otherwise.
void Pet::applyAutoSleep() {
  if (isEgg() || ceremony != CER_NONE) return;
  bool night = isNightHour();
  if (screenIsOff && night && !sleeping && sleepAuto != SLEEP_PLAYER) {
    sleeping = true;
    sleepAuto = SLEEP_AUTO;
    pendingSave = true;
  }
  // NOTHING wakes it here, and that is the whole point. Waking at 06:00 would
  // reopen the hole this exists to close: from the sleep floors, food is empty
  // by 06:15 and every stat by 07:40, so anyone who sleeps past eight would
  // find the creature ready to run away again. It sleeps until YOU are up,
  // which is the screen coming back on.
  if (!night && sleepAuto == SLEEP_PLAYER) sleepAuto = SLEEP_NONE;  // a new day
}

void Pet::setScreenOff(bool off) {
  screenIsOff = off;
  // Coming back to the device is what wakes it -- only if the device is what
  // put it to sleep. A creature sent to bed with the light stays there.
  if (!off && sleeping && sleepAuto == SLEEP_AUTO) {
    sleeping = false;
    sleepAuto = SLEEP_NONE;
  }
  applyAutoSleep();
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
  // The player's hand beats the clock until morning: waking it at 23:00 must
  // not be undone a minute later by the auto-sleep, and neither must putting
  // it to bed early.
  sleepAuto = SLEEP_PLAYER;
  save();
}

void Pet::clean() {
  if (ceremony != CER_NONE) return;
  poops = 0;
  hygiene = 100;
  addBond(1);
  registerCare();
  save();
}

void Pet::caress() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 5);
  heartUntil = millis() + HEART_MS;
  addBond(1);
  registerCare();
}

void Pet::eggTap() {
  if (!isEgg()) return;
  if (++eggTaps >= 3) hatch();
  else save();
}

PetMood Pet::mood() const {
  if (sleeping) return MOOD_SLEEPING;
  if (eating()) return MOOD_EATING;
  if (lowestStat() < 25) return MOOD_SAD;
  return MOOD_HAPPY;
}

void Pet::save() {
  ticksSinceSave = 0;
  pendingSave = false;
  prefs.putUChar("full", fullness);
  prefs.putUChar("joy", joy);
  prefs.putUChar("ene", energy);
  prefs.putUChar("hyg", hygiene);
  prefs.putUChar("poop", poops);
  prefs.putUChar("wgt", weight);
  prefs.putUChar("ivat", ivAtk);
  prefs.putUChar("ivdf", ivDef);
  prefs.putUChar("ivsp", ivSpe);
  prefs.putUChar("ivhp", ivHp);
  prefs.putUChar("tatk", trAtk);
  prefs.putUChar("tdef", trDef);
  prefs.putUChar("tspe", trSpe);
  prefs.putBytes("mvs", moves, sizeof(moves));
  prefs.putUChar("mvlv", lastLearnLevel);
  prefs.putUChar("avtr", avatar);
  prefs.putUChar("reg", region);
  prefs.putBytes("badgX", badgesX, sizeof(badgesX));
  prefs.putBytes("badhX", badgesHardX, sizeof(badgesHardX));
  prefs.putBytes("eggR", eggByRegion, sizeof(eggByRegion));
  prefs.putString("tnam", trainerName);
  prefs.putBool("froz", frozen);
  prefs.putUShort("badg", badges);
  prefs.putUShort("badh", badgesHard);
  prefs.putBool("bk", berryKnown);
  prefs.putBool("shy", shiny);
  prefs.putBool("eshy", eggShiny);
  prefs.putBool("stpk", starterPick);
  prefs.putUChar("evop", evoPen);
  prefs.putUChar("slpa", sleepAuto);
  prefs.putBool("rtpn", retirePending);
  prefs.putBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  prefs.putUInt("age", ageMinutes);
  prefs.putShort("dexn", speciesId);
  prefs.putShort("eggT2", eggTarget);
  prefs.putUChar("crack", eggTaps);
  prefs.putUChar("mist", careMistakes);
  prefs.putBool("sleep", sleeping);
  prefs.putUChar("lend", lastEnd);
  if (lastSeenEpoch) prefs.putUInt("seen", lastSeenEpoch);
  prefs.putBytes("dexreg", dexReg, sizeof(dexReg));
  prefs.putUShort("strk", streak);
  prefs.putUShort("bstrk", bestStreak);
  prefs.putUInt("cday", lastCareDay);
  prefs.putUChar("bond", bond);
  prefs.putUShort("medal", medals);
  prefs.putUShort("tmedal", totalMedals);
  prefs.putUShort("mstone", lastMilestone);
  prefs.putUShort("ghi", gameHi);
  prefs.putUShort("shi", strHi);
  prefs.putUShort("qhi", spdHi);
  prefs.putString("nick", nick);
}

void Pet::load() {
  fullness = prefs.getUChar("full", 80);
  joy = prefs.getUChar("joy", 80);
  energy = prefs.getUChar("ene", 80);
  hygiene = prefs.getUChar("hyg", 100);
  poops = prefs.getUChar("poop", 0);
  weight = prefs.getUChar("wgt", 0);
  if (prefs.isKey("ivat")) {
    ivAtk = prefs.getUChar("ivat", 16);
    ivDef = prefs.getUChar("ivdf", 16);
    ivSpe = prefs.getUChar("ivsp", 16);
    ivHp = prefs.getUChar("ivhp", 16);
  } else {
    // migracion desde los genes (90-110%) a IV (8-31) conservando la calidad
    // relativa: quien tenia un gen top mantiene un IV top. El IV de vitalidad
    // no existia, se tira ahora.
    ivAtk = ivFromGene(prefs.getUChar("gatk", 0));
    ivDef = ivFromGene(prefs.getUChar("gdef", 0));
    ivSpe = ivFromGene(prefs.getUChar("gspe", 0));
    ivHp = rollIV(0);
  }
  trAtk = prefs.getUChar("tatk", 0);
  trDef = prefs.getUChar("tdef", 0);
  trSpe = prefs.getUChar("tspe", 0);
  // un guardado antiguo puede traer entrenamiento por encima del nuevo tope
  if (trAtk > trMaxAtk()) trAtk = trMaxAtk();
  if (trDef > trMaxDef()) trDef = trMaxDef();
  if (trSpe > trMaxSpe()) trSpe = trMaxSpe();
  berryKnown = prefs.getBool("bk", false);
  shiny = prefs.getBool("shy", false);
  eggShiny = prefs.getBool("eshy", false);
  starterPick = prefs.getBool("stpk", false);
  evoPen = prefs.getUChar("evop", 0);
  sleepAuto = prefs.getUChar("slpa", SLEEP_NONE);
  retirePending = prefs.getBool("rtpn", false);
  prefs.getBytes("dexsh", dexShinyReg, sizeof(dexShinyReg));
  ageMinutes = prefs.getUInt("age", 0);
  if (prefs.isKey("dexn")) {
    speciesId = prefs.getShort("dexn", -1);
    eggTarget = prefs.getShort("eggT2", 4);
  } else {
    // migracion desde la version con indices de flash (0-8)
    static const uint8_t OLD2DEX[9] = { 4, 5, 6, 1, 2, 3, 7, 8, 9 };
    int8_t old = prefs.getChar("spec", -1);
    speciesId = (old >= 0 && old < 9) ? OLD2DEX[old] : -1;
    int8_t oldT = prefs.getChar("eggT", 0);
    eggTarget = (oldT >= 0 && oldT < 9) ? OLD2DEX[oldT] : 4;
  }
  eggTaps = prefs.getUChar("crack", 0);
  careMistakes = prefs.getUChar("mist", 0);
  sleeping = prefs.getBool("sleep", false);
  lastEnd = prefs.getUChar("lend", CER_NONE);
  prefs.getBytes("dexreg", dexReg, sizeof(dexReg));
  streak = prefs.getUShort("strk", 0);
  bestStreak = prefs.getUShort("bstrk", 0);
  lastCareDay = prefs.getUInt("cday", 0);
  bond = prefs.getUChar("bond", 0);
  medals = prefs.getUShort("medal", 0);
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  gameHi = prefs.getUShort("ghi", 0);
  strHi = prefs.getUShort("shi", 0);
  spdHi = prefs.getUShort("qhi", 0);
  prefs.getString("nick", nick, sizeof(nick));
  // Moves load last: relearnFromLevel() needs speciesId and ageMinutes, both of
  // which are read above. A save from before moves existed has no "mvs" key and
  // leaves the array zeroed, so an established pet is handed the moveset it
  // should already have rather than walking into a battle knowing nothing.
  prefs.getBytes("mvs", moves, sizeof(moves));
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (moves[i] >= MOVE_COUNT) moves[i] = 0;   // never index MOVE_TBL with junk
  lastLearnLevel = prefs.getUChar("mvlv", 0);
  frozen = prefs.getBool("froz", false);
  avatar = prefs.getUChar("avtr", 0);
  // Absent on a Kanto-only save, which leaves both arrays zeroed -- exactly
  // "no Johto or Hoenn badges yet".
  prefs.getBytes("badgX", badgesX, sizeof(badgesX));
  prefs.getBytes("badhX", badgesHardX, sizeof(badgesHardX));
  region = prefs.getUChar("reg", REGION_ALL);
  if (region >= REGION_COUNT) region = REGION_ALL;
  // A save from the Kanto-only build has neither key; getBytes leaves the
  // array at its zeroed initialiser, which is exactly "nothing remembered".
  prefs.getBytes("eggR", eggByRegion, sizeof(eggByRegion));
  prefs.getString("tnam", trainerName, sizeof(trainerName));
  if (avatar >= AVATAR_COUNT) avatar = 0;   // a save from when there were four
  badges = prefs.getUShort("badg", 0);
  badgesHard = prefs.getUShort("badh", 0);
  if (!isEgg() && moveCount() == 0 && lastLearnLevel == 0) {
    // save from before moves existed: hand it the set it should already have
    // rather than a queue of every gate it ever passed
    relearnFromLevel();
    lastLearnLevel = level();
  }
  learnQCount = 0;      // rebuilt from lastLearnLevel by the next tick
  checkLearnGates();
  // siembra: la mascota actual cuenta como criada (guardados antiguos)
  if (speciesId >= 1) registerSpecies(speciesId);
}
