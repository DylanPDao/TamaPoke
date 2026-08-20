#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "dex.h"
#include "trainers.h"   // GYM_REGIONS sizes the badge masks
#include "party.h"

// 1 tick = 1 minuto de juego. Baja este valor para probar mas rapido
// (p. ej. 5000UL = las estadisticas caen 12x mas rapido).
#define PET_TICK_MS 60000UL
// Minutos de juego por nivel. Con 60, CHARMANDER evoluciona a las ~16 h
// de juego con cuidado perfecto. Baja a 1 para ver evoluciones al momento.
#define MINUTES_PER_LEVEL 60
#define MAX_LEVEL 100              // reached at 4d 3h; see level()
#define EAT_ANIM_MS 2500UL
#define HEART_MS 1500UL
#define EVOLVE_ANIM_MS 5200UL              // animacion de evolucion (mas larga = mas epica)
#define CEREMONY_MS 10000UL                // duracion de la despedida en pantalla
#define FAREWELL_AGE_MIN (3UL * 24 * 60)   // se despide a los 3 dias de juego (en forma final)
// Retiring a creature BEFORE it has earned its farewell costs the NEXT one a
// day's worth of evolution. Derived from MINUTES_PER_LEVEL rather than written
// as 24, so it stays "a day" if the level rate is ever retuned.
#define EVO_PENALTY_LEVELS ((uint8_t)((24UL * 60) / MINUTES_PER_LEVEL))
#define RUNAWAY_TICKS 60                   // se escapa tras 1 h con TODO a cero
// Night, by the RTC: midnight to 06:00. Auto-sleep needs BOTH: the screen off
// AND this window.
// The screen alone would pause the game every time you put the device in a
// pocket -- the creature is supposed to get hungry during the day. The hour
// alone would send it to bed while you were playing. Set the clock in SETTINGS
// or over the console with RTCSET; an unset board reads months out and simply
// never auto-sleeps, which fails in the safe direction (it still drains, and
// the light button still works by hand).
#define NIGHT_START 0
#define NIGHT_END 6
enum : uint8_t { SLEEP_NONE = 0, SLEEP_AUTO, SLEEP_PLAYER };
#define DEF_TRAIN_TICKS 60                 // minutos de bienestar por +1 de DEF

// ceremonias de fin de ciclo
enum : uint8_t { CER_NONE = 0, CER_FAREWELL, CER_RUNAWAY, CER_RELEASE };

enum PetMood : uint8_t { MOOD_HAPPY, MOOD_SAD, MOOD_EATING, MOOD_SLEEPING };

// medallas del individuo (bitmask)
enum : uint16_t {
  MED_LV10 = 1 << 0, MED_LV25 = 1 << 1, MED_LV50 = 1 << 2,
  MED_BERRY = 1 << 3, MED_STREAK7 = 1 << 4, MED_BOND = 1 << 5,
  MED_FINAL = 1 << 6, MED_FIT = 1 << 7,
};
#define MED_COUNT 8

uint8_t moveUnlockLevel(int16_t dex, uint8_t idx);

