// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "types.h"
#include "moves.h"
#include "battle.h"
#include "trainers.h"
#include "link.h"
#include "linknow.h"
#include "backs.h"
#include "badges.h"
#include "avatars.h"
#include <stdarg.h>
#include "party.h"
#include "save.h"
#include "pet.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "i18n.h"
#include "audio.h"

// Version del firmware. Subir este numero en cada release (y manifest.json para
// el instalador web). Se muestra en la pantalla de ajustes y por serie al arrancar.
#define FW_VERSION "2.4"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monShinyFor = false;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
} beh;
#define PET_GROUND 304  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
// 16 to a page, and the Pokedex is browsed ONE REGION AT A TIME. Three
// generations flat is 25 pages of swiping to reach Hoenn, which is not a
// Pokedex, it is a scroll. A vertical swipe changes region and a horizontal one
// pages within it, so nothing is ever more than ten pages from the front.
// ALL is deliberately not offered here -- it is the thing being replaced.
#define GAL_PER_PAGE 16
#define GAL_REGIONS (REGION_COUNT - 1)          // the real regions, not ALL
#define GAL_LO (REGIONS[galleryRegion % GAL_REGIONS].lo)
#define GAL_HI (REGIONS[galleryRegion % GAL_REGIONS].hi)
#define GAL_SPAN (GAL_HI - GAL_LO + 1)
#define GAL_PAGES ((GAL_SPAN + GAL_PER_PAGE - 1) / GAL_PER_PAGE)
uint8_t galleryRegion = 0;
int galleryPage = 0;        // GAL_PAGES paginas de GAL_PER_PAGE
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // pulsacion corta del boton PWR
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;
enum : uint8_t { KB_PET = 0, KB_TRAINER };
uint8_t kbTarget = KB_PET;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_PAGES 4   // profile, stats, moves, progress -- medals moved to
                       // the player card, where the totals already live
uint8_t cardPage = 0;         // 0 perfil, 1 stats+medallas
// Menu overlay: opened by tapping the pet's name on the main screen. The
// horizontal swipe is already taken by the Pokedex (it pages through 10 pages
// internally), so a hub on that axis would be ambiguous; the header was inert
// and is the only free surface left.
bool menuOpen = false;
#define MENU_X 73
// Four rows: PARTY and GYMS came out, since a swipe right and a swipe left now
// reach them directly. Sized to the bezel -- the panel is 320 wide, so 160 from
// the centre, and sqrt(233^2 - 160^2) = 169 means it can only span y 64..402.
#define MENU_Y 104
#define MENU_W 320
#define MENU_H 258
#define MENU_ROW_H 52
#define MENU_ROW_GAP 6
#define MENU_ROWS 4
#define MENU_ROW_Y(i) (MENU_Y + 16 + (i) * (MENU_ROW_H + MENU_ROW_GAP))

// Party screen. partyPick != 0 means the newcomer needs a slot: the player
// either taps someone to replace or lets it go.
bool partyOpen = false;
bool partyPick = false;
uint8_t partyDetail = 0;   // 0 = the grid, else slot + 1
// The box, reached from the party screen. `boxSwapFrom` is the party slot
// waiting for something to trade with, 0 when nothing is pending.
bool boxOpen = false;
uint8_t boxPage = 0;
uint8_t boxSwapFrom = 0;   // party slot + 1, armed from the party side
uint8_t boxSel = 0;        // box slot + 1, armed from the box side
#define BOX_PER_PAGE 6
uint32_t partyBannerUntil = 0;   // "<name> joined the party!"
char partyBannerName[14] = "";
#define PARTY_CELL_W 150
#define PARTY_CELL_H 70
#define PARTY_GRID_X 78
#define PARTY_GRID_Y 88

bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis

// minijuego "toques": mantener la pokeball en el aire
bool gameOpen = false;
uint32_t gameOverUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
bool gameNewHi = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// training submenu (the 5th icon): routes to the trainer for each stat.
// DEF has no minigame -- it rises on its own from good wellbeing -- so its row
// is informational and does not respond to a tap.
bool trainOpen = false;

// move picker, opened from the MOVES card page. Most learnsets are level 0, so
// a level-up "you learned a move" prompt would almost never fire -- the moveset
// is edited on demand instead.
bool movePickOpen = false;
uint8_t movePickSlot = 0;   // which of the 4 slots is being replaced
uint8_t movePickParty = 0;  // 0 = the live pet, else the party slot + 1
uint8_t movePickPage = 0;
#define MOVE_ROW_Y(i) (96 + (i) * 58)
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)
// level-up learn prompt: modal, and deliberately without a timeout -- it
// decides what the creature is for the rest of its life, and once banked into
// the party, forever.
#define LEARN_ROW_Y(i) (104 + (i) * 56)
#define LEARN_SKIP_Y 334

// ---------- battle ----------
// The move menu is a 2x2 grid rather than four stacked rows: the round panel
// has to fit both creatures, both HP bars and the menu, and four full-width
// rows do not leave room for the sprites.
bool battleOpen = false;
Combatant btlYou, btlFoe;
bool btlOver = false;
bool btlWon = false;
bool btlNewBadge = false;
uint32_t btlWinUntil = 0;   // the win screen is up
// A trainer fight is a run of 1v1s: both sides queue their squad and the next
// one steps up when the current one faints. This is the whole difficulty curve
// -- no gating, just attrition, so one strong creature sweeps Brock and dies
// four deep into Lance.
// The ladder is now sequential: a leader opens once the previous one is beaten,
// tracked per difficulty so hard mode is its own run. This replaces the earlier
// "no gating, attrition is the gate" rule -- with both ladders level-capped,
// nothing stopped you opening with Lance and simply losing, which read as a
// dead end rather than a challenge.
// reaction test (trains SPEED)
bool spdOpen = false;
uint32_t spdUntil = 0, spdOverUntil = 0, spdBorn = 0;
int16_t spdX = 0, spdY = 0;
uint16_t spdHits = 0, spdMisses = 0;
uint8_t spdGain = 0;
bool spdNewHi = false;

bool gymOpen = false;
bool gymHard = false;   // which ladder the list is showing

// LAN battle. `lanOpen` is the pairing screen; once both squads are known the
// normal battle screen takes over with btlLink set.
bool lanOpen = false;
Link lan;
static void drawEggRegion();          // defined with the egg screen helpers
static bool eggRegionTap(int16_t x, int16_t y);
static void btlLinkPoll();   // defined with the battle code, called from render()
static void btlSwitchTo(uint8_t i);
static void btlResolve(uint8_t yourMove);
// The peer's whole team, kept live. A trainer's replacements are built fresh
// from TRAINERS[] because they only ever arrive once; a linked opponent can
// switch OUT and back IN, so its creatures have to remember how battered they
// are. Host side only -- the guest takes absolute health off the wire.
Combatant btlFoeSquad[TRAINER_TEAM_MAX];
uint8_t btlFoeSquadN = 0;
uint8_t btlMyAct = 0;        // host: our own action, latched until theirs lands
// Which ladder the gym screen and the current fight belong to. The battle keeps
// its own copy so that leaving the gym list mid-fight cannot retarget the badge.
uint8_t gymRegion = 0;
uint8_t btlRegion = 0;
#define TRAINERS (TRAINER_SETS[gymRegion % GYM_REGIONS].list)
#define BTL_TRAINERS (TRAINER_SETS[btlRegion % GYM_REGIONS].list)
bool gShowAllAvatars = false;  // emulator screenshot aid, never set on hardware
bool btlPetIn = false;       // was the live pet in the squad?
uint8_t btlTrainGain = 0;    // what the win trained, for the win screen
uint8_t btlTrainWhich = 0;
bool btlLink = false;      // this fight is against another device
bool btlLinkHost = false;
static bool gymUnlocked(uint8_t idx, bool hard) {
  return idx == 0 || pet.hasBadge(gymRegion, idx - 1, hard);
}

// Team select. Candidate 0 is the live pet, 1..PARTY_SLOTS are the banked
// members, so one bitmask covers the whole pool.
bool pickOpen = false;
// The team picker serves the gym ladder and the LAN screen both. PICK_LAN is
// not a trainer index: squadCap() already returns an uncapped six for anything
// past the roster, which is what a LAN battle wants -- two players who know
// each other can bring what they like.
#define PICK_LAN 0xFF
static void lanOffer(bool host);
uint8_t pickTrainer = 0;
bool pickHard = false;
bool lanWantHost = true;   // which button opened the picker
uint16_t squadMask = 0xFFFF;   // everything, until the player says otherwise
uint8_t pickPage = 0;
#define PICK_PER_PAGE 6
#define PICK_CELL_W 150
#define PICK_CELL_H 74
#define PICK_X(i) (78 + ((i) % 2) * (PICK_CELL_W + 10))
#define PICK_Y(i) (86 + ((i) / 2) * (PICK_CELL_H + 6))
#define PICK_GO_Y 350
bool playerOpen = false;
// One badge page per gym region, then the medals. Three ladders will not fit on
// one page, and the page you are on IS the region -- no extra control needed,
// and horizontal paging already works everywhere else.
uint8_t playerPage = 0;
#define PLAYER_PAGES (GYM_REGIONS + 1)
#define playerBadgeRegion (playerPage % GYM_REGIONS)
uint8_t gymPage = 0;
#define GYM_ROWS 5
#define GYM_ROW_Y(i) (110 + (i) * 50)
int8_t btlTrainer = -1;      // index into TRAINERS, -1 = a one-off fight
bool btlHard = false;
Combatant btlSquad[TRAINER_TEAM_MAX + 1];
uint8_t btlSquadN = 0, btlSquadAt = 0;
uint8_t btlFoeAt = 0;

// Animation. Deliberately built on the thumbnails the screen already draws
// rather than on PmdMon: three PmdMon blobs are live already, and the battle
// has to stay graceful on a board with no SD at all (S_NO_SPRITES). Index 0 is
// you, 1 is the foe.
uint32_t btlLungeUntil[2] = { 0, 0 };   // acted: leans toward the opponent
uint32_t btlHitUntil[2] = { 0, 0 };     // was hit: jitters and flashes
uint16_t btlHpShown[2] = { 0, 0 };      // bars ease toward the real value
// Two streamed sprites, so the creatures can actually swing and flinch. They
// cost ~135 KB of PSRAM each on average and are freed when the fight ends. The
// player's side is NOT the global `pmd`: the active creature may be a banked
// party member rather than the live pet.
PmdMon btlPmd[2];
int16_t btlPmdDex[2] = { 0, 0 };
// A faint used to swap the next creature in instantly, inside the same call
// that resolved the turn -- which is why it felt like a jump cut. The swap is
// now deferred: the fainted one drops out of frame, and the replacement slides
// in only once the player dismisses that message.
uint32_t btlFaintUntil[2] = { 0, 0 };
uint32_t btlEnterUntil[2] = { 0, 0 };
int8_t btlSwapWho = -1;        // 0 = your side, 1 = the foe's, -1 = nothing due
#define BTL_FAINT_MS 700
#define BTL_ENTER_MS 420
// battle menu: 0 = FIGHT/POKEMON, 1 = the moves, 2 = the switch list
uint8_t btlMenu = 0;
#define BTL_LUNGE_MS 260
#define BTL_HIT_MS 420
char btlMsg[6][40];
uint8_t btlMsgCount = 0;   // queued lines; a tap shows the next
#define BTL_CELL_W 160
#define BTL_CELL_H 44
#define BTL_GRID_X 69
#define BTL_GRID_Y 286
#define BTL_CELL_X(i) (BTL_GRID_X + ((i) % 2) * (BTL_CELL_W + 8))
#define BTL_CELL_Y(i) (BTL_GRID_Y + ((i) / 2) * (BTL_CELL_H + 8))
#define TRAIN_X 73
#define TRAIN_Y 96
#define TRAIN_W 320
#define TRAIN_H 274
#define TRAIN_ROW_H 56
#define TRAIN_ROW_GAP 8
#define TRAIN_ROW_Y(i) (TRAIN_Y + 54 + (i) * (TRAIN_ROW_H + TRAIN_ROW_GAP))

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

#define CX 233  // centro de la pantalla redonda
#define CY 233
#define PET_CY 202  // centro vertical del sprite

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
// Five across the arc: spacing tightened 62 -> 54 so the outer pair stays far
// enough in to keep its old y. Lifting them instead would have run the row into
// the ENE/HYG bars, which end at y=361 -- the buttons are 52 tall, so any centre
// above 387 overlaps them.
#define BTN_COUNT 5
Btn buttons[BTN_COUNT] = {
  { 125, 390, SPR_ICON_FOOD },   // comer
  { 179, 402, SPR_ICON_PLAY },   // jugar
  { 233, 406, SPR_ICON_LIGHT },  // luz
  { 287, 402, SPR_ICON_CLEAN },  // bano
  { 341, 390, SPR_ICON_TRAIN },  // entrenar
};
#define BTN_HALF 26  // boton de 52x52
#define BTN_HIT 36   // radio tactil (un poco mas generoso)

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// eleccion de inicial (primera partida): Bulbasaur / Charmander / Squirtle, 3 filas
static const int16_t STARTER_DEX[3] = { 1, 4, 7 };
#define STARTER_ROW_Y 110
#define STARTER_ROW_H 70
#define STARTER_ROW_GAP 8
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogo de decision: 0 ninguno, 1 evolucion, 2 despedida
uint32_t choiceUntil = 0;   // se cierra solo a este millis
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool holdFired = false;

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s\n", FW_VERSION);
  loadLang();  // idioma guardado (ES por defecto)
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  panel->setBrightness(180);

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("CST9217 no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  party.begin();
  pet.begin();
  sdBegin();
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  if (e == 0) {
    rtcSetEpoch(1767225600UL);  // RTC virgen: semilla (la hora absoluta da igual,
    e = rtcEpoch();             // solo importan las diferencias)
    Serial.println("RTC sin hora: sembrado, sin progresion offline esta vez");
  }
  pet.syncClock(e);

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

  lastInteract = millis();
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdDirty) return;
  sdDirty = false;
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.shiny);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // respaldo: B/N
  }
}

void loop() {
  uint32_t now = millis();
  pet.update(now);

  // The link is pumped here rather than from the LAN screen, because it has to
  // keep running through the battle too: linkNowPoll() drains what the radio
  // parked on the WiFi task, and tick() is what resends a lost packet and gives
  // up on a peer that has gone quiet.
  if (lan.live()) {
    linkNowPoll();
    lan.tick(now);
  }

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  handleSerial();
  ensureMon();

  // A farewell or release just finished: the creature is waiting for a slot.
  // With room it simply joins; with a full party the player is taken straight
  // to the party screen to choose who it replaces, or to let it go.
  if (pet.endedKind != CER_NONE && !partyPick) {
    // party first, then the box; only a full box makes it your choice
    if (party.add(pet.endedMon) || party.boxAdd(pet.endedMon)) {
      snprintf(partyBannerName, sizeof(partyBannerName), "%s",
               pet.endedMon.nick[0] ? pet.endedMon.nick : DEX_TBL[pet.endedMon.dex].name);
      partyBannerUntil = now + 3500;
      pet.endedKind = CER_NONE;
      sfxPlay(SFX_MEDAL);
    } else {
      partyPick = true;
      partyOpen = true;
      menuOpen = false;
    }
  }

  // pulsacion corta del PWR: pantalla on/off
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      if (!screenOff) lastInteract = now;
    }
  }

  updateBrightness(now);

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u\n", (unsigned long)(now / 1000),
                  ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }

  // 85 ms en juego/saco: margen seguro para que el redibujado no pise el envio
  // DMA del frame anterior (a 40-65 ms solapaba y causaba flashes negros; con
  // sprites grandes el dibujo tarda mas, asi que se deja colchon)
  if (now - lastRender >= (uint32_t)((gameOpen || sackOpen || spdOpen) ? 85 : 100)) {
    lastRender = now;
    render();
  }
}

// brillo segun sueno + inactividad (proteccion del AMOLED)
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart()) {
    lastInteract = now;
  }
  uint32_t idle = now - lastInteract;
  dimStage = (idle > 300000) ? 2 : (idle > 90000) ? 1 : 0;
  uint8_t target = pet.sleeping ? 25 : (usbPresent() ? 180 : 145);
  if (dimStage == 1) target = pet.sleeping ? 10 : 60;
  else if (dimStage == 2) target = 8;
  if (screenOff) target = 0;
  static uint8_t current = 255;
  if (target != current) {
    current = target;
    panel->setBrightness(target);
  }
}

