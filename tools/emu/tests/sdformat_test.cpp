// An unreadable SD card is ASKED about, never silently formatted.
//
// sdBegin() used to mount with format_if_mount_failed = true, so any card whose
// filesystem would not mount -- exFAT on most cards over 32 GB, NTFS, a glitch
// -- was wiped at boot without a word. Now a card that answers but will not
// mount raises a modal question with FORMAT and CANCEL, and no card at all
// stays the silent no-sprites path.
//
// Each scenario runs in its own forked child from a fresh boot, because the
// question only ever arises in setup(): appending scenarios onto one long run
// would test whatever state the previous one left behind (CLAUDE.md, trap 3).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "i18n.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
uint32_t g_seed=55; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void onTap(int16_t,int16_t); void onSwipe(int); void onSwipeV(int);
uint8_t uiCurrentScreen();
void sdAskRects(int *x0, int *x1, int *cTop, int *cBot, int *fTop, int *fBot);
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern uint8_t sdAsk;
extern uint32_t sdAskUntil;
extern uint8_t rpickPage;
extern PmdMon pmd;
extern bool gymOpen, partyOpen, playerOpen, cardOpen, menuOpen;
void emuSetSdCard(int mode);
void emuSetSdFormatResult(bool ok);
int emuSdFormatCalls();
void emuSetSdFormatHook(void (*f)());
enum { SDASK_NONE = 0, SDASK_ASK, SDASK_BUSY, SDASK_OK, SDASK_FAIL };   // the sketch's
#define UI_TAP_MIN 44

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *scr(){ return SCREEN_NAME[uiCurrentScreen()]; }
static int X0,X1,CT,CB,FT,FB;

// A card's worth of content -- one thumbnail and one sprite -- so "no sprites
// after CANCEL" can actually fail: with nothing to load (the repo ships no
// sprites) that check would be true whatever the firmware did.
void emuSetSpriteDir(const char *);
static char g_dir[] = "/tmp/sdformatXXXXXX";
static void put(const char *name, const uint8_t *b, size_t n){
  char p[96]; snprintf(p, sizeof(p), "%s/%s", g_dir, name);
  FILE *f = fopen(p, "wb"); fwrite(b, 1, n, f); fclose(f);
}
static void makeCard(){
  mkdtemp(g_dir);
  emuSetSpriteDir(g_dir);
  // thumbs.bin: one species, a 1x1 image on a 1-colour palette
  const uint8_t th[] = { 'T','P','T','H', 1,0, 10,0,0,0, 1,1,1, 0x34,0x12, 0 };
  put("thumbs.bin", th, sizeof(th));
  // p001.bin: one IDLE action, 1x1, one frame of 100 ms
  const uint8_t pk[] = { 'T','P','K','2', 1, 1,0, 0x34,0x12, PMD_IDLE,1,1,1, 100,0, 0 };
  put("p001.bin", pk, sizeof(pk));
}

static void boot(int card){
  makeCard();
  emuSetSdCard(card);
  setLang(LANG_EN);   // past the first-boot language question, which comes first
  setup();
  for (int i = 0; i < 3; i++) loop();
  sdAskRects(&X0,&X1,&CT,&CB,&FT,&FB);
}
static void tapCancel(){ onTap((X0+X1)/2, (CT+CB)/2); }
static void tapFormat(){ onTap((X0+X1)/2, (FT+FB)/2); }

// What the panel showed at the instant the blocking format began.
static uint8_t g_askAtFormat = 0xFF;
static bool g_flushedAtFormat = false;
static void atFormat(){ g_askAtFormat = sdAsk; g_flushedAtFormat = gfx->frameReady; }

// ---- an unreadable card, then CANCEL
static void unreadableCancel(){
  boot(2);
  ck(!sdReady && sdNeedsFormat, "the stub reports a card that answered but will not mount");
  ck(!strcmp(scr(), "sdcard"), "an unreadable card puts the question on the panel");
  ck(pet.awaitingStarter(), "on a first boot, with the region chooser waiting behind it");
  ck(emuSdFormatCalls() == 0, "and nothing was formatted on the way up");

  ck(CB - CT >= UI_TAP_MIN && FB - FT >= UI_TAP_MIN, "both buttons are at least UI_TAP_MIN tall");
  ck(FT - CB >= 24, "with a real dead band between CANCEL and FORMAT");
  ck(X1 - X0 >= 200, "and wide enough to hit");

  // misses: the dead band, beside the buttons, the text, the top of the panel
  const int miss[][2] = { {(X0+X1)/2, (CB+FT)/2}, {X0-12, (FT+FB)/2}, {X1+12, (CT+CB)/2},
                          {233, 160}, {233, 60}, {(X0+X1)/2, FB+6} };
  for (auto &m : miss) { onTap(m[0], m[1]); }
  ck(sdAsk == SDASK_ASK && !strcmp(scr(), "sdcard"), "a tap that misses both buttons does nothing");
  ck(emuSdFormatCalls() == 0, "and certainly does not format");

  uint8_t pg = rpickPage;
  onSwipe(-1); onSwipe(1); onSwipeV(-1); onSwipeV(1);
  ck(sdAsk == SDASK_ASK && rpickPage == pg, "swipes neither close it nor page the chooser behind it");
  ck(!gymOpen && !partyOpen && !playerOpen && !cardOpen && !menuOpen, "nor open a screen underneath it");

  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the question flushes");

  tapCancel();
  ck(sdAsk == SDASK_NONE, "CANCEL closes the question");
  ck(emuSdFormatCalls() == 0, "CANCEL never formats");
  ck(!sdReady, "and the game carries on with no card mounted");
  for (int i = 0; i < 3; i++) loop();
  ck(!strcmp(scr(), "region"), "straight on to the first-boot region chooser");
  ck(!thumbs.loaded, "on the no-sprite path: the thumbnails on the card are not read");
  { PmdMon m; ck(!m.load(1, false), "and no sprite loads off a card that is not mounted"); }
  // companion to the swipe check above: the same swipe DOES page this chooser
  // once the question is gone, so 'did not page' was not true by default
  uint8_t before = rpickPage; onSwipe(-1);
  ck(rpickPage != before, "(and without the dialog that swipe pages the chooser)");
}