class Pet {
public:
  // Estadisticas 0..100
  uint8_t fullness = 80;  // comida
  uint8_t joy = 80;       // felicidad
  uint8_t energy = 80;    // energia
  uint8_t hygiene = 100;  // limpieza
  uint8_t poops = 0;      // cacas en pantalla (max 3)
  uint8_t weight = 0;     // 0-100: las chuches engordan, el minijuego quema
  // IV (valores individuales 0-31, como en los juegos de 3a gen en adelante):
  // se tiran al eclosionar y no cambian nunca. Aportan IV x nivel / 100 al
  // stat Y ademas fijan el tope de entrenamiento (trMaxFor): un individuo
  // mediocre no solo empieza peor, es que no puede llegar tan lejos.
  uint8_t ivAtk = 16, ivDef = 16, ivSpe = 16, ivHp = 16;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  bool berryKnown = false;  // ya descubrio su baya favorita
  bool shiny = false;       // variante de color rara (se sortea en el huevo)
  uint32_t ageMinutes = 0;
  int16_t speciesId = -1;      // numero de Pokedex (1..DEX_COUNT), -1 = huevo
  int16_t prevSpeciesId = -1;  // para la animacion de evolucion
  uint8_t careMistakes = 0;   // descuidos: cada uno retrasa la evolucion 1 nivel
  bool sleeping = false;
  uint32_t lastSeenEpoch = 0;   // ultima hora RTC vista (para progresion offline)
  uint8_t ceremony = CER_NONE;  // despedida/escapada/liberacion en curso
  uint8_t lastEnd = CER_NONE;   // como acabo la anterior (afecta al huevo)
  // A finished ceremony hands the creature over here before newEgg() wipes the
  // live state. The UI drains it (endedKind back to CER_NONE) once the pet has
  // either taken a party slot or been let go. Only farewell and release fill
  // it; a runaway leaves endedKind at CER_NONE and the pet is simply gone.
  PartyMon endedMon;
  uint8_t endedKind = CER_NONE;
  // Pokedex bitmaps, one bit per species. Widening these is SAFE on an existing
  // save: getBytes() copies only what was stored, and the array is zeroed by its
  // initialiser, so a 19-byte blob from the Kanto-only build lands in the front
  // and bits 1-151 keep exactly their old meaning.
  uint8_t dexReg[(DEX_COUNT + 7) / 8] = { 0 };       // criados
  uint8_t dexShinyReg[(DEX_COUNT + 7) / 8] = { 0 };  // criados en version shiny
  // racha de cuidado diario (del jugador: persiste entre crianzas)
  uint16_t streak = 0, bestStreak = 0;
  uint32_t lastCareDay = 0;
  // vinculo (del bicho: sube lento con cuidado, se resetea al nacer otro)
  uint8_t bond = 0;
  char nick[12] = "";    // apodo (vacio = nombre de especie)
  // medallas: del individuo + contador acumulado entre todas las crianzas
  uint16_t medals = 0, totalMedals = 0;
  uint16_t newMedal = 0;   // recien conseguida(s), para celebrar
  uint16_t lastMilestone = 0;  // hito de racha ya celebrado
  uint16_t gameHi = 0;     // record del minijuego (del jugador)
  uint16_t strHi = 0;      // record de golpes al saco

  void begin();                 // carga estado de NVS (o crea el primer huevo)
  void update(uint32_t nowMs);  // llamar en cada loop()

  // Acciones (botones tactiles)
  void feed();              // baya roja (compatibilidad)
  void feedBerry(uint8_t color);  // 0 roja, 1 azul, 2 verde
  void feedCandy();
  bool lovesBerry(uint8_t color) const {
    return !isEgg() && (speciesId % 3) == color;  // gusto oculto por especie
  }
  // The ball game: happiness AND defence training. Returns the DEF gained.
  uint8_t playResult(uint8_t score);
  uint8_t trainStrength(uint16_t hits);  // saco de entrenamiento (entrena FUE)
  // Reaction test: its own trainer, so the ball game can go back to being purely
  // about joy instead of doubling as a stat grind.
  uint8_t trainSpeed(uint16_t hits);
  // What beating a gym leader is worth. Random WHICH stat, but only among the
  // ones with room left -- a random grant that landed on an already-capped stat
  // would silently evaporate, which reads as a bug rather than as luck. Writes
  // the stat into `which` (0 ATK, 1 DEF, 2 SPE) and returns what was actually
  // gained; 0 means every stat is at its ceiling. Costs nothing: the fight
  // already spent the energy, and that is what rate-limits rematching.
  uint8_t rewardTraining(uint8_t amount, uint8_t &which);
  uint16_t spdHi = 0;    // best reaction-test score