// ---------- consola serie (provision de SD + depuracion) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    pet.ageMinutes = (uint32_t)line.substring(4).toInt() * MINUTES_PER_LEVEL;
    Serial.println("DONE");
  } else if (line.startsWith("IV ")) {
    // IV <fue> <def> <vel> <vit>: fija los valores individuales (pruebas).
    // Con "IV 31 31 31 31" se ve el techo; con "IV 8 8 8 8" el suelo.
    int v[4] = { 16, 16, 16, 16 };
    int n = sscanf(line.c_str() + 3, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1) {
      for (int i = 0; i < 4; i++) v[i] = v[i] < 0 ? 0 : (v[i] > 31 ? 31 : v[i]);
      pet.ivAtk = v[0];
      pet.ivDef = (n >= 2) ? v[1] : v[0];
      pet.ivSpe = (n >= 3) ? v[2] : v[0];
      pet.ivHp = (n >= 4) ? v[3] : v[0];
      if (pet.trAtk > pet.trMaxAtk()) pet.trAtk = pet.trMaxAtk();
      if (pet.trDef > pet.trMaxDef()) pet.trDef = pet.trMaxDef();
      if (pet.trSpe > pet.trMaxSpe()) pet.trSpe = pet.trMaxSpe();
    }
    Serial.printf("iv=%u/%u/%u/%u topes=%u/%u/%u\n", pet.ivAtk, pet.ivDef,
                  pet.ivSpe, pet.ivHp, pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line.startsWith("EGG ")) {
    // EGG <dex> [shiny]: hatch a chosen species right now. The legendary and
    // shiny IV guarantees only apply at hatch time, so this is the only way to
    // exercise them on hardware (SHINY below just toggles the flag afterwards).
    int dex = 0, sh = 0;
    int n = sscanf(line.c_str() + 4, "%d %d", &dex, &sh);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      pet.dbgHatchAs(dex, sh != 0);
      Serial.printf("%s%s iv=%u/%u/%u/%u\n", DEX_TBL[dex].name,
                    pet.shiny ? " *SHINY*" : "", pet.ivAtk, pet.ivDef,
                    pet.ivSpe, pet.ivHp);
    }
    Serial.println("DONE");
  } else if (line.startsWith("BATTLE")) {
    // BATTLE <dex> [level] -- the only way in until the trainer roster exists
    int dex = 0, lvl = 0;
    int n = sscanf(line.c_str() + 6, "%d %d", &dex, &lvl);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      startBattle(dex, lvl > 0 ? (uint8_t)lvl : pet.level());
      Serial.printf("battle vs %s Lv.%u\n", DEX_TBL[dex].name, btlFoe.level);
    } else {
      Serial.println("uso: BATTLE <dex> [nivel]");
    }
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "EXPORT") {
    // Prints the whole save as a block of IMPORT commands. Pasting that block
    // back is the restore -- there is no separate format to get wrong, and no
    // single 2000-character line for a terminal to mangle.
    static uint8_t buf[2048];
    size_t n = saveExport(buf, sizeof(buf));
    if (!n) { Serial.println("EXPORT FAIL"); return; }
    Serial.printf("# TamaPoke save, %u bytes. Paste this whole block back.\n",
                  (unsigned)n);
    for (size_t i = 0; i < n; i += 48) {
      Serial.print("IMPORT ");
      for (size_t j = i; j < i + 48 && j < n; j++) Serial.printf("%02X", buf[j]);
      Serial.println();
    }
    Serial.println("IMPORT");        // the empty one commits
  } else if (line.startsWith("IMPORT")) {
    // IMPORT <hex>   append a chunk
    // IMPORT         commit what has been appended
    static uint8_t in[2048];
    static size_t inN = 0;
    String hex = line.substring(6);
    hex.trim();
    if (hex.length()) {
      if (hex.length() & 1) { Serial.println("IMPORT ODD"); inN = 0; return; }
      for (size_t i = 0; i + 1 < (size_t)hex.length(); i += 2) {
        if (inN >= sizeof(in)) { Serial.println("IMPORT FULL"); inN = 0; return; }
        auto nyb = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return -1;
        };
        const char *hs = hex.c_str();
        int hi = nyb(hs[i]), lo = nyb(hs[i + 1]);
        if (hi < 0 || lo < 0) { Serial.println("IMPORT BAD"); inN = 0; return; }
        in[inN++] = (uint8_t)((hi << 4) | lo);
      }
      return;                        // silent while collecting
    }
    if (!inN) { Serial.println("IMPORT EMPTY"); return; }
    bool ok = saveImport(in, inN);
    Serial.println(ok ? "IMPORT OK" : "IMPORT REJECTED");
    inN = 0;
    if (ok) { Serial.println("DONE"); delay(100); ESP.restart(); }
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line.startsWith("PARTY")) {
    // PARTY          list the party
    // PARTY <dex>    bank a level-50 specimen (fills slots up for testing)
    // PARTY CLEAR    empty it
    String arg = line.substring(5);
    arg.trim();
    if (arg == "CLEAR") {
      for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
    } else if (arg.length()) {
      int d = arg.toInt();
      if (d >= 1 && d <= DEX_COUNT) {
        PartyMon m;
        m.dex = d;
        m.level = 50;
        m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
        m.trAtk = m.trDef = m.trSpe = 50;
        Serial.println(party.add(m) ? "added" : "party full");
      }
    }
    Serial.printf("party %u/%u:", party.count(), PARTY_SLOTS);
    for (int i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = party.slots[i];
      if (m.empty()) Serial.print(" -");
      else Serial.printf(" %s%s(nv%u)", DEX_TBL[m.dex].name, m.shiny ? "*" : "", m.level);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "REG") {
    Serial.printf("pokedex %u/%u:", pet.registeredCount(), DEX_COUNT);
    for (int i = 1; i <= DEX_COUNT; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    Serial.println("DONE");
  } else if (line == "STATS") {
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u vit=%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.vitStat(), pet.berryKnown);
    Serial.printf("iv=%u/%u/%u/%u tr=%u/%u/%u topes=%u/%u/%u\n",
                  pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp,
                  pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.printf("shiny=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

// ---------- entrada tactil ----------

bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      lastInteract = millis();
      if (y < 72) leaveSack();       // tocar arriba = salir, conservando lo ganado
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    swallowGesture = (dimStage > 0) || screenOff;  // si estaba a oscuras, solo despierta
    screenOff = false;
    lastInteract = millis();
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    if (!holdFired && !swallowGesture && !galleryOpen && !cardOpen && !kbOpen && !clockOpen && millis() - tStart > 3000 &&
        abs(tXl - tX0) < 30 && abs(tYl - tY0) < 30 && inPetZone(tX0, tY0) &&
        !pet.isEgg() && !confirmUntil && !pet.ceremony) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    lastInteract = millis();
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (!holdFired && !swallowGesture) {
      if (abs(dx) > 80 && abs(dy) < 70 && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > 80 && abs(dx) < 70 && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < 40 && abs(dy) < 40) onTap(tX0, tY0);
    }
  }
  wasPressed = pressed;
}

// deslizar vertical: abre/cierra la ficha del bicho
void openClock();  // prototipo

void onSwipeV(int dir) {
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight
  if (pickOpen) { pickOpen = false; return; }
  if (lanOpen) { lanLeave(); lanOpen = false; return; }
  if (gymOpen) {
    // Same gesture as the Pokedex: vertical changes region, horizontal pages.
    gymRegion = (uint8_t)((gymRegion + (dir > 0 ? 1 : GYM_REGIONS - 1)) % GYM_REGIONS);
    gymPage = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  if (playerOpen) { playerOpen = false; return; }
  if (trainOpen) { trainOpen = false; return; }
  if (movePickOpen) { movePickOpen = false; return; }
  if (boxOpen) { boxOpen = false; boxSel = 0; return; }   // vertical backs out
  if (partyOpen) {
    if (partyDetail) { partyDetail = 0; return; }
    if (partyPick) { partyPick = false; pet.endedKind = CER_NONE; }
    partyOpen = false;
    return;
  }
  // Either minigame exits on a swipe. A swipe cannot be confused with a ball
  // hit -- the gesture resolver separates them -- which the header tap no
  // longer can now that the ball is hittable up there.
  if (gameOpen) { leaveGame(); return; }
  if (sackOpen) { leaveSack(); return; }
  if (spdOpen) { leaveSpeed(); return; }
  if (galleryOpen) {
    if (galleryDetail) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; return; }
    galleryRegion = (uint8_t)((galleryRegion + (dir > 0 ? 1 : GAL_REGIONS - 1)) % GAL_REGIONS);
    galleryPage = 0;
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (kbOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; return; }
  if (cardOpen) {
    if (dir < 0) cardOpen = false;  // arriba cierra la ficha
    return;
  }
  // Swipe down is the PLAYER card, up is the creature's. The clock lost this
  // gesture on purpose -- the menu's SETTINGS row already opens it, and the
  // player card is the thing you reach for far more often.
  if (dir > 0) {
    if (!confirmUntil && !feedMenuUntil) playerOpen = true;
  } else if (!pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;                // deslizar arriba: ficha
    cardPage = 0;
  }
}

// party screen: pick a slot (when a newcomer is waiting) or just leave
// A banked creature's sheet: its moves above all, since typing alone does not
// tell you whether that Lapras still has ICE BEAM -- and in hard mode that is
// what decides the fight.
void renderPartyDetail() {
  const PartyMon &m = party.slots[partyDetail - 1];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (m.empty()) { partyDetail = 0; return; }
  const DexEntry &d = DEX_TBL[m.dex];
  char head[36];
  snprintf(head, sizeof(head), "%s%s Lv.%u", m.shiny ? "*" : "",
           m.nick[0] ? m.nick : d.name, (unsigned)m.level);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 40);
  gfx->print(head);
  char ty[24];
  if (d.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(d.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(d.type1), typeName(d.type2));
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(ty) * 3, 64);
  gfx->print(ty);

  for (int i = 0; i < MOVE_SLOTS; i++)
    drawMoveRow(78 + i * 52, m.moves[i], false, m.dex);

  char st[40];
  snprintf(st, sizeof(st), "ATK %u  DEF %u  SPD %u  HP %u",
           party.atkOf(m), party.defOf(m), party.speOf(m), party.vitOf(m));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(st) * 3, 300);
  gfx->print(st);
  // Bringing one back is only offered while an egg is waiting. Otherwise it
  // would silently destroy whatever creature is currently alive, and a rule the
  // player cannot see is worse than a button they cannot press.
  bool canRevive = pet.isEgg() && !pet.awaitingStarter();
  gfx->fillRoundRect(126, 340, 214, 38, 10, canRevive ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(126, 340, 214, 38, 10, UI_INK);
  gfx->setTextColor(canRevive ? UI_BG_DAY : 0x8410);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_REVIVE)) * 6, 351);
  gfx->print(T(S_REVIVE));
  if (!canRevive) {
    gfx->setTextColor(UI_TRACK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(T(S_REVIVE_EGG)) * 3, 384);
    gfx->print(T(S_REVIVE_EGG));
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 404);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void partyTap(int16_t x, int16_t y) {
  if (boxOpen) { boxTap(x, y); return; }
  if (!partyPick && y >= 336 && y <= 368 && x >= 158 && x <= 308) {
    boxOpen = true;                  // open the box, nothing picked yet
    boxPage = 0;
    boxSwapFrom = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  if (partyDetail) {
    if (y >= 340 && y <= 378 && x >= 126 && x <= 340) {   // BRING BACK
      if (!pet.isEgg() || pet.awaitingStarter()) { sfxPlay(SFX_DENY); return; }
      pet.reviveFrom(party.slots[partyDetail - 1]);
      party.releaseAt(partyDetail - 1);      // it is alive now, not banked
      partyDetail = 0;
      partyOpen = false;
      sfxPlay(SFX_HATCH);
      return;
    }
    for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a move to change it
      int ry = 78 + i * 52;
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      movePickParty = partyDetail;
      movePickSlot = i;
      movePickPage = 0;
      movePickOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    partyDetail = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  // exit button, and the top band, both always work
  if ((y >= 372 && y <= 416 && x >= 133 && x <= 333) || y < 34) {
    if (partyPick) {                 // declined the swap: the pet is let go
      partyPick = false;
      pet.endedKind = CER_NONE;
    }
    partyOpen = false;
    sfxPlay(SFX_TAP);
    return;
  }
  for (int i = 0; i < PARTY_SLOTS; i++) {
    int cx0 = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int cy0 = PARTY_GRID_Y + (i / 2) * (PARTY_CELL_H + 8);
    if (x < cx0 || x > cx0 + PARTY_CELL_W || y < cy0 || y > cy0 + PARTY_CELL_H) continue;
    if (boxSel) {                    // a box creature is waiting for a slot
      party.swapPartyBox(i, boxSel - 1);
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (!partyPick) {
      if (boxSwapFrom == i + 1) {    // tapped again: take it to the box
        boxOpen = true;
        boxPage = 0;
        sfxPlay(SFX_TAP);
        return;
      }
      if (party.slots[i].empty()) { boxSwapFrom = i + 1; sfxPlay(SFX_TAP); return; }
      partyDetail = i + 1;
      boxSwapFrom = i + 1;           // armed, in case the box is opened next
      sfxPlay(SFX_TAP);
      return;
    }
    party.replaceAt(i, pet.endedMon);
    snprintf(partyBannerName, sizeof(partyBannerName), "%s",
             pet.endedMon.nick[0] ? pet.endedMon.nick : DEX_TBL[pet.endedMon.dex].name);
    partyBannerUntil = millis() + 3500;
    pet.endedKind = CER_NONE;
    partyPick = false;
    partyOpen = false;
    sfxPlay(SFX_MEDAL);
    return;
  }
}

// deslizar: dir +1 = hacia la derecha
void onSwipe(int dir) {
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight
  if (pickOpen) {   // horizontal pages the candidates, as everywhere else
    uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
    if (!pages) pages = 1;
    int p = (int)pickPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) pickOpen = false;
    else pickPage = (uint8_t)p;
    return;
  }
  if (gymOpen) {   // horizontal pages the ladder; vertical backs out
    uint8_t pages = (TRAINER_COUNT + GYM_ROWS - 1) / GYM_ROWS;
    int p = (int)gymPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) gymOpen = false;
    else gymPage = (uint8_t)p;
    return;
  }
  if (playerOpen) {   // horizontal pages it, like the card and the gallery
    int p = (int)playerPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= PLAYER_PAGES) playerOpen = false;
    else playerPage = (uint8_t)p;
    return;
  }
  if (trainOpen) { trainOpen = false; return; }
  if (movePickOpen) {   // the picker is paged; without this its later pages
    uint8_t all[64];    // were simply unreachable
    uint8_t n = learnableList(all, sizeof(all));
    uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
    int p = (int)movePickPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) movePickOpen = false;
    else movePickPage = (uint8_t)p;
    return;
  }
  if (boxOpen) {   // horizontal pages the box, as every other paged screen
    uint8_t pages = BOX_SLOTS / BOX_PER_PAGE;
    int p = (int)boxPage + (dir > 0 ? -1 : 1);
    if (p < 0 || p >= pages) { boxOpen = false; boxSel = 0; }
    else boxPage = (uint8_t)p;
    return;
  }
  if (partyOpen) {
    if (partyDetail) { partyDetail = 0; return; }
    if (partyPick) { partyPick = false; pet.endedKind = CER_NONE; }
    partyOpen = false;
    return;
  }
  if (gameOpen) { leaveGame(); return; }   // swipe out, keeping what you earned
  if (spdOpen) { leaveSpeed(); return; }
  if (kbOpen || clockOpen) return;
  if (cardOpen) {  // dentro de la ficha: cambiar entre las 4 paginas
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // izquierda avanza
    cardPage = p < 0 ? 0 : (p > CARD_PAGES - 1 ? CARD_PAGES - 1 : p);
    return;
  }
  if (!galleryOpen) {
    // Swipe LEFT is the gym ladder, RIGHT is the party. The Pokedex lost this
    // gesture: it has a menu row, and gestures are worth more spent on screens
    // without one.
    if (!pet.ceremony && !confirmUntil) {
      if (dir < 0) { gymOpen = true; gymPage = 0; }
      else partyOpen = true;
    }
    return;
  }
  if (galleryDetail) {  // en detalle: volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  int np = galleryPage - dir;  // deslizar a la izquierda avanza pagina
  if (np < 0) {                // retroceder desde la primera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  if (np > GAL_PAGES - 1) np = GAL_PAGES - 1;
  if (np != galleryPage) {
    galleryPage = np;
    galleryDirty = true;
  }
}

void onTap(int16_t x, int16_t y) {
  // Serial.printf("TOUCH %d %d\n", x, y);  // diagnostico (silenciado: satura el log)
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial
    for (int i = 0; i < 3; i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= 70 && x <= 396 && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(STARTER_DEX[i]);
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (battleOpen) {
    battleTap(x, y);
    return;
  }
  if (playerOpen) {
    if (playerPage == 0 && y >= 32 && y < 68) {   // the name: rename yourself
      openKeyboardFor(KB_TRAINER);
      sfxPlay(SFX_TAP);
      return;
    }
    // the avatar is the only other live target; everything else backs out
    if (playerPage == 0 && x > CX - 40 && x < CX + 40 && y > 70 && y < 146) {
      pet.avatar = (uint8_t)((pet.avatar + 1) % AVATAR_COUNT);
      pet.flushSave();
      sfxPlay(SFX_TAP);
      return;
    }
    playerOpen = false;
    return;
  }
  if (pickOpen) {
    pickTap(x, y);
    return;
  }
  if (lanOpen) {
    lanTap(x, y);
    return;
  }
  if (gymOpen) {
    if (y >= 76 && y <= 100) {          // the difficulty pill
      gymHard = !gymHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= 380 && y <= 412 && x >= 148 && x <= 318) {   // LAN battle
      gymOpen = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < GYM_ROWS; i++) {
      uint8_t idx = gymPage * GYM_ROWS + i;
      if (idx >= TRAINER_COUNT) break;
      int ry = GYM_ROW_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 44) continue;
      if (!gymUnlocked(idx, gymHard)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      gymOpen = false;
      pickTrainer = idx;
      pickHard = gymHard;
      pickPage = 0;
      pickDefault(squadCap(idx, gymHard));
      pickOpen = true;
      return;
    }
    gymOpen = false;
    return;
  }
  if (pet.hasLearnOffer()) {
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int ry = LEARN_ROW_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      pet.acceptLearn(i);
      return;
    }
    if (x >= 70 && x <= 396 && y >= LEARN_SKIP_Y && y <= LEARN_SKIP_Y + 44) {
      sfxPlay(SFX_TAP);
      pet.declineLearn();
    }
    return;   // modal: nothing else on screen responds until it is answered
  }
  if (trainOpen) {
    bool inPanel = (x >= TRAIN_X && x <= TRAIN_X + TRAIN_W &&
                    y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H);
    if (!inPanel) { trainOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < 2; i++) {   // row 2 is DEF: passive, deliberately inert
      int ry = TRAIN_ROW_Y(i);
      if (x < TRAIN_X + 18 || x > TRAIN_X + TRAIN_W - 18) continue;
      if (y < ry || y > ry + TRAIN_ROW_H) continue;
      sfxPlay(SFX_TAP);
      trainOpen = false;
      if (i == 0) startSack();
      else startSpeedGame();
      return;
    }
    return;
  }
  // The menu is modal and has three independent ways out: the CLOSE row, a tap
  // anywhere on the dimmed area outside the panel, and any swipe (see onSwipe).
  // Deliberately no timeout: a menu that vanishes while you read it is worse
  // than one that lingers.
  if (menuOpen) {
    bool inPanel = (x >= MENU_X && x <= MENU_X + MENU_W &&
                    y >= MENU_Y && y <= MENU_Y + MENU_H);
    if (!inPanel) { menuOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < MENU_ROWS; i++) {
      int ry = MENU_ROW_Y(i);
      if (x < MENU_X + 18 || x > MENU_X + MENU_W - 18) continue;
      if (y < ry || y > ry + MENU_ROW_H) continue;
      sfxPlay(SFX_TAP);
      menuOpen = false;
      if (i == 0) { cardOpen = true; cardPage = 1; }   // straight to the stats page
      else if (i == 1) { galleryOpen = true; galleryPage = 0; galleryDetail = 0; galleryDirty = true; }
      else if (i == 2) { openClock(); }
      return;                                     // i == 3 is CLOSE: just shut
    }
    return;
  }
  if (movePickOpen) {
    uint8_t all[64];
    uint8_t n = learnableList(all, sizeof(all));
    for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
      uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
      if (idx >= n) break;
      int ry = MOVE_PICK_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      // Swapping for a move already in another slot would silently duplicate
      // it, so trade the two slots instead of overwriting.
      uint8_t *tgt = pickTargetMoves();
      for (int s = 0; s < MOVE_SLOTS; s++)
        if (tgt[s] == all[idx] && s != movePickSlot) tgt[s] = tgt[movePickSlot];
      tgt[movePickSlot] = all[idx];
      if (movePickParty) party.save(); else pet.flushSave();
      movePickOpen = false;
      return;
    }
    movePickOpen = false;   // tap anywhere else = back to the moves page
    return;
  }
  if (partyOpen) {
    partyTap(x, y);
    return;
  }
  if (galleryOpen) {
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (cardPage == 0 && y < 84) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 2) {
      for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a slot to change it
        int ry = MOVE_ROW_Y(i);
        if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
        sfxPlay(SFX_TAP);
        movePickParty = 0;      // the live pet
        movePickSlot = i;
        movePickPage = 0;
        movePickOpen = true;
        return;
      }
      cardOpen = false;            // anywhere else on the page still exits
    } else {
      cardOpen = false;
    }
    return;
  }
  if (spdOpen) {
    spdTap(x, y);
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    bool b1 = (x >= 93 && x <= 373 && y >= 206 && y <= 258);  // accion
    bool b2 = (x >= 93 && x <= 373 && y >= 268 && y <= 320);  // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.shiny); }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 2) {          // despedida
      if (b1) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (millis() < confirmUntil && x >= 118 && x <= 218 && y >= 252 && y <= 304) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (millis() < feedMenuUntil && y >= 288 && y <= 352 && x >= 101 && x <= 365) {
      int item = (x - 101) / 66;
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    // the region pill first, or choosing a region would also crack the egg
    if (eggRegionTap(x, y)) return;
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // botones de final (mismo recuadro): escapada directa; despedida abre dialogo
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
    if (pet.wantFarewellButton()) { choiceKind = 2; choiceUntil = millis() + 12000; return; }
  }
  for (int i = 0; i < BTN_COUNT; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      Serial.printf("BTN %d\n", i);
      sfxPlay(SFX_TAP);
      if (i == 0) {
        if (!pet.sleeping) feedMenuUntil = millis() + 6000;
      } else if (i == 1) {
        startGame();
      } else if (i == 2) {
        pet.toggleLight();
      } else if (i == 3) {
        startBath();
      } else {
        if (!pet.sleeping) trainOpen = true;
      }
      return;
    }
  }
  // tapping the name/status band opens the menu. This band was inert before,
  // and it sits clear of inPetZone (which starts at y 95).
  if (y >= 28 && y < 94) {
    menuOpen = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    Serial.println("PET");
    pet.caress();
    if (!pet.sleeping) sfxPlay(SFX_HEART);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)((now / 50 + k * 250) % 560) - 40;
    int cy = 70 + k * 34;
    gfx->fillCircle(cx, cy, 16, col);
    gfx->fillCircle(cx + 18, cy + 3, 13, col);
    gfx->fillCircle(cx - 15, cy + 4, 12, col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer

  // cielo en bandas
  for (int y = 0; y < HORIZON; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, HORIZON));

  // sol o luna
  if (night) {
    gfx->fillCircle(360, 78, 24, C565(0xe8, 0xee, 0xf5));
    gfx->fillCircle(370, 72, 22, lerp565(top, bot, 78, HORIZON));  // creciente
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  } else if (h < 18) {
    gfx->fillCircle(360, 84, 26, h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(now, C565(0xff, 0xff, 0xff));
  } else {
    gfx->fillCircle(233, HORIZON - 6, 34, C565(0xff, 0xf1, 0xc8));  // sol poniente
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    gfx->fillRect(0, HORIZON - 26, 466, 26, sea);
    for (int i = 0; i < 3; i++) {
      int wy = HORIZON - 22 + i * 7;
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      gfx->fillRect(60 + ((now / 60 + i * 30) % 60), wy, 26, 2, fc);
      gfx->fillRect(300 - ((now / 60 + i * 20) % 60), wy, 26, 2, fc);
    }
  }

  // suelo
  gfx->fillRect(0, HORIZON, 466, 466 - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-60, HORIZON - 14, 586, 60, 30, hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { 60, 150, 360, 416 }) {
      gfx->fillTriangle(tx, HORIZON - 46, tx - 16, HORIZON, tx + 16, HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - 60, tx - 12, HORIZON - 28, tx + 12, HORIZON - 28, dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    gfx->fillTriangle(70, HORIZON, 40, HORIZON + 30, 100, HORIZON + 30, dk);
    gfx->fillTriangle(400, HORIZON + 4, 372, HORIZON + 30, 430, HORIZON + 30, dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(120 + e * 70, HORIZON + 8 + (e % 2) * 6, 4, 4, C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    gfx->fillTriangle(140, HORIZON - 50, 60, HORIZON, 220, HORIZON, dk);
    gfx->fillTriangle(330, HORIZON - 38, 250, HORIZON, 410, HORIZON, dk);
  } else if (biome == 5 && !night) {  // nieve: copos cayendo
    for (int f = 0; f < 10; f++) {
      int fx = (f * 53 + now / 40) % 466;
      int fy = (f * 90 + now / 18) % HORIZON;
      gfx->fillRect(fx, fy, 3, 3, UI_WHITE);
    }
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { 80, 175, 300, 395 })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + b * 5, HORIZON + 6, 2, 8 + (b == 0 ? 4 : 0), dk);
  }
}

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(t) * 6, 68);
  gfx->print(t);
  for (int i = 0; i < 3; i++) {
    int16_t d = STARTER_DEX[i];
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(178, ry + 24);
    gfx->print(de.name);
  }
  gfx->flush();
}

