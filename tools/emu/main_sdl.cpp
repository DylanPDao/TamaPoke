// TamaPoke desktop emulator: runs the real sketch, draws the real framebuffer,
// and feeds mouse clicks in as touch events. Serial commands come from stdin.
#include <SDL.h>   // sdl2-config puts the SDL2 dir on the include path
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include <chrono>
#include <string>
#include <deque>
#include <unistd.h>
#include <sys/select.h>

// --- Arduino runtime globals ---
uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

// millis() lives in clock.cpp so the headless tests share it
void FakeESP::restart() { Serial.println("emu: ESP.restart() -> exiting"); exit(0); }

// --- stdin-backed serial ---
static std::deque<std::string> g_lines;
static std::string g_partial;
static void pumpStdin() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  struct timeval tv = { 0, 0 };
  while (select(1, &fds, nullptr, nullptr, &tv) > 0) {
    char buf[512];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') { g_lines.push_back(g_partial); g_partial.clear(); }
      else g_partial += buf[i];
    }
    FD_ZERO(&fds); FD_SET(0, &fds); tv = { 0, 0 };
  }
}
int FakeSerial::available() { return g_lines.empty() ? 0 : 1; }
String FakeSerial::readStringUntil(char) {
  if (g_lines.empty()) return String("");
  String s(g_lines.front());
  g_lines.pop_front();
  return s;
}

// --- NVS persistence ---
void nvsLoad(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return;
  uint32_t n = 0;
  if (fread(&n, 4, 1, f) != 1) { fclose(f); return; }
  for (uint32_t i = 0; i < n; i++) {
    uint32_t kl = 0, vl = 0;
    if (fread(&kl, 4, 1, f) != 1 || kl > 64) break;
    std::string k(kl, 0);
    if (fread(&k[0], 1, kl, f) != kl) break;
    if (fread(&vl, 4, 1, f) != 1 || vl > 4096) break;
    std::vector<uint8_t> v(vl);
    if (vl && fread(v.data(), 1, vl, f) != vl) break;
    nvs()[k] = v;
  }
  fclose(f);
}
void nvsSave(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  uint32_t n = nvs().size();
  fwrite(&n, 4, 1, f);
  for (auto &kv : nvs()) {
    uint32_t kl = kv.first.size(), vl = kv.second.size();
    fwrite(&kl, 4, 1, f); fwrite(kv.first.data(), 1, kl, f);
    fwrite(&vl, 4, 1, f); if (vl) fwrite(kv.second.data(), 1, vl, f);
  }
  fclose(f);
}

// the sketch
void emuSetSpriteDir(const char *);
void startBattle(int16_t dex, uint8_t lvl);
extern Combatant btlYou, btlFoe;
extern uint32_t btlLungeUntil[2], btlHitUntil[2];
extern uint8_t btlMenu;
void startTrainerBattle(uint8_t idx, bool hard);
extern bool gymOpen, playerOpen;
extern uint8_t playerPage;
extern bool gymHard, pickOpen;
extern uint8_t partyDetail;
extern bool boxOpen; extern uint8_t boxSwapFrom;
extern bool btlNewBadge; extern uint32_t btlWinUntil;
extern uint8_t pickTrainer, pickPage;
void pickDefault(uint8_t cap);
extern bool pickHard;
void startSpeedGame();
void setup();
void loop();
void render();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick, trainOpen, movePickOpen;
extern uint8_t cardPage;

#define PANEL 466