  // stats de combate: base real de gen 1 + nivel + IV + entrenamiento
  uint16_t atkStat() const;
  uint16_t defStat() const;
  uint16_t speStat() const;
  uint16_t vitStat() const;  // vitalidad (bHp): no se entrena, solo IV y nivel
  // The physical/special split lives on the species (bSpA/bSpD), not the
  // individual: special attack reuses ivAtk/trAtk and special defence reuses
  // ivDef/trDef, so no extra IVs and no save migration. See fetch_pokeapi.py.
  uint16_t spaStat() const;
  uint16_t spdStat() const;

  // The four known moves (indices into MOVE_TBL; 0 = empty slot). Player-chosen
  // once the learn/forget prompt exists -- until then, and for saves made before
  // moves were stored at all, relearnFromLevel() fills them in.
  uint8_t moves[MOVE_SLOTS] = { 0, 0, 0, 0 };
  uint8_t moveCount() const;
  bool knowsMove(uint8_t mv) const;
  // The newest MOVE_SLOTS moves this species has learned by its current level,
  // newest last. Used on hatch, on a fresh save, and to backfill empty slots.
  void relearnFromLevel();
  // The level at which `dex` may legally use learnset entry `i`. A level-up
  // move carries its own; a level-0 entry is a TM/tutor/egg move, which the
  // data gives no level at all, and those unlock together at TM_LEVEL.
  //
  // It is a free function, not a Pet method, because the move PICKER needs the
  // identical answer for a banked party member. Having its own gate is what let
  // a level 22 Charmeleon be offered FIRE BLAST.

  // Moves reachable at this level that are not already known, for the learn
  // prompt. Returns how many were written into out (at most max).
  uint8_t pendingLearnables(uint8_t *out, uint8_t max) const;

  // Player-wide, like the streak and the Pokedex: badges outlive the creature
  // that earned them, so newEgg() must never clear this.
  // A creature brought back out of the party or box. It is FROZEN: it does not
  // age, cannot evolve, and cannot be lost -- a companion rather than a
  // contender. The cost is that its level never rises again, so it stops
  // improving; ageMinutes is simply set to match its banked level, which keeps
  // level() working untouched rather than needing a second source of truth.
  bool frozen = false;
  void reviveFrom(const PartyMon &m);

  // The player's own name, alongside the badges and the streak: it belongs to
  // whoever is playing, not to the creature, so newEgg() must never clear it.
  char trainerName[12] = "";
  void renameTrainer(const char *n) {
    strncpy(trainerName, n, sizeof(trainerName) - 1);
    trainerName[sizeof(trainerName) - 1] = 0;
    save();
  }

  // Which generation eggs come from. Player-wide, like the badges: it outlives
  // every creature, so newEgg() must never reset it.
  uint8_t region = REGION_ALL;
  // The species this egg would be in each region. Filled in as the player
  // looks, cleared by newEgg(). It exists so that switching region and back
  // shows the SAME creature rather than rolling a fresh one -- without it,
  // toggling would be a re-roll button.
  int16_t eggByRegion[REGION_COUNT] = { 0 };
  void setRegion(uint8_t r);
  const char *regionName() const { return REGIONS[region % REGION_COUNT].name; }
  // How much of one region's dex is filled in, for the Pokedex header.
  uint16_t registeredCountIn(uint16_t lo, uint16_t hi) const {
    uint16_t n = 0;
    for (uint16_t d = lo; d <= hi && d <= DEX_COUNT; d++) if (isRegistered(d)) n++;
    return n;
  }

  uint8_t avatar = 0;       // which player sprite, 0..3
  // Kanto's ladder, under the keys it has always used. Johto and Hoenn live in
  // a SEPARATE array under new keys rather than widening these -- purely
  // additive, so an existing save cannot be misread, exactly the reasoning that
  // put the box under its own key instead of growing the party blob.
  uint16_t badges = 0;      // bit n = trainer n beaten on easy
  uint16_t badgesHard = 0;  // ... and on hard
  uint16_t badgesX[GYM_REGIONS - 1] = { 0 };
  uint16_t badgesHardX[GYM_REGIONS - 1] = { 0 };