void render() {
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial (prioridad total)
    renderStarterSelect();
    return;
  }
  if (galleryOpen) {
    renderGallery();
    return;
  }
  if (movePickOpen) {
    renderMovePick();
    return;
  }
  if (partyOpen) {
    if (boxOpen) renderBox();
    else if (partyDetail) renderPartyDetail();
    else renderParty();
    return;
  }
  if (gameOpen) {
    renderGame();
    return;
  }
  if (sackOpen) {
    renderSack();
    return;
  }
  if (spdOpen) {
    renderSpeed();
    return;
  }
  if (trainOpen) {
    renderTrain();
    return;
  }
  if (kbOpen) {
    renderKeyboard();
    return;
  }
  if (clockOpen) {
    renderClock();
    return;
  }
  if (battleOpen) {
    btlLinkPoll();
    renderBattle();
    return;
  }
  if (pickOpen) {
    renderPick();
    return;
  }
  if (lanOpen) {
    renderLan();
    return;
  }
  if (gymOpen) {
    renderGyms();
    return;
  }
  if (playerOpen) {
    renderPlayer();
    return;
  }
  if (pet.hasLearnOffer()) {
    renderLearn();
    return;
  }
  if (cardOpen) {
    renderCard();
    return;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(d.name, d.accent, msg);
    drawCeremony();
    gfx->flush();
    return;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(rar) * 6, 316);
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT), pet.registeredCount(), DEX_COUNT);
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    gfx->setTextColor(inkColor());
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(reg) * 6, 344);
    gfx->print(reg);

    // Which generation this egg comes from. It lives HERE rather than in the
    // settings screen because this is the only moment it does anything: the
    // species is decided when the egg appears, so choosing the region is
    // something you do to the egg in front of you.
    drawEggRegion();
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : d.name;
    snprintf(name, sizeof(name), T(S_NAME_FMT), pet.shiny ? "*" : "", base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    drawStreakBadge();
    drawPet();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // CTA dorado: despedida
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    gfx->setTextSize(3);
    gfx->setCursor(320, 130);
    gfx->print("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (millis() > feedMenuUntil) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(101, 288, 264, 64, 14, UI_WHITE);
      gfx->drawRoundRect(101, 288, 264, 64, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, 110, 296, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, 176, 296, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, 242, 296, 3, false);
      drawMap(SPR_ICON_CANDY, 16, 308, 296, 3, false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (millis() > confirmUntil) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(94, 168, 278, 152, 16, UI_WHITE);
      gfx->drawRoundRect(94, 168, 278, 152, 16, UI_INK);
      char q[28];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), DEX_TBL[pet.speciesId].name);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(q) * 6, 196);
      gfx->print(q);
      gfx->fillRoundRect(118, 252, 100, 52, 12, UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(118 + (100 - (int)strlen(T(S_YES)) * 12) / 2, 270);
      gfx->print(T(S_YES));
      gfx->fillRoundRect(248, 252, 100, 52, 12, UI_BAR_BAD);
      gfx->setCursor(248 + (100 - (int)strlen(T(S_NO)) * 12) / 2, 270);
      gfx->print(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (millis() > choiceUntil) choiceKind = 0;
    else drawChoiceDialog();
  }

  // "<name> joined the party!" after a farewell or release
  if (partyBannerUntil) {
    if (millis() > partyBannerUntil) {
      partyBannerUntil = 0;
    } else {
      char b[40];
      snprintf(b, sizeof(b), T(S_PARTY_JOINED), partyBannerName);
      gfx->fillRoundRect(53, 176, 360, 74, 16, UI_BAR_OK);
      gfx->drawRoundRect(53, 176, 360, 74, 16, UI_INK);
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(b) * 6, 206);
      gfx->print(b);
    }
  }

  if (menuOpen) drawMenu();

  gfx->flush();
}

// ---------- minijuego: toques con la pokeball ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameOpen = true;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = 233;
  respawnBall();
}

void respawnBall() {
  ballX = 150 + random(166);
  ballY = 96;
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

// Leaving a minigame early banks what was actually earned rather than voiding
// it. Quitting used to forfeit everything, which mattered little when the ball
// game trained a stat you could grind back -- but it is now purely about
// happiness, and a pet that just played should be happier for it. The
// gameOver/over guards stop a swipe during the results screen paying twice.
void leaveGame() {
  if (!gameOverUntil) pet.playResult(gameScore);
  gameOpen = false;
}
void leaveSack() {
  if (!sackOverUntil) pet.trainStrength(sackHits);
  sackOpen = false;
}
void leaveSpeed() {
  if (!spdOverUntil) pet.trainSpeed(spdHits);
  spdOpen = false;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  // The ball is checked BEFORE the quit strip. The ball bounces inside a circle
  // of radius 205 about the centre, so it reaches y=28 -- well inside the y<72
  // header. Reaching up to hit a high ball used to abandon the game instead,
  // silently forfeiting the score, the speed training and the record.
  float dx = ballX - x, dy = ballY - y;
  bool onBall = (dx * dx + dy * dy < 74 * 74);
  if (!onBall && y < 72) {  // tocar la cabecera = salir, conservando lo ganado
    leaveGame();
    return;
  }
  if (onBall) {  // toque a la bola!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
    float lift = 6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f);
    ballVY = -lift;
    ballVX += dx * 0.12f;
    if (ballVX > 6.5f) ballVX = 6.5f;
    if (ballVX < -6.5f) ballVX = -6.5f;
    hitX = ballX;
    hitY = ballY;
    hitTime = millis();
  }
}

void stepGame() {
  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  ballVY += grav;
  ballX += ballVX;
  ballY += ballVY;
  // rebote en la pared circular
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 205) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 0.85f;
      ballVY = (ballVY - 2 * dot * ny) * 0.85f;
    }
    ballX = CX + nx * 205;
    ballY = CY + ny * 205;
  }
  if (ballY > 384) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // actualiza el record y da felicidad
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  if (chase > 7) chase = 7;
  if (chase < -7) chase = -7;
  gamePetX += chase;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

void sackTap() {
  if (millis() >= sackUntil) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
}

void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (now > sackOverUntil) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (now >= sackUntil) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(6);
  gfx->setCursor(CX - strlen(buf) * 18, 268);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_HIT_FAST)) * 6, 322);
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if (millis() > gameOverUntil) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(buf) * 12, 160);
    gfx->print(buf);
    gfx->setTextSize(2);
    if (gameNewHi && gameScore > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 214);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(rec) * 6, 214);
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(CX - strlen(msg) * 6, 250);
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(buf) * 12, 30);
  gfx->print(buf);
  char rec[12];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(rec) * 6, 76);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + 4) ? PMD_WALKR : (ballX < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, 394, millis(), true, false, 3);
  } else if (mon.loaded) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = (int)gamePetX - w / 2, py = 394 - h;
    for (int r = 0; r < mon.h; r++)
      for (int c = 0; c < mon.w; c++) {
        uint8_t idx = fr[r * mon.w + c];
        if (idx == 0xFF) continue;
        gfx->fillRect(px + c * s, py + r * s, s, s, mon.pal[idx]);
      }
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // la pokeball
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 24, (int)ballY - 24, 3, false);

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