// ---- an unreadable card, then FORMAT, which works
static void unreadableFormatOk(){
  boot(2);
  emuSetSdFormatHook(atFormat);
  ck(!strcmp(scr(), "sdcard"), "the question is up");
  gfx->frameReady = false;
  tapFormat();
  ck(emuSdFormatCalls() == 1, "FORMAT formats, once");
  ck(g_askAtFormat == SDASK_BUSY, "the panel said 'formatting' when the blocking call began");
  ck(g_flushedAtFormat, "and that frame was FLUSHED before it, not just drawn");
  ck(sdReady && !sdNeedsFormat, "the card is mounted afterwards");
  ck(thumbs.loaded, "and the post-mount work ran: the thumbnails were loaded");
  ck(sdAsk == SDASK_OK && !strcmp(scr(), "sdcard"), "the result is reported on the panel");
  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the result page flushes");
  sdAskUntil = millis() - 1;     // its time is up
  loop();
  ck(sdAsk == SDASK_NONE, "the result closes itself");
  ck(!strcmp(scr(), "region"), "leaving the first-boot flow");
}

// ---- an unreadable card, then FORMAT, which fails
static void unreadableFormatFails(){
  emuSetSdFormatResult(false);
  boot(2);
  tapFormat();
  ck(emuSdFormatCalls() == 1, "FORMAT tried");
  ck(!sdReady, "the card is still not mounted");
  ck(sdAsk == SDASK_FAIL && !strcmp(scr(), "sdcard"), "and the failure is reported on the panel");
  gfx->frameReady = false; render();
  ck(gfx->frameReady, "the failure page flushes");
  onTap(233, 60);
  ck(sdAsk == SDASK_NONE, "a tap dismisses the result");
  ck(!strcmp(scr(), "region"), "and the game goes on without sprites");
}

// ---- no card at all: the old silent path
static void noCard(){
  boot(1);
  ck(!sdReady && !sdNeedsFormat, "no card: nothing mounted, nothing to ask");
  ck(sdAsk == SDASK_NONE && strcmp(scr(), "sdcard"), "no card raises no dialog");
  ck(!strcmp(scr(), "region"), "the first boot goes straight to the region chooser");
  ck(emuSdFormatCalls() == 0, "and nothing is formatted");
}

// ---- a readable card: no question either
static void goodCard(){
  boot(0);
  ck(sdReady && sdAsk == SDASK_NONE, "a readable card mounts and asks nothing");
  // companions to the CANCEL checks: on a mounted card the same content loads
  ck(thumbs.loaded, "(a mounted card's thumbnails do load)");
  { PmdMon m; ck(m.load(1, false), "(and its sprite does too)"); }
}

static int run(void (*f)()){
  fflush(stdout);
  pid_t p = fork();
  if (p == 0) {
    f(); fflush(stdout);
    char q[96];
    snprintf(q, sizeof(q), "%s/thumbs.bin", g_dir); unlink(q);
    snprintf(q, sizeof(q), "%s/p001.bin", g_dir); unlink(q);
    rmdir(g_dir);
    _exit(bad ? 1 : 0);
  }
  int st = 0; waitpid(p, &st, 0);
  return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;
}

int main(){
  int fails = 0;
  printf("-- unreadable card, CANCEL\n");   fails += run(unreadableCancel);
  printf("-- unreadable card, FORMAT ok\n"); fails += run(unreadableFormatOk);
  printf("-- unreadable card, FORMAT fails\n"); fails += run(unreadableFormatFails);
  printf("-- no card\n");                   fails += run(noCard);
  printf("-- readable card\n");             fails += run(goodCard);
  printf("%s\n", fails ? "FAILURES" : "all good");
  return fails ? 1 : 0;
}