  uint16_t badgeMask(uint8_t rg, bool hard) const {
    if (rg == 0) return hard ? badgesHard : badges;
    if (rg >= GYM_REGIONS) return 0;
    return hard ? badgesHardX[rg - 1] : badgesX[rg - 1];
  }
  bool hasBadge(uint8_t rg, uint8_t i, bool hard) const {
    return (badgeMask(rg, hard) >> i) & 1;
  }
  void winBadge(uint8_t rg, uint8_t i, bool hard) {
    if (rg >= GYM_REGIONS) return;
    uint16_t bit = (uint16_t)1 << i;
    if (rg == 0) { if (hard) badgesHard |= bit; else badges |= bit; }
    else if (hard) badgesHardX[rg - 1] |= bit;
    else badgesX[rg - 1] |= bit;
    save();
  }
  uint8_t badgeCountIn(uint8_t rg, bool hard) const {
    uint16_t v = badgeMask(rg, hard);
    uint8_t n = 0;
    while (v) { n += v & 1; v >>= 1; }
    return n;
  }
  // Every region's badges together, for the player card's running total.
  uint8_t badgeCount(bool hard) const {
    uint8_t n = 0;
    for (uint8_t r = 0; r < GYM_REGIONS; r++) n += badgeCountIn(r, hard);
    return n;
  }