// una fila de la ficha: etiqueta, barra, valor y (si iv != IV_NONE) el valor
// individual que fija el techo de ese stat
// (sin argumento por defecto: el generador de prototipos de Arduino los
// descarta y las llamadas que lo omitan no compilarian)
#define IV_NONE 0xFF
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar,
                  uint16_t color, uint8_t iv) {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(70, y);
  gfx->print(label);
  // The bar used to start at 112, which leaves 42px for a label drawn at size 2
  // -- three characters. BOND (EN), LIEN (FR) and LACO (PT) are four, so the
  // label ran under the bar. 132 fits five, with the bar narrowed to keep the
  // number clear of it.
  int bw = 130;
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(132, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(132, y + 2, fw, 11, 3, color);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(272, y);
  gfx->print(num);
  if (iv != IV_NONE) {
    char b[10];
    snprintf(b, sizeof(b), T(S_IV_FMT), iv);
    // un IV perfecto se resalta: es el golpe de suerte que el jugador busca
    gfx->setTextColor(iv >= 31 ? UI_BAR_WARN : UI_TRACK);
    gfx->setCursor(344, y);
    gfx->print(b);
  }
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(4);
  gfx->setCursor(x + 17, y + 15);
  gfx->print(l);
}

// pildoras de idioma centradas en y; rellena la activa
#define LANG_PILL_Y 296
#define LANG_PILL_H 30
#define LANG_PILL_X 336          // pildora de idioma (cicla los 6 al tocar)
#define LANG_PILL_W 96
// the volume mixer sits in the gap between the sound switch and the language
// pill: minus, the level, plus
#define VOL_MINUS_X 146
#define VOL_PLUS_X 276
#define VOL_BTN_W 48
static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT" };

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_SET_TIME)) * 9, 44);
  gfx->print(T(S_SET_TIME));

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextSize(7);
  gfx->setCursor(CX - 105, 108);
  gfx->print(t);

  drawClockBtn(104, 190, "-");  // hora -
  drawClockBtn(170, 190, "+");  // hora +
  drawClockBtn(252, 190, "-");  // min -
  drawClockBtn(318, 190, "+");  // min +
  gfx->setTextSize(2);
  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(120, 256);
  gfx->print(T(S_HOUR));
  gfx->setCursor(276, 256);
  gfx->print(T(S_MIN));

  // interruptor de sonido (izquierda de la fila de idioma)
  bool snd = audioEnabled();
  const char *sl = snd ? T(S_SND_ON) : T(S_SND_OFF);
  gfx->fillRoundRect(34, LANG_PILL_Y, 96, LANG_PILL_H, 8, snd ? UI_BAR_OK : UI_WHITE);
  gfx->drawRoundRect(34, LANG_PILL_Y, 96, LANG_PILL_H, 8, UI_INK);
  gfx->setTextColor(snd ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(34 + (96 - (int)strlen(sl) * 12) / 2, LANG_PILL_Y + 8);
  gfx->print(sl);

  // volume: a level, not a toggle. The sound switch beside it is still the
  // master -- this is how loud it is when it is on, and 0 is silent.
  {
    uint8_t v = audioVolume();
    for (int i = 0; i < 2; i++) {
      int bx = i ? VOL_PLUS_X : VOL_MINUS_X;
      bool live = i ? (v < 10) : (v > 0);
      gfx->fillRoundRect(bx, LANG_PILL_Y, VOL_BTN_W, LANG_PILL_H, 8,
                         live ? UI_WHITE : UI_TRACK);
      gfx->drawRoundRect(bx, LANG_PILL_Y, VOL_BTN_W, LANG_PILL_H, 8, UI_INK);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(2);
      gfx->setCursor(bx + VOL_BTN_W / 2 - 6, LANG_PILL_Y + 8);
      gfx->print(i ? "+" : "-");
    }
    char vl[12];
    snprintf(vl, sizeof(vl), T(S_VOL_FMT), v);
    gfx->setTextColor(v ? UI_INK : UI_TRACK);
    gfx->setTextSize(1);
    gfx->setCursor(210 + (56 - (int)strlen(vl) * 6) / 2, LANG_PILL_Y + 4);
    gfx->print(vl);
    // a small bar under the number, so the level reads at a glance
    gfx->fillRoundRect(210, LANG_PILL_Y + 18, 56, 8, 3, UI_TRACK);
    if (v) gfx->fillRoundRect(210, LANG_PILL_Y + 18, 56 * v / 10, 8, 3, UI_BAR_OK);
  }

  // selector de idioma: una pildora que cicla los 6 idiomas al tocar
  gfx->fillRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_WHITE);
  gfx->drawRoundRect(LANG_PILL_X, LANG_PILL_Y, LANG_PILL_W, LANG_PILL_H, 8, UI_INK);
  char lp[10];
  snprintf(lp, sizeof(lp), "%s >", LANG_CODES[gLang]);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(LANG_PILL_X + (LANG_PILL_W - (int)strlen(lp) * 12) / 2, LANG_PILL_Y + 8);
  gfx->print(lp);

  gfx->fillRoundRect(133, 340, 200, 48, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  gfx->setTextSize(3);
  gfx->setCursor(CX - 18, 352);
  gfx->print("OK");

  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_CLOCK_CANCEL)) * 6, 410);
  gfx->print(T(S_CLOCK_CANCEL));

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaPoke v%s", FW_VERSION);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(ver) * 3, 436);
  gfx->print(ver);
  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  if (y >= 190 && y <= 248) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (y >= LANG_PILL_Y && y <= LANG_PILL_Y + LANG_PILL_H) {
    if (x >= 34 && x < 130) {                  // interruptor de sonido
      audioSetEnabled(!audioEnabled());
      if (audioEnabled()) sfxPlay(SFX_TAP);    // confirma al encender
      return;
    }
    if (x >= VOL_MINUS_X && x < VOL_MINUS_X + VOL_BTN_W) {
      if (audioVolume() > 0) audioSetVolume(audioVolume() - 1);
      sfxPlay(SFX_TAP);                        // so the new level is audible
      return;
    }
    if (x >= VOL_PLUS_X && x < VOL_PLUS_X + VOL_BTN_W) {
      if (audioVolume() < 10) audioSetVolume(audioVolume() + 1);
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= LANG_PILL_X && x < LANG_PILL_X + LANG_PILL_W) {  // cicla idioma
      setLang((Lang)((gLang + 1) % LANG_COUNT));
      sfxPlay(SFX_TAP);
      return;
    }
  }
  if (y >= 340 && y <= 388 && x >= 133 && x <= 333) { applyClock(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x + 22, y + 2);
  gfx->print(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[20];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(l1) * 9, 176);
  gfx->print(l1);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(l2) * 6, 212);
  gfx->print(l2);
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  gfx->setTextSize(2);
  gfx->setCursor(x + (100 - (int)strlen(medalLabel(i)) * 12) / 2, y + 5);
  gfx->print(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : d.name;
  char head[26];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  int hlen = strlen(head);
  int hts = (hlen <= 11) ? 3 : 2;
  gfx->setTextSize(hts);
  gfx->setCursor(CX - hlen * (hts == 3 ? 9 : 6), hts == 3 ? 34 : 40);
  gfx->print(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    gfx->setTextColor(UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (strlen(d.name) + 2) * 6, 64);
    gfx->printf("(%s)", d.name);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[30];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(sx + 24, sy + 2);
  gfx->print(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e), IV_NONE);

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[40];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(info) * 6, 296);
  gfx->print(info);

  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(CX - strlen(T(S_RENAME_HINT)) * 6, 332);
  gfx->print(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BATTLE)) * 9, 44);
  gfx->print(T(S_BATTLE));

  // typing, in the accent colour of the species (English in every language,
  // same as the species names themselves)
  const DexEntry &de = DEX_TBL[pet.speciesId];
  char ty[24];
  if (de.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(de.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(de.type1), typeName(de.type2));
  gfx->setTextColor(de.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(ty) * 6, 76);
  gfx->print(ty);

  // 360 de tope de barra: a nivel 73 (fin de ciclo) el stat mas alto de toda
  // la dex es la vitalidad de CHANSEY (355). El 260 anterior ya se desbordaba.
  drawCardStat(104, T(S_STAT_ATK), pet.atkStat(), 360, UI_BAR_BAD, pet.ivAtk);
  drawCardStat(144, T(S_STAT_DEF), pet.defStat(), 360, 0x4C98, pet.ivDef);
  drawCardStat(184, T(S_STAT_SPE), pet.speStat(), 360, UI_BAR_WARN, pet.ivSpe);
  drawCardStat(224, T(S_STAT_VIT), pet.vitStat(), 360, UI_BAR_OK, pet.ivHp);
  drawCardStat(264, T(S_STAT_WGT), pet.weight, 100, 0xB3C8, IV_NONE);

}

// Draws one move as a row: name, its type in the type's own colour, and either
// power or a STATUS marker. Shared by the moves page and the picker so a move
// looks the same wherever you meet it.
void drawMoveRow(int y, uint8_t mv, bool highlight, int16_t dex) {
  gfx->fillRoundRect(70, y, 326, 50, 12, highlight ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawRoundRect(70, y, 326, 50, 12, UI_INK);
  if (!mv) {
    gfx->setTextColor(UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(T(S_MOVE_EMPTY)) * 6, y + 17);
    gfx->print(T(S_MOVE_EMPTY));
    return;
  }
  const MoveEntry &m = MOVE_TBL[mv];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(82, y + 8);
  gfx->print(m.name);
  // There is no per-type palette (DexEntry.accent is per species), and inventing
  // one by hand would duplicate what gen_dex.py generates. Colouring same-type
  // moves in the species accent is more useful anyway: STAB is a 1.5x damage
  // bonus, so this marks the moves that actually hit hardest for this creature.
  bool stab = hasStab(dex, m.type) && m.cat != MC_STATUS;
  gfx->setTextColor(stab ? DEX_TBL[dex].accent : UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(82, y + 32);
  gfx->print(typeName(m.type));
  char pw[16];
  if (m.cat == MC_STATUS) snprintf(pw, sizeof(pw), "%s", T(S_MOVE_STATUS));
  else snprintf(pw, sizeof(pw), T(S_MOVE_PWR), m.power);
  gfx->setTextColor(UI_INK);
  gfx->setCursor(384 - (int)strlen(pw) * 6, y + 32);
  gfx->print(pw);
}

// card page 4: the four known moves. Tapping a slot opens the picker.
void renderCardMoves() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_MOVES)) * 9, 44);
  gfx->print(T(S_MOVES));
  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(MOVE_ROW_Y(i), pet.moves[i], false, pet.speciesId);
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_MOVE_TAP)) * 3, 340);
  gfx->print(T(S_MOVE_TAP));
}

// Every move the species can learn by this level, so a slot can be swapped for
// anything legal -- not just the handful a level-up would have offered.
uint8_t learnableFor(int16_t dex, uint8_t lvl, uint8_t *out, uint8_t max) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  uint8_t n = learnCount(dex), w = 0;
  for (uint8_t i = 0; i < n && w < max; i++) {
    if (learnLevel(dex, i) > lvl) continue;
    uint8_t mv = learnMove(dex, i);
    if (!mv || mv >= MOVE_COUNT) continue;
    bool dup = false;
    for (uint8_t j = 0; j < w; j++)
      if (out[j] == mv) { dup = true; break; }
    if (!dup) out[w++] = mv;
  }
  return w;
}

// The picker targets either the live pet or a banked member. A banked one keeps
// its frozen level, so it can only relearn what it could have known back then.
uint8_t learnableList(uint8_t *out, uint8_t max) {
  if (movePickParty) {
    const PartyMon &m = party.slots[movePickParty - 1];
    return learnableFor(m.dex, (uint8_t)m.level, out, max);
  }
  return pet.isEgg() ? 0 : learnableFor(pet.speciesId, pet.level(), out, max);
}

uint8_t *pickTargetMoves() {
  return movePickParty ? party.slots[movePickParty - 1].moves : pet.moves;
}
int16_t pickTargetDex() {
  return movePickParty ? party.slots[movePickParty - 1].dex : pet.speciesId;
}

void renderMovePick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_MOVE_PICK)) * 6, 40);
  gfx->print(T(S_MOVE_PICK));

  uint8_t all[64];
  uint8_t n = learnableList(all, sizeof(all));
  uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
  if (movePickPage >= pages) movePickPage = 0;
  for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
    uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
    if (idx >= n) break;
    // the move already in this slot is highlighted, so replacing like for like
    // is obvious rather than a guess
    drawMoveRow(MOVE_PICK_Y(i), all[idx], all[idx] == pickTargetMoves()[movePickSlot], pickTargetDex());
  }
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    if (i == movePickPage) gfx->fillCircle(CX - (pages - 1) * 13 + i * 26, 380, 5, UI_INK);
    else gfx->drawCircle(CX - (pages - 1) * 13 + i * 26, 380, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 402);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- battle ----------

// Draws a battle backdrop scaled 2x. Emitted as runs of identical indices
// rather than a write per pixel: this is flat pixel art with long horizontal
// runs, and 240x112 at 2x would otherwise be 26,880 fillRect calls a frame.
// The round bezel crops the overhang physically, so nothing is clipped here.
static void drawBack(const BackScene &b, int y0) {
  const int SC = 2;
  int x0 = CX - (b.w * SC) / 2;
  for (int r = 0; r < b.h; r++) {
    const uint8_t *row = b.idx + (uint32_t)r * b.w;
    int c = 0;
    while (c < b.w) {
      uint8_t v = row[c];
      int run = 1;
      while (c + run < b.w && row[c + run] == v) run++;
      uint16_t col = b.pal[v];
      gfx->fillRect(x0 + c * SC, y0 + r * SC, run * SC, SC, col);
      c += run;
    }
  }
}

// Which scene: the FOE's biome, since a battle happens where it lives, and the
// same day/night split the main screen already uses.
static void drawBattleBack() {
  int16_t dex = btlFoe.dex;
  if (dex < 1 || dex > DEX_COUNT) { gfx->fillCircle(CX, CY, 231, UI_BG_DAY); return; }
  uint8_t bi = DEX_TBL[dex].biome;
  if (bi >= BACK_BIOMES) bi = 0;
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  drawBack(BACKS[bi][night ? 1 : 0], 30);
}

// Streams a side's sprite if it is not already the one loaded. Called whenever
// a creature steps in, never per frame.
static void btlSyncSprite(uint8_t who, const Combatant &c) {
  int16_t key = c.dex * (c.shiny ? -1 : 1);
  if (btlPmdDex[who] == key && btlPmd[who].loaded) return;
  btlPmd[who].unload();
  btlPmdDex[who] = 0;
  if (c.dex < 1 || c.dex > DEX_COUNT) return;
  if (btlPmd[who].load((uint8_t)c.dex, c.shiny)) btlPmdDex[who] = key;
}

static void btlFreeSprites() {
  for (int i = 0; i < 2; i++) { btlPmd[i].unload(); btlPmdDex[i] = 0; }
}

static void btlSay(const char *fmt, ...) {
  if (btlMsgCount >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(btlMsg[btlMsgCount], sizeof(btlMsg[0]), fmt, ap);
  va_end(ap);
  btlMsgCount++;
}

// Turns a TurnLog into narration. Everything here was already decided by the
// engine -- nothing is recomputed, so the text can never disagree with the maths.
// Picks the cue for an action from the TurnLog, so the sound can never
// disagree with what actually happened.
static void btlSfxFor(const TurnLog &lg) {
  if (lg.targetFainted) { sfxPlay(SFX_FAINT); return; }
  if (lg.inflicted) { sfxPlay(SFX_STATUS); return; }
  if (lg.damage && lg.effPct > 100) { sfxPlay(SFX_SUPER); return; }
  if (lg.damage) {
    sfxPlay(lg.move && MOVE_TBL[lg.move].cat == MC_SPEC ? SFX_BEAM : SFX_HIT);
    return;
  }
  if (lg.move && MOVE_TBL[lg.move].cat == MC_STATUS && !lg.missed) sfxPlay(SFX_STATUS);
}

static void btlNarrate(const Combatant &actor, const Combatant &target, const TurnLog &lg) {
  if (lg.skipped) return;
  btlSfxFor(lg);
  if (lg.hurtSelf) { btlSay(T(S_BTL_HURTSELF)); return; }
  if (lg.charged) { btlSay(T(S_BTL_USED), actor.name, MOVE_TBL[lg.move].name); return; }
  if (lg.move) btlSay(T(S_BTL_USED), actor.name, MOVE_TBL[lg.move].name);
  if (lg.missed) { btlSay(T(S_BTL_MISS), actor.name); return; }
  if (lg.immune) { btlSay(T(S_BTL_IMMUNE)); return; }
  if (lg.crit) btlSay(T(S_BTL_CRIT));
  if (lg.damage && lg.effPct > 100) btlSay(T(S_BTL_SUPER));
  else if (lg.damage && lg.effPct < 100) btlSay(T(S_BTL_WEAK));
  if (lg.inflicted) {
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    if (lg.inflicted < 7) btlSay(T(S_BTL_STATUS), target.name, T(AIL_STR[lg.inflicted]));
  }
  if (lg.targetFainted) btlSay(T(S_BTL_FAINT), target.name);
}

// Builds one opponent through Pet, so it gets the same stat formula and the
// same learnset-driven moveset the player's creatures do.
static void foeFromSpecies(Combatant &c, int16_t dex, uint8_t lvl, uint8_t iv) {
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = iv;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(c, foe);
}

// Your side: the live pet first, then the banked party.
//
// Both ladders cap your LEVEL to the leader's best, so a gym is always fought
// on its own terms and grinding is never the answer -- the type chart, the
// movesets and the choices are. Hard additionally caps your team SIZE to the
// leader's, so Brock is two-on-two. The caps are applied while BUILDING the
// combatants, so nothing is ever written back to the stored creature, exactly
// like ailments.
static void buildSquad(uint8_t maxLvl, uint8_t maxCount, uint16_t mask) {
  btlSquadN = 0;
  btlSquadAt = 0;
  btlPetIn = false;
  if (maxCount > TRAINER_TEAM_MAX) maxCount = TRAINER_TEAM_MAX;
  if (!pet.isEgg() && btlSquadN < maxCount && (mask & 1)) {
    Pet tmp = pet;                       // a copy: the real pet is untouched
    if (maxLvl && tmp.level() > maxLvl)
      tmp.ageMinutes = (uint32_t)(maxLvl - 1) * MINUTES_PER_LEVEL;
    combatantFromPet(btlSquad[btlSquadN++], tmp);
    btlPetIn = true;      // the training reward goes to whoever fought for it
  }
  for (int i = 0; i < PARTY_SLOTS && btlSquadN < maxCount; i++) {
    if (party.slots[i].empty() || !(mask & (1 << (i + 1)))) continue;
    PartyMon m = party.slots[i];
    if (maxLvl && m.level > maxLvl) m.level = maxLvl;
    combatantFromParty(btlSquad[btlSquadN++], m);
  }
  if (btlSquadN) btlYou = btlSquad[0];
}

// How many you may bring: the leader's own count in hard mode, six otherwise.
uint8_t squadCap(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT) return TRAINER_TEAM_MAX;
  return hard ? TRAINERS[idx].count : TRAINER_TEAM_MAX;
}

// A fight against another device. The squads are already exchanged; the host
// owns resolution and the guest renders what it is sent.
void startLinkBattle() {
  if (!lan.mineN || !lan.theirsN) return;
  // Rebuilt from lan.mine, NOT from squadMask. What we fight with has to be
  // exactly what the peer was told we have -- rebuilding from the party would
  // silently diverge if anything changed between offering and starting.
  btlSquadN = 0;
  btlSquadAt = 0;
  for (uint8_t i = 0; i < lan.mineN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlSquad[btlSquadN++], lan.mine[i]);
  if (!btlSquadN) return;
  btlYou = btlSquad[0];
  btlLink = true;
  btlLinkHost = lan.isHost;
  btlTrainer = -1;
  btlHard = false;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  for (uint8_t i = 0; i < lan.theirsN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlFoeSquad[btlFoeSquadN++], lan.theirs[i]);
  btlFoe = btlFoeSquad[0];
  btlMyAct = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  battleOpen = true;
}

