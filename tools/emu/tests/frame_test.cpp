// What reaches the panel, and when.
//
// A flush pushes the whole ~434 KB framebuffer over QSPI. loop() used to draw
// and push ten times a second whatever the panel was doing -- including with
// the screen switched OFF, where not one of those frames could be seen.
// FrameCanvas (the sketch's canvas) decides whether a frame is pushed; this
// test drives the real loop() and counts what actually went out, which the
// stub canvas records in `pushes` and `pushBrightness`.
//
// flush_test answers a different question -- does every render path ASK for a
// flush -- and keeps answering it; this one is about the pushes themselves.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
uint8_t uiCurrentScreen();
extern Pet pet;
extern Arduino_Canvas *gfx;
extern Arduino_CO5300 *panel;
extern bool screenOff, menuOpen, sackOpen, partyOpen;
extern uint8_t dimStage;
extern uint32_t lastInteract;
extern uint32_t gFlushCalls, gFramePushes;
extern uint32_t gCrumbScreen;
extern const char *const SCREEN_NAME[];
#include <cstring>
#include <vector>
void startSack();

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}


// The test's OWN copy of what the panel shows: updated on every push, and every
// flush that did not push is checked against it. A frame that differs from the
// panel and was not pushed is a frozen panel, whatever the canvas thinks.
static std::vector<uint16_t> panelCopy;
static int dropped = 0, moved = 0;
static void afterFlush(uint32_t pushesBefore){
  const uint16_t *fb = gfx->buffer();
  bool differs = panelCopy.empty() || memcmp(panelCopy.data(), fb, panelCopy.size() * 2) != 0;
  if (gfx->pushes != pushesBefore) { if (differs) moved++; panelCopy.assign(fb, fb + 466 * 466); }
  else if (differs && !screenOff) dropped++;   // dark: nothing is meant to go out
}
static void flushTracked(){ uint32_t p = gfx->pushes; gfx->flush(); afterFlush(p); }
// identical flushes until one is skipped: the screen has settled
static void settle(){ for (int i = 0; i < 4; i++) { uint32_t p = gfx->pushes; flushTracked(); if (gfx->pushes == p) return; } }
// real time, because loop() paces itself off millis()
static void pumpMs(int ms){
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint32_t c = gFlushCalls, p = gfx->pushes;
    loop();
    if (gFlushCalls != c) afterFlush(p);
  }
}
static void touch(int x,int y){
  g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt(); pumpMs(40);
  g_touchDown=false; emuFireInterrupt(); pumpMs(40);
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  lastInteract = millis();
  pumpMs(300);

  // --- 1. screen on: frames are drawn ------------------------------------
  uint32_t c0 = gFlushCalls;
  pumpMs(500);
  printf("     lit, 500 ms: %u flushes asked for\n", gFlushCalls - c0);
  ck(gFlushCalls - c0 >= 3, "with the screen on, render() runs at its normal rate");

  // --- 2. screen off: nothing drawn, nothing pushed, crumb still kept ------
  screenOff = true;
  pumpMs(150);                      // the pass that notices it went dark
  c0 = gFlushCalls;
  uint32_t p0 = gfx->pushes;
  menuOpen = true;                  // the screen changes while dark
  pumpMs(800);
  printf("     dark, 800 ms: %u flushes asked for, %u pushed\n",
         gFlushCalls - c0, gfx->pushes - p0);
  ck(gFlushCalls == c0, "with the screen off, nothing is drawn at all");
  ck(gfx->pushes == p0, "and nothing is pushed to the panel");
  ck(gCrumbScreen == uiCurrentScreen(),
     "but the crash breadcrumb still follows the screen that is up");
  menuOpen = false;

  // A render path that IS game logic keeps running in the dark: the bag counts
  // and ends on its own clock inside renderSack(). Drawn, never pushed.
  startSack();
  c0 = gFlushCalls; p0 = gfx->pushes;
  pumpMs(500);
  printf("     dark minigame, 500 ms: %u flushes asked for, %u pushed\n",
         gFlushCalls - c0, gfx->pushes - p0);
  ck(gFlushCalls - c0 >= 3, "a minigame keeps stepping with the screen off");
  ck(gfx->pushes == p0, "without a single frame pushed");
  sackOpen = false;

  // --- 3. waking: a fresh frame is pushed BEFORE the panel is lit ----------
  pumpMs(150);
  p0 = gfx->pushes;
  ck(panel->brightness == 0, "the panel really is dark before the wake");
  // the real path: a touch while dark wakes it (handleTouch clears screenOff)
  std::this_thread::sleep_for(std::chrono::milliseconds(25));   // past the 20 ms touch poll gate
  g_touchX=233; g_touchY=200; g_touchDown=true; emuFireInterrupt();
  { uint32_t c = gFlushCalls, p = gfx->pushes;
    loop();                         // ONE pass: the wake must happen inside it
    if (gFlushCalls != c) afterFlush(p); }
  ck(!screenOff, "the touch woke the screen");
  ck(gfx->pushes > p0, "the wake pushes a frame in the same loop pass");
  ck(gfx->pushBrightness == 0,
     "and pushes it while the panel is still dark, so a stale frame is never lit");
  ck(panel->brightness > 0, "and then lights the panel");
  g_touchDown=false; emuFireInterrupt(); pumpMs(100);

  // --- 4. dimStage 2: about 2 fps -----------------------------------------
  lastInteract = millis() - 400000;     // 400 s idle
  pumpMs(150);
  ck(dimStage == 2, "400 s without a touch reaches dimStage 2");
  c0 = gFlushCalls;
  pumpMs(2000);
  uint32_t dimN = gFlushCalls - c0;
  printf("     dimStage 2, 2000 ms: %u frames\n", dimN);
  ck(dimN >= 3 && dimN <= 6, "dimStage 2 draws about 2 frames a second");
  // and a touch brings the normal rate straight back
  touch(233, 60);
  c0 = gFlushCalls;
  pumpMs(500);
  ck(dimStage == 0 && gFlushCalls - c0 >= 3, "a touch restores the full rate at once");

  // --- 5. an unchanged frame is not pushed --------------------------------
  // The mechanism first, on the framebuffer directly, so it does not depend on
  // what any screen happens to draw.
  {
    uint16_t *fb = gfx->getFramebuffer();
    const int W = 466;
    settle();
    uint32_t q = gfx->pushes;
    flushTracked();
    ck(gfx->pushes == q, "an identical frame is not pushed");
    fb[465 * W + 465] ^= 0x0001; flushTracked();
    ck(gfx->pushes == q + 1, "one changed pixel in the LAST row is pushed");
    settle(); q = gfx->pushes;
    fb[0] ^= 0x0001; flushTracked();
    ck(gfx->pushes == q + 1, "one changed pixel in the FIRST row is pushed");
    // A change near the top of a settled screen must still hash the rows below
    // it, or the next -- identical -- frame finds them unknown and goes out too.
    flushTracked();
    ck(gfx->pushes == q + 1, "a change on a settled screen costs ONE push, not two");

    // The early exit must not leave a stale row vouching for the panel. On a
    // screen that is MOVING (so the scan stops at the first changed row),
    // change rows 10 AND 400: row 400's stored hash is still the OLD content.
    // Put row 400 back to that old content: the panel shows the NEW row 400,
    // so the frame differs from the panel -- even though row 400 now matches
    // its stale stored hash -- and must go out.
    settle(); q = gfx->pushes;
    fb[300 * W + 7] ^= 0x0001; flushTracked();                     // now moving
    uint16_t old400 = fb[400 * W + 10];
    fb[10 * W + 5] ^= 0x0001; fb[400 * W + 10] ^= 0x0101; flushTracked();
    ck(gfx->pushes == q + 2, "a moving screen changed at rows 10 and 400 is pushed");
    fb[400 * W + 10] = old400; flushTracked();
    ck(gfx->pushes == q + 3,
       "a row the early exit never re-hashed is not trusted (row 400 put back is pushed)");
    flushTracked();
    ck(gfx->pushes == q + 3, "and once re-hashed, the same frame is skipped again");
    ck(dropped == 0, "no frame that differed from the panel was ever dropped");
  }

  // Then the screens themselves, through loop().
  lastInteract = millis();
  partyOpen = true;
  pumpMs(300);
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "party"), "the party screen is up");
  c0 = gFlushCalls; p0 = gfx->pushes;
  pumpMs(1500);
  printf("     party, 1500 ms: %u flushes asked for, %u pushed\n",
         gFlushCalls - c0, gfx->pushes - p0);
  ck(gFlushCalls - c0 >= 10, "the static party screen is still redrawn at 10 fps");
  ck(gfx->pushes - p0 <= 1, "but pushed at most once in 1.5 s");
  p0 = gfx->pushes;
  pumpMs(2600);
  ck(gfx->pushes - p0 >= 1 && gfx->pushes - p0 <= 2,
     "and still refreshed about every 2 s, in case a change was missed");
  partyOpen = false;
  pumpMs(150);
  ck(gfx->frameReady, "closing it pushes the main screen");

  // An animated screen loses nothing: every main-screen frame that differs
  // from what the panel shows goes out. Checked against a copy of the panel
  // the test keeps itself, not against the canvas's own bookkeeping.
  c0 = gFlushCalls; p0 = gfx->pushes;
  int moved0 = moved;
  pumpMs(1500);
  printf("     main, 1500 ms: %u flushes asked for, %u pushed, %d of them changed\n",
         gFlushCalls - c0, gfx->pushes - p0, moved - moved0);
  ck(moved - moved0 >= 3, "the main screen really is moving here");
  ck(dropped == 0, "and not one of its changed frames was dropped");

  ck(dropped == 0, "over the whole run, no lit frame that differed from the panel was dropped");

  // What the hash costs here, against what it saves. Printed, not asserted:
  // the host is not the board. See the commit message for the S3 estimate.
  {
    auto t0 = std::chrono::steady_clock::now();
    const int N = 200;
    uint16_t *fb = gfx->getFramebuffer();
    for (int i = 0; i < N; i++) { fb[465 * 466 + 400] ^= 1; gfx->flush(); }   // full scans
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;
    printf("     host: one full-frame scan + decision = %.0f us\n", us);
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