  // Level-up learning. lastLearnLevel is the highest level whose gates have
  // been handled, so a move declined once is not offered forever, and the
  // offline catch-up -- which can cross a dozen levels in one go -- queues its
  // offers instead of firing a dozen dialogs at boot.
  uint8_t lastLearnLevel = 0;
  uint8_t learnQueue[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t learnQCount = 0;
  void checkLearnGates();
  bool hasLearnOffer() const { return learnQCount > 0; }
  uint8_t learnOffer() const { return learnQCount ? learnQueue[0] : 0; }
  void acceptLearn(uint8_t slot);   // put the pending move into slot 0..3
  void declineLearn();
  // tope de entrenamiento que permite un IV: 77 (IV 8) .. 100 (IV 31)
  static uint8_t trMaxFor(uint8_t iv) { return 70 + (30 * (uint16_t)iv) / 31; }
  uint8_t trMaxAtk() const { return trMaxFor(ivAtk); }
  uint8_t trMaxDef() const { return trMaxFor(ivDef); }
  uint8_t trMaxSpe() const { return trMaxFor(ivSpe); }
  void play();
  void toggleLight();
  bool isNightHour() const;
  void setScreenOff(bool off);  // the sketch reports the PWR button
  bool screenIsOff = false;
  void dbgTick() { tick(); }   // tests drive minutes directly; tick() is private
  // syncClock() reads "seen" back out of NVS, so a test has to put it there
  void dbgSetSeen(uint32_t e) { lastSeenEpoch = e; prefs.putUInt("seen", e); }
  uint8_t sleepAuto = SLEEP_NONE;   // who decided the current sleep state  // dormir / despertar
  void clean();
  void caress();  // tocar al bicho
  void eggTap();  // tocar el huevo: 3 toques y eclosiona
  void newEgg();   // empezar de cero con un inicial aleatorio
  void release();  // soltar (pulsacion larga + confirmar)
  void syncClock(uint32_t nowEpoch);  // aplica el tiempo transcurrido apagado
  void setClock(uint32_t nowEpoch);   // fija la hora sin aplicar progresion
  void startFarewell();  // tambien usable desde la consola serie (BYE)
  // Retire on demand. Before the farewell is earned this is the SAME ceremony
  // -- the creature is banked exactly as it would be -- but it hands the next
  // creature EVO_PENALTY_LEVELS on every evolution threshold. Retiring one that
  // has already earned its farewell costs nothing: it is then just the button.
  void startRetire();
  bool canRetireNow() const;
  bool retireIsFree() const { return canFarewellNow(); }
  uint8_t evoPenalty() const { return evoPen; }
  void startRunaway();   // tambien usable desde la consola serie (RUN)

  bool isEgg() const { return speciesId < 0; }
  uint8_t eggCracks() const { return eggTaps; }
  bool eating() const { return millis() < eatUntil; }
  bool showHeart() const { return millis() < heartUntil; }
  bool evolving() const { return millis() < evolveUntil; }
  float evolveT() const {     // progreso de la animacion de evolucion 0..1
    uint32_t n = millis();
    uint32_t left = evolveUntil > n ? evolveUntil - n : 0;
    return 1.0f - (float)left / (float)EVOLVE_ANIM_MS;
  }
  bool canEvolveNow() const;  // condiciones de evolucion cumplidas (lista)
  void evolve();              // dispara la transformacion (la llama un toque del usuario)
  bool canFarewellNow() const;  // forma final + 7 dias: lista para despedirse (boton)
  bool canRunawayNow() const;   // abandono total 1h: lista para escaparse (boton triste)
  // el usuario decide en un dialogo; "mantener/quedaros" pospone y re-ofrece luego
  bool wantEvolveButton() const { return canEvolveNow() && level() > evoDeclinedLv; }
  bool wantFarewellButton() const { return canFarewellNow() && ageMinutes >= farDeclinedAge; }
  void declineEvolve() { evoDeclinedLv = level(); }              // re-ofrece al subir de nivel
  void declineFarewell() { farDeclinedAge = ageMinutes + 1440; } // re-ofrece dentro de 1 dia
  // primera partida: el jugador elige inicial (Bulbasaur/Charmander/Squirtle)
  bool awaitingStarter() const { return starterPick; }
  void chooseStarter(int16_t dex) { eggTarget = dex; starterPick = false; save(); }
  void factoryReset() { prefs.clear(); }  // borra la NVS (test: comando serie WIPE)
  void dbgRunawayReady() { fullness = joy = energy = hygiene = 0; neglectTicks = RUNAWAY_TICKS; }  // test
  // test: force what the egg holds and hatch it now (serial command EGG).
  // The legendary/shiny IV guarantees only fire inside hatch(), so without
  // this there is no way to exercise them from outside the class.
  void dbgHatchAs(int16_t dex, bool wantShiny) {
    if (dex < 1 || dex > DEX_COUNT) return;
    eggTarget = dex;
    eggShiny = wantShiny;
    starterPick = false;
    speciesId = -1;
    eggTaps = 0;
    hatch();
  }
  // Capped at 100, the series' own ceiling. The cap is not cosmetic: ageMinutes
  // is never clamped, so without it 1 + ageMinutes/60 overflows this uint8_t
  // past ~10.6 days -- and the RTC catches up to 2 weeks offline, which lands on
  // level 337 and wraps to 81. Reached at 4d 3h; farewell is merely *offered* at
  // 3 days (level 73), so declining it to raise a stronger battler is a real
  // choice rather than an accident.
  uint8_t level() const {
    uint32_t l = 1 + ageMinutes / MINUTES_PER_LEVEL;
    return l > MAX_LEVEL ? MAX_LEVEL : (uint8_t)l;
  }
  bool isRegistered(int16_t dex) const {
    return dex >= 1 && dex <= DEX_COUNT && (dexReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  bool isShinyRegistered(int16_t dex) const {
    return dex >= 1 && dex <= DEX_COUNT && (dexShinyReg[(dex - 1) >> 3] & (1 << ((dex - 1) & 7)));
  }
  uint16_t registeredCount() const;
  bool lineHasUnregistered(int16_t base) const;
  uint8_t eggRarity() const;       // rareza del huevo actual (sin revelar especie)
  int16_t pickEggSpecies();        // publica para poder simular tiradas (EGGS)
  // What the waiting egg would hatch into. Hidden from the PLAYER, not from the
  // code: the serial console already simulates rolls, and the region tests have
  // to see which creature a switch landed on.
  int16_t eggPeek() const { return eggTarget; }
  int16_t rollInRegion(uint8_t r, uint8_t tier);
  uint8_t lowestStat() const { return min(min(fullness, joy), min(energy, hygiene)); }
  PetMood mood() const;
  // progreso de la ceremonia de despedida/escapada, 0..1 (para animarla)
  float ceremonyT() const {
    if (ceremony == CER_NONE) return 0.0f;
    uint32_t n = millis();
    uint32_t left = ceremonyUntil > n ? ceremonyUntil - n : 0;
    return 1.0f - (float)left / (float)CEREMONY_MS;
  }

  // racha / vinculo / medallas / nombre
  void rename(const char *name);
  bool hasMedal(uint16_t m) const { return medals & m; }
  bool showMedal() const { return millis() < medalUntil; }
  bool showMilestone() const { return millis() < milestoneUntil; }
  int careBonus() const;  // mejora del huevo por racha + vinculo

  // guardado periodico diferido: tick() marca pendiente y el loop lo vuelca
  // cuando la pantalla esta atenuada/apagada (la escritura a flash congela
  // ~1s ambos cores: asi no se ve ni corta el tactil)
  bool savePending() const { return pendingSave; }
  void flushSave();
  // Writes NOW, whatever pendingSave says. flushSave() is `if (pendingSave)
  // save()`, so a console command that changed something the game had not
  // already marked dirty wrote nothing at all and quietly depended on the next
  // autosave to notice. Defined out of line: save() is private and declared
  // further down.
  void saveNow();

private:
  Preferences prefs;
  uint32_t lastTick = 0;
  uint32_t eatUntil = 0;
  uint32_t heartUntil = 0;
  uint32_t evolveUntil = 0;
  int16_t eggTarget = 1;       // dex oculto que saldra del huevo
  bool eggShiny = false;       // sorpresa sorteada al crear el huevo
  uint8_t eggTaps = 0;
  uint8_t mistakeCooldown = 0;
  uint8_t ticksSinceSave = 0;
  bool pendingSave = false;     // guardado periodico pendiente de volcar
  uint8_t evoDeclinedLv = 0;    // "mantener forma": no ofrecer evolucion hasta subir de nivel
  uint32_t farDeclinedAge = 0;  // "quedaros juntos": no ofrecer despedida hasta esta edad
  bool starterPick = false;     // primera partida: esperando que el jugador elija inicial
  uint8_t evoPen = 0;           // levels added to this creature's evolution gate
  bool retirePending = false;   // an early retire is under way; newEgg() spends it
  uint8_t neglectTicks = 0;
  uint16_t goodTicks = 0;  // racha bien cuidado: forja la DEF
  uint32_t ceremonyUntil = 0;
  uint8_t bondToday = 0;       // tope diario de subida de vinculo
  uint32_t medalUntil = 0;     // celebracion de medalla en pantalla
  uint32_t milestoneUntil = 0; // celebracion de hito de racha

  uint32_t today() const { return lastSeenEpoch ? lastSeenEpoch / 86400 : 0; }
  void registerCare();   // primer cuidado del dia: racha + vinculo
  void addBond(uint8_t amt);
  uint8_t rollIV(int bonus) const;  // una tirada 8-31 empujada por el cuidado
  void rollIVs();                   // los 4, con las garantias de legendario/shiny
  uint8_t ivFromGene(uint8_t gene) const;  // migracion de guardados con genes
  void defTick(bool resting);       // la calma forja la defensa (ver pet.cpp)
  void snapshotForParty();          // copy into endedMon before newEgg() wipes it
  void checkMedals();
  void tick();
  void applyAutoSleep();
  void hatch();
  void registerSpecies(int16_t dex);
  void save();
  void load();
  static uint8_t clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
};