void startTrainerBattle(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT || pet.isEgg() || pet.ceremony != CER_NONE) return;
  const Trainer &tr = TRAINERS[idx];
  uint8_t top = 0;
  for (int k = 0; k < tr.count; k++)
    if (tr.team[k].level > top) top = tr.team[k].level;
  // BOTH ladders cap your level to the leader's best. Without it a L73 team
  // walks every trainer at 100% and the type chart never matters. Hard adds the
  // size cap on top, plus a smarter AI and better opposing IVs.
  buildSquad(top, hard ? tr.count : TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return;
  btlTrainer = (int8_t)idx;
  btlHard = hard;
  btlFoeAt = 0;
  const Trainer &t = TRAINERS[idx];
  foeFromSpecies(btlFoe, t.team[0].dex, t.team[0].level, hard ? HARD_IV : EASY_IV);
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

void startBattle(int16_t dex, uint8_t lvl) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return;
  if (dex < 1 || dex > DEX_COUNT) return;
  buildSquad(0, TRAINER_TEAM_MAX, 0xFFFF);
  if (!btlSquadN) return;
  // The opponent is built through Pet so it gets the same stat formula and the
  // same learnset-driven moveset the player's creature does -- no special-cased
  // "enemy" maths that could quietly diverge.
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = 20;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

// The guest's whole turn: copy in what the host resolved and play the same
// animations the host is playing. It runs no battle logic at all -- that is the
// entire point of one side being authoritative (see link.h).
static void btlApplyResult() {
  if (lan.resultN < sizeof(LinkResult)) { lan.resultNew = false; return; }
  LinkResult r;
  memcpy(&r, lan.result, sizeof(r));
  lan.resultNew = false;

  // The wire says "host"/"guest"; here we are always the guest, so their fields
  // are the foe's and ours are ours.
  uint32_t now = millis();
  if (r.guestIdx < btlSquadN && r.guestIdx != btlSquadAt) {
    btlSquad[btlSquadAt] = btlYou;
    btlSquadAt = r.guestIdx;
    btlYou = btlSquad[btlSquadAt];
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
  }
  if (r.hostIdx != btlFoeAt && r.hostIdx < lan.theirsN) {
    btlFoeAt = r.hostIdx;
    linkMonTo(btlFoe, lan.theirs[btlFoeAt]);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
  }
  btlYou.hp = r.guestHp > btlYou.maxHp ? btlYou.maxHp : r.guestHp;
  btlFoe.hp = r.hostHp > btlFoe.maxHp ? btlFoe.maxHp : r.hostHp;
  btlYou.ailment = r.guestAil;
  btlFoe.ailment = r.hostAil;

  btlMsgCount = 0;
  if (r.hostMove) btlSay(T(S_BTL_USED), btlFoe.name, MOVE_TBL[r.hostMove].name);
  if (r.guestMove) btlSay(T(S_BTL_USED), btlYou.name, MOVE_TBL[r.guestMove].name);
  if (r.guestDmg) { btlHitUntil[0] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (r.hostDmg) { btlHitUntil[1] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (btlYou.fainted()) {
    btlFaintUntil[0] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), btlYou.name);
  }
  if (btlFoe.fainted()) {
    btlFaintUntil[1] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), btlFoe.name);
  }
}

// Radio packets land on another task, so the guest picks them up here, once a
// frame, rather than rendering from inside an interrupt.
static void btlLinkPoll() {
  if (!btlLink) return;

  // A peer that stopped answering. Ending the fight is the only honest thing to
  // do -- there is no result coming, and pretending otherwise is the hang this
  // whole layer exists to remove.
  if (!lan.live() && !btlOver) {
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlMsgCount = 0;
    btlSay("%s", T(S_LAN_GONE));
    return;
  }

  if (btlLinkHost) {
    // Our own action was latched when it was tapped; theirs arrives whenever
    // the radio manages it. Whichever is second sets the turn going.
    if (btlMyAct && lan.hasPeerAct() && !btlOver && !btlMsgCount &&
        btlSwapWho < 0) {
      uint8_t act = btlMyAct;
      btlMyAct = 0;
      if (LINK_ACT_IS_SWITCH(act)) btlSwitchTo(LINK_ACT_SLOT(act));
      else btlResolve(btlYou.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS]);
    }
    return;
  }

  if (lan.resultNew) btlApplyResult();
  if (lan.state == LINK_DONE && !btlOver) {
    btlOver = true;
    btlWon = lan.youWon;
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE));
  }
}

// Packs the outcome for the guest. Only the host ever calls this.
static void btlShipResult(uint8_t yourMove, uint8_t theirMove,
                          uint16_t hp0You, uint16_t hp0Foe) {
  LinkResult r = {};
  r.hostHp = btlYou.hp;   r.guestHp = btlFoe.hp;
  r.hostAil = btlYou.ailment; r.guestAil = btlFoe.ailment;
  r.hostMove = yourMove;  r.guestMove = theirMove;
  r.hostDmg = (hp0You > btlYou.hp) ? hp0You - btlYou.hp : 0;
  r.guestDmg = (hp0Foe > btlFoe.hp) ? hp0Foe - btlFoe.hp : 0;
  r.hostIdx = btlSquadAt; r.guestIdx = btlFoeAt;
  if (btlYou.fainted() || btlFoe.fainted()) r.flags |= 0x04;
  lan.sendResult((const uint8_t *)&r, (uint8_t)sizeof(r));
}

// One exchange: both sides act in speed order, then burn/poison chip.
static void btlResolve(uint8_t yourMove) {
  TurnLog lg;
  // Against another device the opponent's move comes off the wire, never from
  // the AI -- and the host is the only side that runs this at all.
  uint8_t foeMove;
  bool foeSwitched = false;
  uint32_t now = millis();
  if (btlLink) {
    // Off the wire, never from the AI. A switch is carried in the same message
    // as a move, and like our own switch it costs the turn: they change, we act.
    uint8_t act = lan.pendingAct;
    if (LINK_ACT_IS_SWITCH(act)) {
      uint8_t to = LINK_ACT_SLOT(act);
      if (to < btlFoeSquadN && to != btlFoeAt && !btlFoeSquad[to].fainted()) {
        btlFoeSquad[btlFoeAt] = btlFoe;     // remember how battered it was
        btlFoeAt = to;
        btlFoe = btlFoeSquad[to];
        btlHpShown[1] = btlFoe.hp;
        btlSyncSprite(1, btlFoe);
        btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
        btlEnterUntil[1] = now + BTL_ENTER_MS;
        btlSay(T(S_BTL_SENDS), lan.peerName, btlFoe.name);
        foeSwitched = true;
      }
      foeMove = 0;
    } else {
      foeMove = btlFoe.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS];
    }
    lan.pendingAct = 0;
  } else {
    foeMove = aiChooseMove(btlFoe, btlYou, btlHard);
  }
  (void)foeSwitched;

  bool youFirst = battleMovesFirst(btlYou, yourMove, btlFoe, foeMove);
  Combatant *a = youFirst ? &btlYou : &btlFoe;
  Combatant *b = youFirst ? &btlFoe : &btlYou;
  uint8_t ma = youFirst ? yourMove : foeMove;
  uint8_t mb = youFirst ? foeMove : yourMove;

  uint16_t hp0You = btlYou.hp, hp0Foe = btlFoe.hp;
  battleAct(*a, *b, ma, lg);
  btlNarrate(*a, *b, lg);
  if (lg.damage && !lg.hurtSelf) btlLungeUntil[a == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS;
  if (!b->fainted()) {
    battleAct(*b, *a, mb, lg);
    btlNarrate(*b, *a, lg);
    if (lg.damage && !lg.hurtSelf)
      btlLungeUntil[b == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS + BTL_LUNGE_MS;
  }
  // whoever actually lost health flinches, whichever side dealt it
  if (btlYou.hp < hp0You) btlHitUntil[0] = now + BTL_HIT_MS;
  if (btlFoe.hp < hp0Foe) btlHitUntil[1] = now + BTL_HIT_MS;
  if (btlLink && btlLinkHost) btlShipResult(yourMove, foeMove, hp0You, hp0Foe);
  if (!btlYou.fainted() && !btlFoe.fainted()) {
    battleEndTurn(btlYou, lg);
    if (lg.damage) btlNarrate(btlYou, btlYou, lg);
    battleEndTurn(btlFoe, lg);
    if (lg.damage) btlNarrate(btlFoe, btlFoe, lg);
  }
  // Someone went down. The replacement is NOT swapped in here -- that made the
  // change instant and read as a jump cut. Flag it, let the sprite drop out of
  // frame, and swap when the player dismisses the message.
  if (btlFoe.fainted() && btlLink && btlFoeAt + 1 < btlFoeSquadN) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlFoe.fainted() && btlTrainer >= 0 && btlFoeAt + 1 < BTL_TRAINERS[btlTrainer].count) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlYou.fainted() && btlSquadAt + 1 < btlSquadN) {
    btlFaintUntil[0] = millis() + BTL_FAINT_MS;
    btlSwapWho = 0;
    return;
  }
  if (btlFoe.fainted() || btlYou.fainted()) {
    btlOver = true;
    btlWon = btlFoe.fainted();
    btlNewBadge = false;
    btlTrainGain = 0;
    if (btlWon && btlTrainer >= 0 && !pet.hasBadge(btlRegion, btlTrainer, btlHard)) {
      pet.winBadge(btlRegion, btlTrainer, btlHard);
      btlNewBadge = true;
    }
    // A badge and nothing else made the ladder a one-way checklist. A win now
    // trains the creature that fought for it -- so a leader you can already
    // beat is worth returning to. It goes to the LIVE pet only: banked members
    // are frozen at the level and training they were banked with, and battling
    // already costs the live pet energy, which is what rate-limits the grind
    // without needing a cooldown.
    if (btlWon && btlTrainer >= 0 && btlPetIn) {
      // Later leaders are worth more, and hard mode is worth roughly double.
      uint8_t amt = (btlHard ? 6 + random(5) : 3 + random(3)) + btlTrainer / 3;
      btlTrainGain = pet.rewardTraining(amt, btlTrainWhich);
    }
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    // Tell the peer before anything else: if we stop here without sending, the
    // other device sits on a battle that will never take another turn.
    if (btlLink && btlLinkHost) lan.sendEnd(btlWon);
    if (btlLink) { btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE)); return; }
    if (btlWon && btlTrainer >= 0) { btlWinUntil = millis() + 60000; return; }
    btlSay("%s", T(S_BTL_LOSE));
  }
}

static void btlHpBar(int x, int y, int w, const Combatant &c, uint16_t shown) {
  int fw = c.maxHp ? (w - 4) * shown / c.maxHp : 0;
  uint16_t col = (shown * 2 > c.maxHp) ? UI_BAR_OK
                 : (shown * 4 > c.maxHp) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(x, y, w, 14, 4, UI_TRACK);
  if (fw > 0) gfx->fillRoundRect(x + 2, y + 2, fw, 10, 3, col);
  gfx->drawRoundRect(x, y, w, 14, 4, UI_INK);
}

static void btlSide(int tx, int ty, int sx, int sy, const Combatant &c, uint8_t who) {
  // the scenes are busy, so the name and bar sit on their own plate rather
  // than fighting the artwork for contrast
  int ph = (who == 0) ? 54 : 40;
  gfx->fillRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_BG_DAY);
  gfx->drawRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_INK);
  char l[28];
  snprintf(l, sizeof(l), "%s Lv.%u", c.name, c.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(tx, ty);
  gfx->print(l);
  gfx->setTextColor(UI_BAR_WARN);
  gfx->setCursor(tx, ty + 14);
  gfx->print("HP");
  btlHpBar(tx + 18, ty + 12, 122, c, btlHpShown[who]);
  if (who == 0) {                 // your own numbers, as the games do
    char hp[16];
    snprintf(hp, sizeof(hp), "%u/%u", btlHpShown[who], c.maxHp);
    gfx->setTextColor(UI_INK);
    gfx->setCursor(tx + 140 - (int)strlen(hp) * 6, ty + 28);
    gfx->print(hp);
  }
  if (c.ailment != AIL_NONE) {   // a status is the thing you most need to see
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setCursor(tx + 18, ty + 28);
    gfx->print(T(AIL_STR[c.ailment]));
  }
  // a platform under each creature, so they stand in the scene rather than
  // floating over it
  uint32_t now = millis();
  int ox = 0, oy = 0;
  bool flash = false;
  if (now < btlFaintUntil[who]) {          // sinks out of frame as it faints
    uint32_t left = btlFaintUntil[who] - now;
    oy += (int)((BTL_FAINT_MS - left) * 70 / BTL_FAINT_MS);
  } else if (c.fainted() && btlSwapWho == (int8_t)who) {
    return;                                // gone, waiting to be replaced
  }
  if (now < btlEnterUntil[who]) {          // and the next one rises into place
    uint32_t left = btlEnterUntil[who] - now;
    oy += (int)(left * 70 / BTL_ENTER_MS);
  }
  if (now < btlLungeUntil[who]) {          // lean in, then back out
    uint32_t left = btlLungeUntil[who] - now;
    int amt = (int)(left > BTL_LUNGE_MS / 2 ? BTL_LUNGE_MS - left : left) * 22 / (BTL_LUNGE_MS / 2);
    ox = who == 0 ? amt : -amt;            // you lunge right, the foe lunges left
    oy = who == 0 ? -amt / 2 : amt / 2;
  }
  if (now < btlHitUntil[who]) {
    uint32_t left = btlHitUntil[who] - now;
    ox += ((left / 50) % 2) ? 5 : -5;      // jitter
  }

  // Real PMD playback when the sprite streamed: attack while lunging, hurt
  // while flinching, idle otherwise. `has()` guards every one, because not
  // every species ships every action -- falling through to idle, and to the
  // flat thumbnail if the sprite is missing entirely (no SD).
  if (btlPmd[who].loaded) {
    uint8_t act = PMD_IDLE;
    bool loop = true;
    uint32_t t = now;
    if (now < btlHitUntil[who] && btlPmd[who].has(PMD_HURT)) {
      act = PMD_HURT; loop = false; t = now - (btlHitUntil[who] - BTL_HIT_MS);
    } else if (now < btlLungeUntil[who] && btlPmd[who].has(PMD_ATTACK)) {
      act = PMD_ATTACK; loop = false; t = now - (btlLungeUntil[who] - BTL_LUNGE_MS);
    }
    drawPmdActM(btlPmd[who], act, sx + 24 + ox, sy + 78 + oy, t, loop, false, 4);
    return;
  }
  const uint8_t *th = thumbs.get(c.dex);
  if (!th) return;
  if (now < btlHitUntil[who]) flash = ((btlHitUntil[who] - now) / 60) % 2 == 0;
  drawThumb(th, sx + ox, sy + oy, 3, flash);
}

// Bars drain rather than snap: a hit that removes half your health should be
// visible as it happens, not as a value that was already different.
static void btlEaseBars() {
  const uint16_t real[2] = { btlYou.hp, btlFoe.hp };
  for (int i = 0; i < 2; i++) {
    int diff = (int)real[i] - (int)btlHpShown[i];
    if (!diff) continue;
    int step = diff / 5;
    if (!step) step = diff > 0 ? 1 : -1;
    btlHpShown[i] = (uint16_t)((int)btlHpShown[i] + step);
  }
}

