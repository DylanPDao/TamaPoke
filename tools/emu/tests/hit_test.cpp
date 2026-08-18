// Where the battle grid can actually be TAPPED, as opposed to where it is drawn.
//
// This exists because of a hardware report: the two bottom buttons were much
// harder to press than the top two. The emulator could never have found it --
// its taps are exact coordinates, so a cell that is one pixel tall still passes
// every synthetic test. A finger is not exact, and the drawn cells are 44 px
// tall with an 8 px dead gap between the rows and nothing live beneath them.
//
// So the thing worth asserting is not "a tap in the middle works", it is that
// there are NO DEAD PIXELS between the cells and that the bottom row is not
// smaller than the top row.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include <cstdio>
uint32_t g_seed=4; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void render(); void battleTap(int16_t,int16_t);
extern Pet pet;
extern bool battleOpen;
extern uint8_t btlMenu;
extern Combatant btlYou;
void startBattle(int16_t dex, uint8_t lvl);
int btlCellIndexAt(int16_t x, int16_t y);
void partyButtonRects(int *boxTop, int *boxBot, int *closeTop, int *closeBot);
void uiButtonHeights(int *out, int max, int *n);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 50UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // Sweep the whole grid area and record which cell each pixel belongs to.
  const int X0 = 40, X1 = 430, Y0 = 270, Y1 = 420;
  int area[4] = {0,0,0,0};
  int overlap = 0;
  int gapRow = 0, gapCol = 0;
  for (int y = Y0; y <= Y1; y++)
    for (int x = X0; x <= X1; x++) {
      int hits = 0, which = -1;
      for (int i = 0; i < 4; i++)
        if (btlCellIndexAt(x, y) == i) { hits++; which = i; }
      // btlCellIndexAt returns the FIRST match, so overlap is found by asking
      // each cell directly instead
      (void)hits;
      if (which >= 0) area[which]++;
    }
  ck(area[0] > 0 && area[1] > 0 && area[2] > 0 && area[3] > 0,
     "all four cells are reachable");
  printf("      areas: %d %d %d %d px\n", area[0], area[1], area[2], area[3]);

  // THE POINT: the bottom row must be at least as easy to hit as the top row.
  ck(area[2] >= area[0] && area[3] >= area[1],
     "the bottom row is not smaller than the top row");

  // no dead horizontal seam between the rows
  int seam = 0;
  for (int y = 328; y <= 340; y++)
    if (btlCellIndexAt(200, y) < 0) seam++;
  ck(seam == 0, "no dead gap between the two rows");
  (void)gapRow; (void)gapCol; (void)overlap;

  // no dead vertical seam between the columns
  int vseam = 0;
  for (int x = 226; x <= 240; x++)
    if (btlCellIndexAt(x, 300) < 0) vseam++;
  ck(vseam == 0, "no dead gap between the two columns");

  // a tap just below the bottom row still counts -- that is where a finger
  // reaching for the lowest button actually lands
  ck(btlCellIndexAt(150, 392) == 2 && btlCellIndexAt(320, 392) == 3,
     "a low tap still reaches the bottom row");

  // The four boxes must TILE: the seam between them is one pixel wide, owned by
  // exactly one side. Checked at the boundaries rather than by re-deriving the
  // rectangles here -- a test that copies the geometry drifts from it.
  ck(btlCellIndexAt(232, 300) == 0 && btlCellIndexAt(233, 300) == 1,
     "the column seam belongs to exactly one side");
  ck(btlCellIndexAt(200, 333) == 0 && btlCellIndexAt(200, 334) == 2,
     "and so does the row seam");

  // finally, drive a real tap low in the bottom-left cell through battleTap
  startBattle(9, 50);
  btlMenu = 1;
  uint8_t before = btlMenu;
  battleTap(150, 390);
  ck(btlMenu != before, "a low tap in the move grid is actually accepted");

  // The party screen's BOX and CLOSE buttons must not share a pixel. Padding
  // BOX to make it easier to hit pushed its hit area 8 px into CLOSE, so taps
  // meant to close the screen opened the box instead -- fixing one target by
  // stealing from its neighbour.
  {
    int boxTop, boxBot, clTop, clBot;
    partyButtonRects(&boxTop, &boxBot, &clTop, &clBot);
    ck(boxBot < clTop, "BOX and CLOSE hit areas do not overlap");
    ck(clTop - boxBot >= 4, "and there is a real gap between them");
    ck(clBot < 466 && boxTop > 0, "both stay on the panel");
    printf("      BOX %d..%d, CLOSE %d..%d, gap %d px\n",
           boxTop, boxBot, clTop, clBot, clTop - boxBot);
  }

  // Three separate "hard to hit" reports came in from the board, all the same
  // mistake: a button sized to fit its label rather than a finger. This holds
  // every primary control to one minimum so the fourth report does not happen.
  {
    int h[8], n = 0;
    uiButtonHeights(h, 8, &n);
    int small = 0;
    for (int i = 0; i < n; i++) if (h[i] < 44) small++;
    printf("      button heights:");
    for (int i = 0; i < n; i++) printf(" %d", h[i]);
    printf(" px\n");
    ck(small == 0, "every primary button is at least 44 px tall");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