// Headless capture, so the layout can be checked without a display. Writes a
// PPM (no image library needed); convert with `sips -s format png`.
static void writePPM(const char *path) {
  const uint16_t *fb = gfx->buffer();
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", PANEL, PANEL);
  for (int i = 0; i < PANEL * PANEL; i++) {
    uint16_t c = fb[i];
    int x = i % PANEL, y = i / PANEL, dx = x - 233, dy = y - 233;
    uint8_t r = ((c >> 11) & 31) * 255 / 31;
    uint8_t g = ((c >> 5) & 63) * 255 / 63;
    uint8_t b = (c & 31) * 255 / 31;
    if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }  // off-panel
    fputc(r, f); fputc(g, f); fputc(b, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static int shotMode(const char *screen, const char *out, int lvl, int iv, int dex) {
  setup();
  for (int i = 0; i < 4; i++) loop();          // let the sketch settle
  if (pet.awaitingStarter() && strcmp(screen, "starter")) pet.chooseStarter(4);
  if (pet.isEgg() && strcmp(screen, "egg")) pet.dbgHatchAs(dex, false);
  if (lvl > 0) pet.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  if (iv >= 0) {
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = iv;
    pet.trAtk = pet.trMaxAtk(); pet.trDef = pet.trMaxDef(); pet.trSpe = pet.trMaxSpe();
  }
  for (int i = 0; i < 2; i++) loop();          // pick up the sprite for the new species
  cardOpen = galleryOpen = clockOpen = kbOpen = false;
  menuOpen = partyOpen = partyPick = trainOpen = movePickOpen = false;
  if (!strcmp(screen, "battle"))      { cardOpen = true; cardPage = 1; }
  else if (!strcmp(screen, "profile")){ cardOpen = true; cardPage = 0; }
  else if (!strcmp(screen, "medals")) { cardOpen = true; cardPage = 3; }
  else if (!strcmp(screen, "progress")){cardOpen = true; cardPage = 4; }
  else if (!strcmp(screen, "gallery")) galleryOpen = true;
  else if (!strcmp(screen, "clock"))   clockOpen = true;
  else if (!strcmp(screen, "menu"))    menuOpen = true;
  else if (!strcmp(screen, "train"))   trainOpen = true;
  else if (!strcmp(screen, "moves"))   { cardOpen = true; cardPage = 2; }
  else if (!strcmp(screen, "movepick")) { movePickOpen = true; }
  else if (!strcmp(screen, "battle2")) { startBattle(9, 50); }
  else if (!strcmp(screen, "btlmenu")) { startTrainerBattle(3, false); }
  else if (!strcmp(screen, "btlswitch")) { startTrainerBattle(3, false); btlMenu = 2; }
  else if (!strcmp(screen, "battleanim")) {
    startBattle(9, 50);
    btlFoe.hp = btlFoe.maxHp / 3;      // bar mid-drain
    btlLungeUntil[0] = millis() + 130; // you mid-lunge
    btlHitUntil[1] = millis() + 300;   // foe flinching
  }
  else if (!strcmp(screen, "gyms")) { gymOpen = true; }
  else if (!strcmp(screen, "box")) {
    static const int f[]={9,25,143,94,131,3};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=f[i]; m.level=40+i*5;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    static const int b[]={6,65,68,143,12,131,94,25};
    for(int i=0;i<8;i++){ PartyMon m; m.dex=b[i]; m.level=30+i*4;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; m.shiny=(i==3);
      party.box[i]=m; }
    party.boxSave();
    partyOpen=true; boxOpen=true; boxSwapFrom=1;
  }
  else if (!strcmp(screen, "win")) {
    startTrainerBattle(2, true);
    btlNewBadge = true; btlWinUntil = 60000; pet.badgesHard = 0x07;
  }
  else if (!strcmp(screen, "pmon")) {
    Pet t; t.dbgHatchAs(131,false); t.ivAtk=t.ivDef=t.ivSpe=t.ivHp=27;
    t.ageMinutes=53UL*MINUTES_PER_LEVEL; t.relearnFromLevel();
    PartyMon m; m.dex=131; m.level=54; m.shiny=1;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=27; m.trAtk=m.trDef=m.trSpe=40;
    for(int k=0;k<MOVE_SLOTS;k++) m.moves[k]=t.moves[k];
    snprintf(m.nick,sizeof(m.nick),"NESSIE");
    party.replaceAt(0,m);
    partyOpen = true; partyDetail = 1;
  }
  else if (!strcmp(screen, "pick")) {
    static const int fill[]={9,25,143,94,131,3};
    static const int fill6[]={9,25,143,94,131,3};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=fill6[i]; m.level=40+i*6;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    pickTrainer = 7; pickHard = false; pickPage = 0;
    pickDefault(6); pickOpen = true;
  }
  else if (!strcmp(screen, "gymshard")) { gymOpen = true; gymHard = true; pet.badgesHard = 0x03; }
  else if (!strcmp(screen, "speed")) { startSpeedGame(); }
  else if (!strcmp(screen, "player")) { pet.badges = 0xBF; pet.badgesHard = 0x0A; pet.streak = 5; playerOpen = true; }
  else if (!strcmp(screen, "medals2")) { pet.medals = 0x5B; pet.totalMedals = 12; playerOpen = true; playerPage = 1; }
  else if (!strcmp(screen, "gymfight")) { startTrainerBattle(0, false); }
  else if (!strcmp(screen, "learn")) {
    // fill the four slots, then cross a gate so the offer has to be answered
    pet.relearnFromLevel();
    pet.lastLearnLevel = 0;
    pet.checkLearnGates();
  }
  else if (!strcmp(screen, "party") || !strcmp(screen, "partyfull")) {
    partyOpen = true;
    static const int fill[] = { 6, 25, 149, 94, 143, 131 };
    static const char *nk[] = { "EMBER", "SPARK", "", "SPOOK", "", "" };
    int n = !strcmp(screen, "partyfull") ? 6 : 3;
    for (int i = 0; i < n; i++) {
      PartyMon m;
      m.dex = fill[i]; m.level = 40 + i * 7; m.shiny = (i == 2);
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      snprintf(m.nick, sizeof(m.nick), "%s", nk[i]);
      party.replaceAt(i, m);
    }
    if (!strcmp(screen, "partyfull")) partyPick = true;
  }
  render();
  writePPM(out);
  return 0;
}

int main(int argc, char **argv) {
  int scale = 2;
  const char *save = "tamapoke.nvs";
  const char *shot = nullptr, *shotOut = "shot.ppm";
  int shotLvl = 0, shotIv = -1, shotDex = 6;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fast") && i + 1 < argc) emuSetTimeScale(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) shotOut = argv[++i];
    else if (!strcmp(argv[i], "--lvl") && i + 1 < argc) shotLvl = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--iv") && i + 1 < argc) shotIv = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--dex") && i + 1 < argc) shotDex = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sprites") && i + 1 < argc) emuSetSpriteDir(argv[++i]);
    else if (!strcmp(argv[i], "--wipe")) { remove(save); }
  }
  if (shot) return shotMode(shot, shotOut, shotLvl, shotIv, shotDex);   // headless: no SDL at all
  nvsLoad(save);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window *win = SDL_CreateWindow("TamaPoke (emulator)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, PANEL * scale, PANEL * scale,
                                     SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
                                       SDL_TEXTUREACCESS_STREAMING, PANEL, PANEL);

  printf("TamaPoke emulator — click to touch, drag to swipe, hold 3s to release.\n");
  printf("Type serial commands here (STATS, IV 31 31 31 31, EGG 150 1, LVL 73, WIPE...)\n");
  printf("Time scale x%u (suspended while you touch). Ctrl-C or close the window to quit.\n\n",
         emuTimeScale());

  setup();

  bool run = true;
  std::vector<uint32_t> px(PANEL * PANEL);
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) run = false;
      // Every mouse event pulses the touch INT, the way the CST9217 does on the
      // board. The sketch ignores the panel entirely until that fires.
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        g_touchX = e.button.x / scale; g_touchY = e.button.y / scale; g_touchDown = true;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        g_touchDown = false;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEMOTION && g_touchDown) {
        g_touchX = e.motion.x / scale; g_touchY = e.motion.y / scale;
        emuFireInterrupt();
      } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) run = false;
    }
    pumpStdin();
    loop();

    if (gfx && gfx->frameReady) {
      gfx->frameReady = false;
      const uint16_t *fb = gfx->buffer();
      for (int y = 0; y < PANEL; y++)
        for (int x = 0; x < PANEL; x++) {
          uint16_t c = fb[y * PANEL + x];
          uint32_t r = ((c >> 11) & 31) * 255 / 31;
          uint32_t g = ((c >> 5) & 63) * 255 / 63;
          uint32_t b = (c & 31) * 255 / 31;
          // dim what falls outside the round panel: those pixels exist in the
          // framebuffer but are not visible on the real hardware
          int dx = x - 233, dy = y - 233;
          if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }
          px[y * PANEL + x] = (r << 16) | (g << 8) | b;
        }
      SDL_UpdateTexture(tex, nullptr, px.data(), PANEL * 4);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
    }
    SDL_Delay(5);
  }

  nvsSave(save);
  printf("\nemu: state saved to %s\n", save);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