// The moment the ladder builds toward. It used to be one more line in the same
// message box as "It's super effective!", with the badge awarded silently.
void renderWin() {
  const Trainer &t = BTL_TRAINERS[btlTrainer];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  gfx->setTextColor(UI_BAR_WARN);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BTL_WIN)) * 9, 54);
  gfx->print(T(S_BTL_WIN));

  char l[40];
  snprintf(l, sizeof(l), T(S_BTL_BEAT), t.name);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(l) * 6, 96);
  gfx->print(l);

  // the badge, large, with the hard-mode halo if that is how it was won
  if (btlTrainer < TRAINER_GYMS) {
    int by = 190;
    if (btlHard) {
      for (int r = 62; r >= 56; r--) gfx->drawCircle(CX, by, r, r % 2 ? 0xFEA0 : 0xFF60);
    }
    const BadgeArt &a = BADGES_ART[btlRegion % BADGE_REGIONS][btlTrainer];
    for (int r = 0; r < BADGE_PX; r++)
      for (int c = 0; c < BADGE_PX; c++) {
        uint8_t v = a.idx[r * BADGE_PX + c];
        if (v == 0xFF) continue;
        // 3x, so it reads as a prize rather than a list entry
        gfx->fillRect(CX - BADGE_PX * 3 / 2 + c * 3, by - BADGE_PX * 3 / 2 + r * 3,
                      3, 3, a.pal[v]);
      }
    if (btlNewBadge) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(T(S_BTL_NEWBADGE)) * 6, 286);
      gfx->print(T(S_BTL_NEWBADGE));
    }
  }
  snprintf(l, sizeof(l), T(S_BADGES_FMT), pet.badgeCountIn(btlRegion, btlHard));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(l) * 6, 316);
  gfx->print(l);

  // what the win was worth beyond the badge
  if (btlTrainGain) {
    static const StrId NAMES[3] = { S_TR_ATK, S_TR_DEF, S_TR_SPE };
    snprintf(l, sizeof(l), T(S_WIN_TRAIN_FMT),
             T(NAMES[btlTrainWhich % 3]), btlTrainGain);
    gfx->setTextColor(UI_BAR_OK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(l) * 6, 344);
    gfx->print(l);
  } else if (btlPetIn && btlTrainer >= 0) {
    gfx->setTextColor(UI_TRACK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(T(S_WIN_MAXED)) * 3, 348);
    gfx->print(T(S_WIN_MAXED));
  }

  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 380);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void renderBattle() {
  if (btlWinUntil) { renderWin(); return; }
  btlEaseBars();
  gfx->fillScreen(RGB565_BLACK);
  drawBattleBack();
  // the lower band stays flat so the move grid and the HP text keep their
  // contrast against it
  gfx->fillRect(0, 254, 466, 212, UI_BG_DAY);

  // x=82 not 58: at y=60 the round bezel starts around x=77, and a longer
  // name like BLASTOISE was losing its first characters off the edge
  btlSide(82, 82, 300, 40, btlFoe, 1);    // foe reads top-left, sprite top-right
  btlSide(250, 190, 76, 168, btlYou, 0);  // you read bottom-right, sprite bottom-left

  // Waiting on the other device. Without this the screen is identical to the
  // one where it is your turn, so a tap that has been sent and a tap that was
  // never registered look exactly the same.
  bool lanWait = btlLink && !btlOver &&
                 (btlLinkHost ? (btlMyAct && !lan.hasPeerAct())
                              : (lan.state == LINK_WAITING));
  if (lanWait && !btlMsgCount) {
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_INK);
    gfx->setTextColor(UI_TRACK);
    gfx->setTextSize(1);
    const char *w = T(S_LAN_WAITFOE);
    gfx->setCursor(CX - (int)strlen(w) * 3, BTL_GRID_Y + 40);
    gfx->print(w);
  } else if (btlMsgCount) {            // narration takes over the menu area
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_CELL_H * 2 + 8, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    for (uint8_t i = 0; i < btlMsgCount && i < 4; i++) {
      gfx->setCursor(CX - (int)strlen(btlMsg[i]) * 3, BTL_GRID_Y + 14 + i * 18);
      gfx->print(btlMsg[i]);
    }
    gfx->setTextColor(UI_TRACK);
    gfx->setCursor(CX - 30, BTL_GRID_Y + 84);
    gfx->print("tap...");
  } else if (btlMenu == 0) {
    // FIGHT or POKEMON, the mainline's own first choice
    const char *lab[2] = { T(S_FIGHT), T(S_BTL_SWITCH) };
    for (int i = 0; i < 2; i++) {
      int x = BTL_GRID_X, y = BTL_GRID_Y + i * (BTL_CELL_H + 8);
      gfx->fillRoundRect(x, y, 328, BTL_CELL_H, 10, i ? UI_TRACK : UI_BG_DAY);
      gfx->drawRoundRect(x, y, 328, BTL_CELL_H, 10, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lab[i]) * 6, y + 14);
      gfx->print(lab[i]);
    }
  } else if (btlMenu == 2) {
    // who to bring on instead; the current one and anything fainted is inert
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      bool usable = (i != btlSquadAt) && !m.fainted();
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(1);
      gfx->setCursor(x + 10, y + 10);
      gfx->print(m.name);
      char hp[20];
      snprintf(hp, sizeof(hp), "%u/%u", m.hp, m.maxHp);
      gfx->setCursor(x + 10, y + 28);
      gfx->print(hp);
    }
  } else {
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      uint8_t mv = btlYou.moves[i];
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, mv ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_INK);
      if (!mv) continue;
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(1);
      gfx->setCursor(x + 10, y + 12);
      gfx->print(MOVE_TBL[mv].name);
      gfx->setTextColor(hasStab(btlYou.dex, MOVE_TBL[mv].type) &&
                                MOVE_TBL[mv].cat != MC_STATUS
                            ? DEX_TBL[btlYou.dex].accent
                            : UI_TRACK);
      gfx->setCursor(x + 10, y + 28);
      gfx->print(typeName(MOVE_TBL[mv].type));
    }
  }
  gfx->flush();
}

// Brings on the flagged replacement and starts its entrance.
static void btlDoSwap() {
  uint32_t now = millis();
  if (btlSwapWho == 1 && btlLink) {
    btlFoeSquad[btlFoeAt] = btlFoe;
    // the next one still standing, not simply the next index
    uint8_t nxt = btlFoeAt;
    while (++nxt < btlFoeSquadN && btlFoeSquad[nxt].fainted()) {}
    if (nxt >= btlFoeSquadN) { btlSwapWho = -1; return; }
    btlFoeAt = nxt;
    btlFoe = btlFoeSquad[btlFoeAt];
    btlHpShown[1] = btlFoe.hp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), lan.peerName, btlFoe.name);
  } else if (btlSwapWho == 1) {
    const Trainer &t = BTL_TRAINERS[btlTrainer];
    btlFoeAt++;
    foeFromSpecies(btlFoe, t.team[btlFoeAt].dex, t.team[btlFoeAt].level,
                   btlHard ? HARD_IV : EASY_IV);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), t.name, btlFoe.name);
  } else if (btlSwapWho == 0) {
    btlSquad[btlSquadAt] = btlYou;     // remember how battered it was
    btlSquadAt++;
    btlYou = btlSquad[btlSquadAt];
    btlHpShown[0] = btlYou.hp;
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_GO), btlYou.name);
  }
  btlSwapWho = -1;
}

// Switching spends your turn: the opponent still acts. That is what stops it
// being a free look at the matchup every round.
static void btlSwitchTo(uint8_t i) {
  if (i >= btlSquadN || i == btlSquadAt) return;
  btlSquad[btlSquadAt] = btlYou;
  btlSquadAt = i;
  btlYou = btlSquad[i];
  btlHpShown[0] = btlYou.hp;
  btlSyncSprite(0, btlYou);
  btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
  btlEnterUntil[0] = millis() + BTL_ENTER_MS;
  btlMenu = 0;
  btlSay(T(S_BTL_GO), btlYou.name);
  btlResolve(0);          // move 0 = no attack, so only the foe acts
}

void battleTap(int16_t x, int16_t y) {
  if (btlWinUntil) {          // dismiss the win screen and leave the fight
    btlWinUntil = 0;
    btlFreeSprites();
    audioMusic(MUS_NONE);
    battleOpen = false;
    if (btlLink) { btlLink = false; lanOpen = true; }
    return;
  }
  if (btlMsgCount) {          // a tap clears the narration and returns the menu
    btlMsgCount = 0;
    if (btlOver) {
      btlFreeSprites();
      battleOpen = false;
      // Back to the LAN screen rather than all the way out: that is where a
      // rematch is offered, and re-pairing for every fight would be tedious.
      if (btlLink) { btlLink = false; lanOpen = true; }
      return;
    }
    if (btlSwapWho >= 0) btlDoSwap();   // the replacement arrives on this beat
    return;
  }
  if (btlMenu == 0) {
    for (int i = 0; i < 2; i++) {
      int cy = BTL_GRID_Y + i * (BTL_CELL_H + 8);
      if (x < BTL_GRID_X || x > BTL_GRID_X + 328 || y < cy || y > cy + BTL_CELL_H) continue;
      sfxPlay(SFX_TAP);
      btlMenu = i ? 2 : 1;
      return;
    }
    return;
  }
  if (btlMenu == 2) {
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      int cx = BTL_CELL_X(i), cy = BTL_CELL_Y(i);
      if (x < cx || x > cx + BTL_CELL_W || y < cy || y > cy + BTL_CELL_H) continue;
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      if (i == btlSquadAt || m.fainted()) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (btlLink && !btlLinkHost) {
        // The guest asks; it never switches on its own. A switch rides the same
        // message as a move, so the host spends the turn on it exactly as it
        // would for us.
        lan.sendAct(LINK_ACT_SWITCH_TO(i));
        btlMenu = 0;
        return;
      }
      if (btlLink) {            // host: latched like a move, see btlLinkPoll
        btlMyAct = LINK_ACT_SWITCH_TO(i);
        btlMenu = 0;
        if (!lan.hasPeerAct()) return;
        btlMyAct = 0;
      }
      btlSwitchTo(i);
      return;
    }
    btlMenu = 0;      // anywhere else backs out
    return;
  }
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!btlYou.moves[i]) continue;
    int cx = BTL_CELL_X(i), cy = BTL_CELL_Y(i);
    if (x < cx || x > cx + BTL_CELL_W || y < cy || y > cy + BTL_CELL_H) continue;
    sfxPlay(SFX_TAP);
    btlMenu = 0;
    if (btlLink && !btlLinkHost) {
      lan.sendAct(LINK_ACT_MOVE(i));   // the guest asks; the host decides
      return;
    }
    if (btlLink) {
      // Latched, not discarded. The host used to throw the tap away when the
      // rival had not chosen yet, so you had to keep jabbing at the move until
      // the timing happened to line up.
      btlMyAct = LINK_ACT_MOVE(i);
      if (!lan.hasPeerAct()) return;   // resolved by btlLinkPoll when it lands
      btlMyAct = 0;
    }
    btlResolve(btlYou.moves[i]);
    return;
  }
  btlMenu = 0;        // a tap off the grid goes back to FIGHT/POKEMON
}

// ---------- player card (swipe down) ----------
// Everything here is player-wide and outlives the creature: badges, the daily
// streak, the Pokedex and the party. No player sprite yet -- the SD carries
// PMD creature sprites only, so a trainer portrait needs new art.
// Page 1: who you are -- avatar, badges, totals. Tap the avatar to cycle it;
// the four sprites are hand-drawn (see species.h) because SpriteCollab has no
// trainer art and ripped sprites would be unlicensed.
static void renderPlayerBadges() {
  // the player's name if they have set one, the generic title if not; either
  // way tapping it opens the keyboard
  const char *tn = pet.trainerName[0] ? pet.trainerName : T(S_TRAINER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(tn) * 9, 40);
  gfx->print(tn);

  // Pages 1 and 2 are the other regions' ladders: name them, and drop the
  // avatar so the badges have the room. Only page 0 is "you".
  if (playerBadgeRegion != 0) {
    const char *rn = TRAINER_SETS[playerBadgeRegion].region;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(rn) * 6, 120);
    gfx->print(rn);
  } else if (gShowAllAvatars) {          // emulator only: every avatar at once
    for (uint8_t i = 0; i < AVATAR_COUNT; i++)
      drawAvatar(i, 60 + (i % 4) * 88, 60 + (i / 4) * 60, 3);
  } else {
    drawAvatar(pet.avatar, CX - AVATAR_PX * 2, 72, 4);
    // the sprite alone is not always obvious at 16x16, so it is named
    const char *an = AVATARS[pet.avatar % AVATAR_COUNT].name;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(an) * 3, 143);
    gfx->print(an);
    gfx->setTextColor(UI_TRACK);
    gfx->setCursor(CX - (int)strlen(T(S_AVATAR_HINT)) * 3, 154);
    gfx->print(T(S_AVATAR_HINT));
  }

  // The real badges, 2x4. Unearned ones draw as a faint outline so the shape
  // of what is missing is still visible.
  for (int i = 0; i < TRAINER_GYMS; i++) {
    int bx = 140 + (i % 4) * 62, by = 188 + (i / 4) * 62;
    bool got = pet.hasBadge(playerBadgeRegion, i, false);
    bool hard = pet.hasBadge(playerBadgeRegion, i, true);
    if (hard) {
      // Beaten on hard: a golden halo. Concentric rings, not a filled disc --
      // a disc sat behind the art and read as a gold coin rather than a glow.
      gfx->drawCircle(bx, by, 25, 0xFDE0);
      gfx->drawCircle(bx, by, 24, 0xFEA0);
      gfx->drawCircle(bx, by, 23, 0xFF60);
      gfx->drawCircle(bx, by, 22, 0xFEA0);
      gfx->drawCircle(bx, by, 21, 0xFDE0);
    }
    if (!got) {
      gfx->drawCircle(bx, by, 20, UI_TRACK);
      continue;
    }
    const BadgeArt &a = BADGES_ART[playerBadgeRegion % BADGE_REGIONS][i];
    for (int r = 0; r < BADGE_PX; r++)
      for (int c = 0; c < BADGE_PX; c++) {
        uint8_t v = a.idx[r * BADGE_PX + c];
        if (v == 0xFF) continue;
        gfx->fillRect(bx - BADGE_PX / 2 + c, by - BADGE_PX / 2 + r, 1, 1, a.pal[v]);
      }
  }
  char l[32];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  snprintf(l, sizeof(l), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setCursor(CX - (int)strlen(l) * 6, 286);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_POKEDEX_FMT), pet.registeredCount(), DEX_COUNT);
  gfx->setCursor(CX - (int)strlen(l) * 6, 312);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_PARTY_FMT), party.count());
  gfx->setCursor(CX - (int)strlen(l) * 6, 338);
  gfx->print(l);
}

// Page 2: the medals. They used to sit on the creature's card; they belong with
// the player, since totalMedals accumulates across every pet you raise.
static void renderPlayerMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 44);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 46 + (i % 2) * 190, y = 96 + (i / 2) * 58;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 180, 48, 10, g ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(x, y, 180, 48, 10, UI_INK);
    gfx->setTextColor(g ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(x + (180 - (int)strlen(medalLabel(i)) * 12) / 2, y + 6);
    gfx->print(medalLabel(i));
    gfx->setTextSize(1);
    gfx->setCursor(x + (180 - (int)strlen(medalDesc(i)) * 6) / 2, y + 30);
    gfx->print(medalDesc(i));
  }
  char tot[28];
  snprintf(tot, sizeof(tot), T(S_MEDALS_TOTAL_FMT), pet.totalMedals);
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(tot) * 3, 344);
  gfx->print(tot);
}

void renderPlayer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (playerPage < GYM_REGIONS) renderPlayerBadges();
  else renderPlayerMedals();

  for (uint8_t i = 0; i < PLAYER_PAGES; i++) {
    int dx = CX - (PLAYER_PAGES - 1) * 13 + i * 26;
    if (i == playerPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- reaction test (trains SPEED) ----------
// The third training verb. The bag is a masher and the ball is a juggler, so
// this one is a reaction: a target appears somewhere on the panel and you tap
// it before it expires. The window shrinks as you go, which is what makes it
// read as speed rather than as endurance.
#define SPD_MS 15000UL       // session length
#define SPD_LIFE0 1100       // first target's window, ms
#define SPD_LIFE_MIN 380
#define SPD_R 46             // target radius

void spdSpawn() {
  // Keep the whole target inside the bezel: pick an angle and a radius that
  // leave SPD_R of margin, rather than a square that clips at the corners.
  int ang = random(360);
  int rad = random(150);
  float a = ang * 3.14159f / 180.0f;
  spdX = CX + (int)(cosf(a) * rad);
  spdY = CY + (int)(sinf(a) * rad);
  spdBorn = millis();
}

void startSpeedGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  spdOpen = true;
  spdUntil = millis() + SPD_MS;
  spdOverUntil = 0;
  spdHits = 0;
  spdMisses = 0;
  spdGain = 0;
  spdNewHi = false;
  spdSpawn();
}

static uint16_t spdLife() {
  int life = SPD_LIFE0 - spdHits * 32;
  return life < SPD_LIFE_MIN ? SPD_LIFE_MIN : life;
}

void spdTap(int16_t x, int16_t y) {
  if (spdOverUntil) return;
  int dx = x - spdX, dy = y - spdY;
  if (dx * dx + dy * dy <= (SPD_R + 14) * (SPD_R + 14)) {
    spdHits++;
    sfxPlay(SFX_TAP);
    spdSpawn();
  }
}

void renderSpeed() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (spdOverUntil) {
    if (now > spdOverUntil) { spdOpen = false; return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_SCORE_FMT), spdHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[20];
    snprintf(g, sizeof(g), T(S_SPD_GAIN_FMT), spdGain);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (spdNewHi && spdHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[20];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.spdHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // session over?
  if (now >= spdUntil) {
    spdNewHi = (spdHits > pet.spdHi);
    spdGain = pet.trainSpeed(spdHits);
    sfxPlay(spdNewHi ? SFX_MEDAL : SFX_PLAY);
    spdOverUntil = now + 3500;
    gfx->flush();
    return;
  }
  // target expired?
  if (now - spdBorn > spdLife()) {
    spdMisses++;
    spdSpawn();
  }

  // the target, shrinking as its window runs out so the urgency is visible
  uint32_t age = now - spdBorn;
  int life = spdLife();
  int r = SPD_R - (int)((uint32_t)SPD_R * age / (life ? life : 1) / 2);
  if (r < 8) r = 8;
  gfx->fillCircle(spdX, spdY, r, UI_BAR_BAD);
  gfx->fillCircle(spdX, spdY, r * 2 / 3, UI_WHITE);
  gfx->fillCircle(spdX, spdY, r / 3, UI_BAR_BAD);

  char b[12];
  snprintf(b, sizeof(b), "%u", spdHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(b) * 12, 30);
  gfx->print(b);
  // seconds left
  uint32_t left = (spdUntil > now) ? (spdUntil - now + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%us", (unsigned)left);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(b) * 6, 76);
  gfx->print(b);
  gfx->flush();
}

// ---------- team select ----------
// Which creatures come to this fight. It exists because hard mode caps your
// team to the leader's size, so the difference between a sweep and a wipe is
// bringing the right type -- and the squad used to be simply whoever sat first
// in the party.

// candidate n: 0 = the live pet, 1..PARTY_SLOTS = banked members
bool pickExists(uint8_t n) {
  if (n == 0) return !pet.isEgg();
  return n <= PARTY_SLOTS && !party.slots[n - 1].empty();
}
uint8_t pickChosen() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n) && (squadMask & (1 << n))) c++;
  return c;
}
uint8_t pickCandidates() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n)) c++;
  return c;
}
// Trims the selection to the first `cap` candidates. The default used to be
// "everything", which with a live pet plus six banked is seven against a cap of
// six -- so the screen opened already invalid.
void pickDefault(uint8_t cap) {
  squadMask = 0;
  uint8_t taken = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS && taken < cap; n++)
    if (pickExists(n)) { squadMask |= (1 << n); taken++; }
}

static void drawPickCell(uint8_t n, int x, int y, uint8_t capLvl) {
  bool on = (squadMask & (1 << n)) != 0;
  int16_t dex; uint16_t lvl; const char *nm; bool shiny;
  if (n == 0) {
    dex = pet.speciesId; lvl = pet.level(); shiny = pet.shiny;
    nm = pet.nick[0] ? pet.nick : DEX_TBL[dex].name;
  } else {
    const PartyMon &m = party.slots[n - 1];
    dex = m.dex; lvl = m.level; shiny = m.shiny;
    nm = m.nick[0] ? m.nick : DEX_TBL[dex].name;
  }
  if (capLvl && lvl > capLvl) lvl = capLvl;   // show the level it will FIGHT at
  gfx->fillRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_BG_DAY : UI_TRACK);
  gfx->drawRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_INK : 0x8410);
  const uint8_t *th = thumbs.get(dex);
  if (th) drawThumb(th, x - 12, y - 6, 2, !on);
  gfx->setTextColor(on ? UI_INK : 0x8410);
  gfx->setTextSize(1);
  gfx->setCursor(x + 54, y + 14);
  gfx->print(nm);
  char l[16];
  snprintf(l, sizeof(l), "Lv.%u%s", (unsigned)lvl, shiny ? " *" : "");
  gfx->setCursor(x + 54, y + 30);
  gfx->print(l);
  // its typing is the whole reason you are on this screen
  const DexEntry &d = DEX_TBL[dex];
  gfx->setTextColor(on ? d.accent : 0x8410);
  gfx->setCursor(x + 54, y + 48);
  gfx->print(typeName(d.type1));
  if (d.type2 != T_NONE) {
    gfx->setCursor(x + 54, y + 60);
    gfx->print(typeName(d.type2));
  }
  if (on) {
    gfx->fillCircle(x + PICK_CELL_W - 16, y + 16, 9, UI_BAR_OK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(x + PICK_CELL_W - 19, y + 13);
    gfx->print("*");
  }
}

void renderPick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t cap = squadCap(pickTrainer, pickHard);
  uint8_t top = 0;          // the level cap shown on each cell; 0 = uncapped
  char head[40];
  if (pickTrainer == PICK_LAN) {
    snprintf(head, sizeof(head), "%s: %s", T(S_LAN),
             lanWantHost ? T(S_LAN_HOST) : T(S_LAN_JOIN));
  } else {
    const Trainer &t = TRAINERS[pickTrainer];
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    snprintf(head, sizeof(head), "%s  Lv.%u x%u", t.name, top, t.count);
  }
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 44);
  gfx->print(head);
  char sub[28];
  snprintf(sub, sizeof(sub), T(S_PICK_FMT), pickChosen(), cap);
  gfx->setTextSize(1);
  gfx->setTextColor(pickChosen() > cap ? UI_BAR_BAD : UI_TRACK);
  gfx->setCursor(CX - (int)strlen(sub) * 3, 68);
  gfx->print(sub);

  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    drawPickCell(n, PICK_X(drawn), PICK_Y(drawn), top);
    drawn++;
  }
  uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
  if (!pages) pages = 1;
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == pickPage) gfx->fillCircle(dx, 332, 5, UI_INK);
    else gfx->drawCircle(dx, 332, 4, UI_INK);
  }

  bool ok = pickChosen() > 0 && pickChosen() <= cap;
  gfx->fillRoundRect(140, PICK_GO_Y, 186, 40, 12, ok ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(140, PICK_GO_Y, 186, 40, 12, UI_INK);
  gfx->setTextColor(ok ? UI_BG_DAY : 0x8410);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_FIGHT)) * 6, PICK_GO_Y + 12);
  gfx->print(T(S_FIGHT));
  gfx->flush();
}

void pickTap(int16_t x, int16_t y) {
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + 40 && x >= 140 && x <= 326) {
    uint8_t cap = squadCap(pickTrainer, pickHard);
    if (pickChosen() == 0 || pickChosen() > cap) return;   // GO stays inert
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) {
      // The squad is chosen BEFORE the radio comes up, so what gets offered to
      // the peer is what the player picked -- lanOffer() builds lan.mine from
      // squadMask, and the fight is then rebuilt from lan.mine rather than from
      // the party (see startLinkBattle).
      lanOffer(lanWantHost);
      lanOpen = true;
      return;
    }
    startTrainerBattle(pickTrainer, pickHard);
    return;
  }
  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    int cx0 = PICK_X(drawn), cy0 = PICK_Y(drawn);
    drawn++;
    if (x < cx0 || x > cx0 + PICK_CELL_W || y < cy0 || y > cy0 + PICK_CELL_H) continue;
    squadMask ^= (1 << n);
    sfxPlay(SFX_TAP);
    return;
  }
}

// ---------- LAN battle ----------
// Pairing on a touch-only screen: one device hosts, the other joins, and the
// protocol does the rest. There is no MAC entry because there is no keyboard
// worth typing one on.
void renderLan() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_LAN)) * 6, 44);
  gfx->print(T(S_LAN));

  const char *msg = T(S_LAN_PICK);
  switch (lan.state) {
    case LINK_HANDSHAKE:
    case LINK_LISTENING: msg = T(S_LAN_WAIT); break;
    case LINK_SQUADS:    msg = T(S_LAN_WAIT); break;
    case LINK_READY:     msg = T(S_LAN_READY); break;
    case LINK_REFUSED:   msg = T(S_LAN_REFUSED); break;
    case LINK_LOST:      msg = T(S_LAN_GONE); break;
    case LINK_DONE:      msg = lan.youWon ? T(S_BTL_WIN) : T(S_BTL_LOSE); break;
    default: break;
  }
  gfx->setTextColor((lan.state == LINK_REFUSED || lan.state == LINK_LOST)
                      ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(msg) * 3, 76);
  gfx->print(msg);

  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    const char *lab[2] = { T(S_LAN_HOST), T(S_LAN_JOIN) };
    for (int i = 0; i < 2; i++) {
      int y = 120 + i * 70;
      gfx->fillRoundRect(90, y, 286, 56, 12, UI_BG_DAY);
      gfx->drawRoundRect(90, y, 286, 56, 12, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lab[i]) * 6, y + 20);
      gfx->print(lab[i]);
    }
  } else if (lan.state == LINK_READY) {
    char l[40];
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lan.peerName) * 6, 130);
      gfx->print(lan.peerName);
    }
    snprintf(l, sizeof(l), T(S_LAN_VS), lan.theirsN);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(l) * 6, 150);
    gfx->print(l);
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BAR_OK);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(CX - strlen(T(S_FIGHT)) * 6, 240);
    gfx->print(T(S_FIGHT));
  } else if (lan.state == LINK_DONE) {
    // Both squads are still in hand on both devices, so going again costs one
    // packet -- there is nothing to re-exchange.
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lan.peerName) * 6, 140);
      gfx->print(lan.peerName);
    }
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BG_DAY);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_LAN_REMATCH)) * 6, 240);
    gfx->print(T(S_LAN_REMATCH));
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// Hands our chosen squad to the link, then announces.
// Leaving deliberately: tell the peer so it reports at once instead of sitting
// out the timeout, then free the radio -- it costs real current.
void lanLeave() {
  if (lan.live()) lan.sendBye();
  linkNowEnd();
  lan.state = LINK_OFF;
}

static void lanOffer(bool host) {
  // The squad is built BEFORE the radio is touched. What we advertise has to be
  // exactly what the player just chose in the picker, and that does not depend
  // on whether the radio comes up -- doing it the other way round meant a
  // failed radio skipped the squad entirely and left nothing to inspect.
  lan.begin(host, pet.trainerName);
  snprintf(lan.peerName, sizeof(lan.peerName), "%s", pet.trainerName);
  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  for (uint8_t i = 0; i < btlSquadN; i++) {
    LinkMon m;
    linkMonFrom(m, btlSquad[i]);
    lan.addMon(m);
  }
  if (!linkNowBegin(&lan)) {          // no radio: say so rather than hanging
    lan.state = LINK_REFUSED;
    return;
  }
  // BOTH sides announce. Which of them ends up hosting is settled by id inside
  // the hello, so the buttons are only a preference -- two players who both tap
  // HOST still get a working fight instead of two authorities, and two who both
  // tap JOIN still get one instead of mutual silence.
  lan.start();
}

void lanTap(int16_t x, int16_t y) {
  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    for (int i = 0; i < 2; i++) {
      int by = 120 + i * 70;
      if (x < 90 || x > 376 || y < by || y > by + 56) continue;
      sfxPlay(SFX_TAP);
      lanWantHost = (i == 0);
      lanOpen = false;
      pickTrainer = PICK_LAN;
      pickHard = false;
      pickPage = 0;
      pickDefault(squadCap(PICK_LAN, false));
      pickOpen = true;
      return;
    }
  } else if (lan.state == LINK_READY) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lanOpen = false;
      startLinkBattle();
      return;
    }
  } else if (lan.state == LINK_DONE) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lan.sendRematch();      // both sides go back to READY and tap FIGHT
      return;
    }
  }
  if (y > 370) { lanLeave(); lanOpen = false; }   // back
}

// ---------- gym list ----------
void renderGyms() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // The ladder's own region in the title, since a vertical swipe moves between
  // three of them and "GYMS" alone would not say which you are looking at.
  char title[28];
  snprintf(title, sizeof(title), "%s %s", TRAINER_SETS[gymRegion % GYM_REGIONS].region,
           T(S_GYMS));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(title) * 6, 42);
  gfx->print(title);
  char sub[24];
  snprintf(sub, sizeof(sub), T(S_BADGES_FMT), pet.badgeCountIn(gymRegion, gymHard));
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(sub) * 3, 66);
  gfx->print(sub);
  // difficulty pill: hard caps YOUR team to the leader's size and level, so it
  // is a different ladder with its own badges rather than a damage multiplier
  const char *dif = T(gymHard ? S_HARD : S_EASY);
  int dw = (int)strlen(dif) * 12 + 24;
  gfx->fillRoundRect(CX - dw / 2, 76, dw, 24, 10, gymHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(gymHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(dif) * 6, 81);
  gfx->print(dif);

  for (int i = 0; i < GYM_ROWS; i++) {
    uint8_t idx = gymPage * GYM_ROWS + i;
    if (idx >= TRAINER_COUNT) break;
    const Trainer &t = TRAINERS[idx];
    int y = GYM_ROW_Y(i);
    bool done = pet.hasBadge(gymRegion, idx, gymHard);
    bool open_ = gymUnlocked(idx, gymHard);
    gfx->fillRoundRect(70, y, 326, 44, 10, done ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(70, y, 326, 44, 10, open_ ? UI_INK : UI_TRACK);
    gfx->setTextColor(open_ ? UI_INK : UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(84, y + 8);
    gfx->print(t.name);
    gfx->setTextSize(1);
    gfx->setTextColor(UI_TRACK);
    gfx->setCursor(84, y + 28);
    gfx->print(open_ ? t.place : T(S_LOCKED));
    // the level of the strongest creature: the honest measure of the wall
    uint8_t top = 0;
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u x%u", top, t.count);
    gfx->setTextColor(done ? UI_BAR_OK : (open_ ? UI_INK : UI_TRACK));
    gfx->setCursor(384 - (int)strlen(lv) * 6, y + 28);
    gfx->print(lv);
    if (done) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setCursor(370, y + 8);
      gfx->print("*");
    }
  }
  uint8_t pages = (TRAINER_COUNT + GYM_ROWS - 1) / GYM_ROWS;
  for (uint8_t i = 0; i < pages; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == gymPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  // the other kind of battle lives here too
  gfx->fillRoundRect(148, 380, 170, 32, 9, UI_BG_DAY);
  gfx->drawRoundRect(148, 380, 170, 32, 9, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_LAN)) * 6, 388);
  gfx->print(T(S_LAN));
  gfx->flush();
}

// The region pill under a waiting egg. Tapping it cycles; Pet::setRegion swaps
// the egg to that region's creature, keeping the rarity it was granted and
// remembering each region's answer so flipping back and forth is not a re-roll.
#define EGGREG_X 133
#define EGGREG_Y 374
#define EGGREG_W 200
#define EGGREG_H 34

static void drawEggRegion() {
  char l[24];
  snprintf(l, sizeof(l), "%s >", pet.regionName());
  gfx->fillRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_WHITE);
  gfx->drawRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(EGGREG_X + (EGGREG_W - (int)strlen(l) * 12) / 2, EGGREG_Y + 9);
  gfx->print(l);
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_EGG_REGION)) * 3, EGGREG_Y + EGGREG_H + 6);
  gfx->print(T(S_EGG_REGION));
}

// True if the tap was on the region pill, so the egg does not also get cracked.
static bool eggRegionTap(int16_t x, int16_t y) {
  if (!pet.isEgg() || x < EGGREG_X || x > EGGREG_X + EGGREG_W ||
      y < EGGREG_Y || y > EGGREG_Y + EGGREG_H) return false;
  pet.setRegion((pet.region + 1) % REGION_COUNT);
  sfxPlay(SFX_TAP);
  return true;
}

// ---------- level-up learn prompt ----------
void renderLearn() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t mv = pet.learnOffer();
  char head[40];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(head, sizeof(head), T(S_LEARN_Q), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(head) * 3, 48);
  gfx->print(head);
  gfx->setTextColor(DEX_TBL[pet.speciesId].accent);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(MOVE_TBL[mv].name) * 9, 66);
  gfx->print(MOVE_TBL[mv].name);

  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(LEARN_ROW_Y(i), pet.moves[i], false, pet.speciesId);

  gfx->fillRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_TRACK);
  gfx->drawRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_LEARN_SKIP)) * 6, LEARN_SKIP_Y + 14);
  gfx->print(T(S_LEARN_SKIP));
  gfx->flush();
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[20];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 48);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(x + 16, y + 13);
      gfx->print("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(x + 44, y + 14);
    gfx->print(medalDesc(i));
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_PROGRESS)) * 9, 44);
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  gfx->setTextSize(5);
  gfx->setCursor(CX - strlen(lv) * 15, 86);
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[26];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(nx) * 6, by + 32);
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(UI_TRACK);
  gfx->setCursor(CX - strlen(T(S_EVO_LABEL)) * 6, 230);
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[28];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    int needed = d.evolveLevel + pet.careMistakes;
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(CX - strlen(evo) * 6, 256);
  gfx->print(evo);

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  gfx->setCursor(CX - strlen(ms) * 6, 312);
  gfx->print(ms);
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMoves();
  else renderCardProgress();

  // indicador de paginas + ayuda
  for (int i = 0; i < CARD_PAGES; i++) {
    int dx = CX - (CARD_PAGES - 1) * 13 + i * 26;
    if (i == cardPage) gfx->fillCircle(dx, 374, 5, UI_INK);
    else gfx->drawCircle(dx, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 398);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- menu overlay ----------

// Row labels are built fresh each frame because two of them carry live counts.
static void menuRowLabel(int i, char *out, size_t n) {
  switch (i) {
    case 0: snprintf(out, n, "%s", T(S_STATS)); break;
    case 1: snprintf(out, n, T(S_POKEDEX_FMT), pet.registeredCount(), DEX_COUNT); break;
    case 2: snprintf(out, n, "%s", T(S_SETTINGS)); break;
    default: snprintf(out, n, "%s", T(S_CLOSE)); break;
  }
}

void drawMenu() {
  // dim the game behind the panel so the overlay reads as modal, and so it is
  // obvious that tapping the darkened area is a way out
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_WHITE);
  gfx->drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_INK);

  for (int i = 0; i < MENU_ROWS; i++) {
    int y = MENU_ROW_Y(i);
    bool close = (i == MENU_ROWS - 1);
    gfx->fillRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12,
                       close ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12, UI_INK);
    char lbl[28];
    menuRowLabel(i, lbl, sizeof(lbl));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(lbl) * 6, y + MENU_ROW_H / 2 - 8);
    gfx->print(lbl);
  }
}

// ---------- training submenu (5th icon) ----------

// Bars here show progress toward the IV-capped ceiling, not a raw stat: 100%
// means this individual cannot train the stat any higher, which is the whole
// point of trMaxFor() gating training by IV.
static uint8_t trainPct(uint8_t cur, uint8_t cap) {
  return cap ? (uint8_t)((uint16_t)cur * 100 / cap) : 0;
}

void renderTrain() {
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_WHITE);
  gfx->drawRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_INK);

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_TRAIN)) * 6, TRAIN_Y + 20);
  gfx->print(T(S_TRAIN));

  const char *lbl[3] = { T(S_TR_ATK), T(S_TR_SPE), T(S_TR_DEF) };
  uint8_t cur[3] = { pet.trAtk, pet.trSpe, pet.trDef };
  uint8_t cap[3] = { pet.trMaxAtk(), pet.trMaxSpe(), pet.trMaxDef() };

  for (int i = 0; i < 3; i++) {
    int y = TRAIN_ROW_Y(i);
    bool passive = (i == 2);   // DEF has no minigame: drawn flat, ignores taps
    gfx->fillRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12,
                       passive ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(TRAIN_X + 32, y + 10);
    gfx->print(lbl[i]);

    uint8_t pct = trainPct(cur[i], cap[i]);
    int bx = TRAIN_X + 32, bw = TRAIN_W - 64, bh = 12, by = y + 34;
    gfx->fillRoundRect(bx, by, bw, bh, 4, UI_TRACK);
    int fw = (bw - 4) * pct / 100;
    if (fw > 0)
      gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, pct >= 100 ? UI_BAR_OK : UI_BAR_WARN);
  }

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_TR_DEF_HINT)) * 3, TRAIN_Y + TRAIN_H - 22);
  gfx->print(T(S_TR_DEF_HINT));
  gfx->flush();   // without this the panel never updates and the screen freezes
}

// ---------- the box ----------
// Storage past the six that fight. A creature is moved by picking a party slot
// and then a box slot, which swaps them -- so one gesture covers deposit,
// withdraw and exchange rather than needing three.
void renderBox() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[32];
  snprintf(head, sizeof(head), T(S_BOX_FMT), party.boxCount(), BOX_SLOTS);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 40);
  gfx->print(head);
  if (boxSwapFrom) {
    const PartyMon &p = party.slots[boxSwapFrom - 1];
    char sub[40];
    snprintf(sub, sizeof(sub), T(S_BOX_SWAP),
             p.empty() ? "-" : (p.nick[0] ? p.nick : DEX_TBL[p.dex].name));
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(sub) * 3, 64);
    gfx->print(sub);
  }
  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t idx = boxPage * BOX_PER_PAGE + i;
    if (idx >= BOX_SLOTS) break;
    const PartyMon &m = party.box[idx];
    int x = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int y = 88 + (i / 2) * (PARTY_CELL_H + 8);
    gfx->fillRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10,
                       m.empty() ? UI_TRACK : UI_WHITE);
    gfx->drawRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_INK);
    if (m.empty()) {
      gfx->setTextColor(0x8410);
      gfx->setTextSize(1);
      gfx->setCursor(x + PARTY_CELL_W / 2 - 18, y + PARTY_CELL_H / 2 - 4);
      gfx->print(T(S_PARTY_EMPTY));
      continue;
    }
    const uint8_t *th = thumbs.get(m.dex);
    if (th) drawThumb(th, x - 14, y - 4, 2, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(x + 52, y + 16);
    gfx->print(m.nick[0] ? m.nick : DEX_TBL[m.dex].name);
    char l[16];
    snprintf(l, sizeof(l), "Lv.%u%s", (unsigned)m.level, m.shiny ? " *" : "");
    gfx->setCursor(x + 52, y + 34);
    gfx->print(l);
  }
  uint8_t pages = BOX_SLOTS / BOX_PER_PAGE;
  for (uint8_t i = 0; i < pages; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == boxPage) gfx->fillCircle(dx, 366, 5, UI_INK);
    else gfx->drawCircle(dx, 366, 4, UI_INK);
  }
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void boxTap(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t idx = boxPage * BOX_PER_PAGE + i;
    if (idx >= BOX_SLOTS) break;
    int cx0 = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int cy0 = 88 + (i / 2) * (PARTY_CELL_H + 8);
    if (x < cx0 || x > cx0 + PARTY_CELL_W || y < cy0 || y > cy0 + PARTY_CELL_H) continue;
    if (boxSwapFrom) {           // a party slot is waiting: complete the trade
      if (party.slots[boxSwapFrom - 1].empty() && party.box[idx].empty()) {
        sfxPlay(SFX_DENY);
        return;
      }
      party.swapPartyBox(boxSwapFrom - 1, idx);
      boxSwapFrom = 0;
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    // otherwise arm from THIS side: if the party has room, send it straight
    // over; if not, remember it and let the player pick who it replaces
    if (party.box[idx].empty()) { sfxPlay(SFX_DENY); return; }
    int free = party.firstFree();
    if (free >= 0) {
      party.swapPartyBox((uint8_t)free, idx);
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    boxSel = idx + 1;            // party is full: go choose who steps out
    boxOpen = false;
    sfxPlay(SFX_TAP);
    return;
  }
  boxOpen = false;               // anywhere else backs out
  boxSwapFrom = 0;
}

// ---------- party ----------

void drawPartySlot(int i, int x, int y) {
  const PartyMon &m = party.slots[i];
  gfx->fillRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10,
                     m.empty() ? UI_TRACK : UI_WHITE);
  gfx->drawRoundRect(x, y, PARTY_CELL_W, PARTY_CELL_H, 10, UI_INK);
  if (m.empty()) {
    gfx->setTextColor(0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(x + (PARTY_CELL_W - (int)strlen(T(S_PARTY_EMPTY)) * 12) / 2,
                   y + PARTY_CELL_H / 2 - 8);
    gfx->print(T(S_PARTY_EMPTY));
    return;
  }
  const uint8_t *th = thumbs.get(m.dex);
  if (th) drawThumb(th, x - 6, y - 3, 1, false);
  const DexEntry &d = DEX_TBL[m.dex];
  const char *nm = m.nick[0] ? m.nick : d.name;
  gfx->setTextColor(d.accent);
  gfx->setTextSize(1);
  gfx->setCursor(x + 62, y + 18);
  gfx->print(nm);
  if (m.shiny) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(x + 62 + (int)strlen(nm) * 6 + 3, y + 18);
    gfx->print("*");
  }
  char lv[12];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), (unsigned)m.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(x + 62, y + 36);
  gfx->print(lv);
}

void renderParty() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  char head[24];
  snprintf(head, sizeof(head), T(S_PARTY_FMT), party.count());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(head) * 9, 42);
  gfx->print(head);

  // the box lives behind this button; it also shows how full it is, so the
  // player knows there is anything in there without opening it
  if (!partyPick) {
    char bl[24];
    snprintf(bl, sizeof(bl), T(S_BOX_FMT), party.boxCount(), BOX_SLOTS);
    bool armed = boxSwapFrom != 0;
    gfx->fillRoundRect(158, 336, 150, 32, 9, armed ? UI_BAR_WARN : UI_BG_DAY);
    gfx->drawRoundRect(158, 336, 150, 32, 9, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(233 - (int)strlen(bl) * 6, 344);
    gfx->print(bl);
  }

  if (boxSel) {
    const PartyMon &b = party.box[boxSel - 1];
    char sw[44];
    snprintf(sw, sizeof(sw), T(S_BOX_SWAP),
             b.nick[0] ? b.nick : DEX_TBL[b.dex].name);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(sw) * 3, 72);
    gfx->print(sw);
  }

  // when a newcomer is waiting, say so instead of the usual hint
  if (partyPick) {
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(T(S_PARTY_FULL)) * 3, 74);
    gfx->print(T(S_PARTY_FULL));
  }

  for (int i = 0; i < PARTY_SLOTS; i++) {
    int x = PARTY_GRID_X + (i % 2) * (PARTY_CELL_W + 10);
    int y = PARTY_GRID_Y + (i / 2) * (PARTY_CELL_H + 8);
    drawPartySlot(i, x, y);
  }

  // exit: an explicit button, always in the same place
  const char *ex = partyPick ? T(S_PARTY_LETGO) : T(S_CLOSE);
  gfx->fillRoundRect(133, 372, 200, 44, 12, partyPick ? UI_BAR_BAD : UI_TRACK);
  gfx->drawRoundRect(133, 372, 200, 44, 12, UI_INK);
  gfx->setTextColor(partyPick ? UI_WHITE : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(ex) * 6, 386);
  gfx->print(ex);
  gfx->flush();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

// The keyboard is shared, so it has to be told what it is naming. It used to
// hardcode pet.rename() on commit, which is why a second caller needed this.
void openKeyboardFor(uint8_t target) {
  kbTarget = target;
  kbOpen = true;
  const char *cur = (target == KB_TRAINER) ? pet.trainerName : pet.nick;
  strncpy(nameBuf, cur, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}
void openKeyboard() { openKeyboardFor(KB_PET); }

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_NAME)) * 6, 56);
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(95, 94);
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    if (i < 28) {
      gfx->setCursor(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      gfx->print(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      gfx->setCursor(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      gfx->print(lab);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col < 0 || col >= KB_COLS || row < 0 || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    if (kbTarget == KB_TRAINER) pet.renameTrainer(nameBuf);
    else pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL 80

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      uint8_t idx = d[r * w + c];
      if (idx == 0xFF) continue;
      uint16_t col = sil ? INK_K : (uint16_t)(pal[idx * 2] | (pal[idx * 2 + 1] << 8));
      gfx->fillRect(ox + c * s, oy + r * s, s, s, col);
    }
  }
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    char head[24];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isShinyRegistered(galleryDetail) ? "*" : "", reg ? d.name : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    int glen = strlen(head);
    int gts = (glen <= 13) ? 3 : 2;  // auto-encoge nombres largos (no caben a t3)
    gfx->setTextSize(gts);
    gfx->setCursor(CX - glen * (gts == 3 ? 9 : 6), gts == 3 ? 56 : 60);
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, reg ? millis() : 0, true, !reg, 6);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !reg);
    }
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_DETAIL_BACK)) * 6, 408);
    gfx->print(T(S_DETAIL_BACK));
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // the region's own name and its own tally: "how much of Johto have I seen"
  // is the question you are actually asking here
  char head[32];
  const RegionInfo &grg = REGIONS[galleryRegion % GAL_REGIONS];
  snprintf(head, sizeof(head), "%s %u/%u", grg.name,
           pet.registeredCountIn(grg.lo, grg.hi), (unsigned)GAL_SPAN);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 36);
  gfx->print(head);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = GAL_LO + galleryPage * GAL_PER_PAGE + r * 4 + c;
      if (dex > GAL_HI || dex > DEX_COUNT) break;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, 2, !pet.isRegistered(dex));
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(2);
          gfx->setCursor(x + 62, y + 4);
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_TRACK);
        gfx->setTextSize(2);
        gfx->setCursor(x + 24, y + 32);
        gfx->print(num);
      }
    }
  }
  // A page number, not a row of dots. 25 dots do not fit across the bottom of
  // a round panel -- the chord at that height is only ~228 px -- and counting
  // them to find where you are is worse than reading the number.
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", galleryPage + 1, (int)GAL_PAGES);
  gfx->setTextColor(UI_TRACK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(pg) * 6, 428);
  gfx->print(pg);
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  if (y < 72) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c < 0 || c > 3 || r < 0 || r > 3) return;
  int16_t dex = GAL_LO + galleryPage * GAL_PER_PAGE + r * 4 + c;
  if (dex > GAL_HI || dex > DEX_COUNT) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - 14, y = 12, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  gfx->setTextColor(nameColor);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(name) * 9, 52);
  gfx->print(name);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(msg) * 6, 90);
  gfx->print(msg);
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// dialogo de decision (2 botones apilados): evolucionar/mantener o despedirse/quedaros
void drawChoiceDialog() {
  const char *q, *o1, *o2;
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  gfx->fillRoundRect(73, 156, 320, 188, 16, UI_WHITE);
  gfx->drawRoundRect(73, 156, 320, 188, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(q) * 6, 176);
  gfx->print(q);
  gfx->fillRoundRect(93, 206, 280, 52, 12, c1);     // boton accion
  gfx->setTextColor(t1);
  gfx->setCursor(CX - (int)strlen(o1) * 6, 224);
  gfx->print(o1);
  gfx->fillRoundRect(93, 268, 280, 52, 12, c2);     // boton mantener/quedaros
  gfx->setTextColor(t2);
  gfx->setCursor(CX - (int)strlen(o2) * 6, 286);
  gfx->print(o2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  const char *t = T(S_EVO_TAP);
  gfx->setCursor(CX - (int)strlen(t) * 9, y + h / 2 - 11);
  gfx->print(t);
}

// boton-CTA dorado de despedida: "<nombre> quiere decirte algo..."
void drawFarewellButton() {
  uint32_t now = millis();
  int p = (int)(4 * sinf(now * 0.005f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, UI_BAR_WARN);
  gfx->drawRoundRect(x, y, w, h, 16, UI_INK);
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(3 * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, 16, C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  int fi = flashIdxForDex(pet.speciesId);
  if (fi < 0) {
    // sin SD y sin sprite de flash: aviso claro de que faltan sprites
    gfx->setTextColor(inkColor());
    gfx->setTextSize(6);
    gfx->setCursor(CX - 18, PET_CY - 80);
    gfx->print("?");
    gfx->setTextSize(2);
    const char *l1 = T(S_NO_SPRITES);
    gfx->setCursor(CX - (int)strlen(l1) * 6, PET_CY - 4);
    gfx->print(l1);
    const char *l2 = T(S_LOAD_SPRITES);
    gfx->setCursor(CX - (int)strlen(l2) * 6, PET_CY + 20);
    gfx->print(l2);
    return;
  }
  const Species &sp = SPECIES[fi];
  int s = sp.scale;
  int x = CX - 16 * s;
  int y = PET_CY - 16 * s;

  // animacion de evolucion: alterna la silueta de la forma anterior y la nueva
  if (pet.evolving()) {
    bool flash = (millis() / 300) % 2;
    int16_t showDex = (flash && pet.prevSpeciesId >= 0) ? pet.prevSpeciesId : pet.speciesId;
    int sfi = flashIdxForDex(showDex);
    if (sfi >= 0) {
      const Species &show = SPECIES[sfi];
      drawMap(show.sprite, SPRITE_H, CX - 16 * show.scale, PET_CY - 16 * show.scale, show.scale, flash);
    }
    return;
  }

  PetMood m = pet.mood();
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= 6;  // saltito

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // expresiones superpuestas usando las anclas de la especie
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, 2, false);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (now > bathUntil) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // pose de alegria al quedar limpio
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  while (t >= a.ms[i]) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  uint8_t sBase = m.acts[PMD_IDLE].h ? 170 / m.acts[PMD_IDLE].h : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    for (int c = 0; c < a.w; c++) {
      uint8_t idx = row[c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x0 + c * s, y0 + r * s, s, s, sil ? INK_K : m.pal[idx]);
    }
  }
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS);
}

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = 150 + random(176);
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else {
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        behNext();
        act = PMD_IDLE;
      } else {
        beh.x += (d > 0 ? 3.0f : -3.0f);
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 5);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
}

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
void drawPetSD() {
  int s = mon.scale;
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= 6;  // saltito
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    for (int c = 0; c < mon.w; c++) {
      uint8_t idx = row[c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, sil ? INK_K : mon.pal[idx]);
    }
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - 30, y - 50, 2, false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado
void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// borra la sonrisa base y pinta boca abierta (comer) o ceno (triste)
void overlayMouth(const Species &sp, int x, int y, int s, bool open) {
  int mc = sp.mouthCol, mr = sp.mouthRow;
  gfx->fillRect(x + (mc - 3) * s, y + mr * s, 7 * s, 2 * s, sp.bodyColor);
  if (open) {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, 2 * s, INK_K);
  } else {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, s, INK_K);
    gfx->fillRect(x + (mc - 3) * s, y + (mr + 1) * s, s, s, INK_K);
    gfx->fillRect(x + (mc + 3) * s, y + (mr + 1) * s, s, s, INK_K);
  }
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

void drawBars() {
  drawBar(78, 318, T(S_BAR_FOOD), pet.fullness);
  drawBar(244, 318, T(S_BAR_JOY), pet.joy);
  drawBar(78, 346, T(S_BAR_ENE), pet.energy);
  drawBar(244, 346, T(S_BAR_HYG), pet.hygiene);
}

void drawBar(int x, int y, const char *label, uint8_t val) {
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x, y);
  gfx->print(label);
  int bx = x + 48, bw = 100, bh = 15;  // +48: deja sitio a etiquetas de 4 letras (EN)
  uint16_t fill = (val >= 50) ? UI_BAR_OK : (val >= 25) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(bx, y, bw, bh, 4, UI_TRACK);
  int fw = (bw - 4) * val / 100;
  if (fw > 0) gfx->fillRoundRect(bx + 2, y + 2, fw, bh - 4, 3, fill);
}

void drawButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    bool off = pet.sleeping && i != 2;  // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, UI_WHITE);
    gfx->drawRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 14, inkColor());
    if (!off) drawMap(buttons[i].icon, 16, buttons[i].cx - 16, buttons[i].cy - 16, 2, false);
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.weight > 60) return T(S_CHUBBY);
  if (pet.shiny && pet.ageMinutes < 15) return T(S_IS_SHINY);
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
// An 8bpp indexed avatar, same shape as the badge art: 0xFF is transparent.
void drawAvatar(uint8_t which, int x, int y, int s) {
  const AvatarArt &a = AVATARS[which % AVATAR_COUNT];
  for (int r = 0; r < AVATAR_PX; r++)
    for (int c = 0; c < AVATAR_PX; c++) {
      uint8_t v = a.idx[r * AVATAR_PX + c];
      if (v == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, a.pal[v]);
    }
}

void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      char ch = map[r][c];
      if (ch == '.') continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, silhouette ? INK_K : spriteColor(ch));
    }
  }
}
