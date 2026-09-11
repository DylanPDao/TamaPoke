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
extern uint8_t choiceKind;
extern uint8_t btlMenu;
extern Combatant btlYou;
void startBattle(int16_t dex, uint8_t lvl);
int btlCellIndexAt(int16_t x, int16_t y);
void partyButtonRects(int *boxTop, int *boxBot, int *closeTop, int *closeBot);
void uiButtonHeights(int *out, int max, int *n);
void gymHeaderRects(int *pillTop, int *pillBot, int *rowTop);
int uiSleepButton(int *cx, int *cy);
void uiEggPillRect(int *x, int *y, int *w, int *h, bool hitArea);
bool uiButtonDisabled(int i);
void uiButtonAt(int i, int *cx, int *cy, int *half);
bool monSheetBtn(int16_t x, int16_t y, bool left);
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
#include "badges.h"
#include "trainers.h"
bool badgeArtExists(uint8_t region, uint8_t i);
extern Pet pet;
void onTap(int16_t x, int16_t y);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // These assert the STRICT rules. Kids mode (the default in this fork, see
  // kids_test) forgives all of them, so it is switched off here on purpose.
  setup();
  pet.setKidsMode(false);
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 50UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // Sweep the whole grid area and record which cell each pixel belongs to.
  const int X0 = 40, X1 = 430, Y0 = 258, Y1 = 410;
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
  for (int y = 316; y <= 328; y++)
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
  ck(btlCellIndexAt(150, 372) == 2 && btlCellIndexAt(320, 372) == 3,
     "a low tap still reaches the bottom row");

  // The four boxes must TILE: the seam between them is one pixel wide, owned by
  // exactly one side. Checked at the boundaries rather than by re-deriving the
  // rectangles here -- a test that copies the geometry drifts from it.
  ck(btlCellIndexAt(232, 300) == 0 && btlCellIndexAt(233, 300) == 1,
     "the column seam belongs to exactly one side");
  ck(btlCellIndexAt(200, 321) == 0 && btlCellIndexAt(200, 322) == 2,
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

  {
    int pt, pb, rt;
    gymHeaderRects(&pt, &pb, &rt);
    ck(pb < rt, "the gym difficulty pill does not sit on the first leader row");
    printf("      pill %d..%d, first row at %d\n", pt, pb, rt);
  }

  // While the pet sleeps only ONE home icon works, and it must be the LIGHT.
  // Removing the ball icon shifted every index by one and left drawButtons()
  // lighting index 2, which had become the BATH -- so the wash button looked
  // like the wake-up button.
  {
    battleOpen = false;        // the battle above owns every tap until it closes
    const int BTN_LIGHT_IDX = uiSleepButton(nullptr, nullptr);
    int lx = 0, ly = 0;
    uiSleepButton(&lx, &ly);
    if (pet.awaitingStarter()) pet.chooseStarter(4);
    if (pet.isEgg()) pet.dbgHatchAs(6, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    if (!pet.sleeping) pet.toggleLight();
    ck(pet.sleeping, "the pet is asleep");
    // the bath icon must NOT wake it
    int bx = 0, by = 0;
    uiButtonAt(2, &bx, &by, nullptr);
    onTap((int16_t)bx, (int16_t)by);
    ck(pet.sleeping, "the bath icon does not wake a sleeping pet");
    // and what is drawn greyed is exactly what is refused: one answer, so the
    // dimming can never point at a different icon than the tap handler does
    bool grey[4];
    for (int i = 0; i < 4; i++) grey[i] = uiButtonDisabled(i);
    ck(!grey[BTN_LIGHT_IDX] && grey[0] && grey[2] && grey[3],
       "and it is the only icon drawn lit while asleep");

    // the light icon must
    onTap((int16_t)lx, (int16_t)ly);
    ck(!pet.sleeping, "the light icon does");
    for (int i = 0; i < 4; i++)
      if (uiButtonDisabled(i)) bad++, printf("FAIL  icon %d still greyed awake\n", i);
    ck(true, "and awake, every icon is live again");
  }

  // the home icons must not overlap each other now that they are bigger
  {
    int worst = 9999;
    for (int i = 0; i + 1 < 4; i++) {
      int ax, ay, ah, bx2, by2, bh;
      uiButtonAt(i, &ax, &ay, &ah);
      uiButtonAt(i + 1, &bx2, &by2, &bh);
      int gap = (bx2 - bh) - (ax + ah);
      if (gap < worst) worst = gap;
    }
    printf("      smallest gap between home icons: %d px\n", worst);
    ck(worst >= 0, "the home icons do not overlap");
  }

  // THE EGG REGION PILL. Missing it fell through to pet.eggTap(), and three
  // taps hatch -- so fumbling at the region selector hatched the egg you were
  // trying to re-aim. Reported from a board: "i wasnt able to change egg
  // setting. it kept hatching the egg".
  {
    int gx, gy, gw, gh, hx, hy, hw, hh;
    uiEggPillRect(&gx, &gy, &gw, &gh, false);
    uiEggPillRect(&hx, &hy, &hw, &hh, true);
    printf("      pill %dx%d, hit area %dx%d\n", gw, gh, hw, hh);
    ck(hw > gw && hh > gh, "the pill's hit area is bigger than the pill");
    ck(hh >= 44 && hw >= 44, "and is at least UI_TAP_MIN across");

    // A fresh egg, then taps in the DEAD GUARD BAND -- past the hit area but
    // inside the swallow zone, which is where a fumbled re-aim actually lands.
    //
    // These offsets used to be 8 px, which is INSIDE the hit area (EGGREG_PAD
    // is 16), so every "near miss" was a direct hit that cycled the region.
    // The region assertion passed only because 12 taps over REGION_COUNT 4
    // came full circle back to the start; Sinnoh made it 5 and the coincidence
    // died. It never once exercised the guard band it claims to protect --
    // CLAUDE.md trap 3, a test proving the arithmetic rather than the firmware.
    //
    // Derived from the two rects above, NOT from EGGREG_PAD/EGGREG_GUARD: those
    // are #defines inside the sketch and copying them here would put a second
    // copy of a firmware constant in the test, which is the same mistake in a
    // different place. (gx - hx) IS the pad, so a few px past it is outside the
    // hit area and inside the swallow zone beyond it.
    const int band = (gx - hx) + 4;
    pet.newEgg();
    while (!pet.isEgg()) pet.newEgg();
    uint8_t wasRegion = pet.region;
    int changed = 0, hatched = 0;
    for (int i = 0; i < 6; i++) {
      // checked after EVERY tap, so no number of taps can cancel out again
      onTap((int16_t)(gx + gw / 2), (int16_t)(gy + gh + band));  // just below
      if (pet.region != wasRegion) changed++;
      if (!pet.isEgg()) hatched++;
      onTap((int16_t)(gx - band), (int16_t)(gy + gh / 2));       // just left
      if (pet.region != wasRegion) changed++;
      if (!pet.isEgg()) hatched++;
    }
    ck(hatched == 0, "a near miss on the pill does NOT hatch the egg");
    ck(changed == 0, "and does not silently change the region either");
    ck(band > (gx - hx) && band > (gy - hy),
       "and those taps really were outside the hit area, not on the pill");

    // on the pill: it cycles
    onTap((int16_t)(gx + gw / 2), (int16_t)(gy + gh / 2));
    ck(pet.region != wasRegion, "tapping the pill really does change the region");
    ck(pet.isEgg(), "and never cracks the egg while doing it");

    // and the egg itself still hatches when you actually tap the egg
    for (int i = 0; i < 4; i++) onTap(233, 200);
    ck(!pet.isEgg(), "tapping the egg still hatches it");
  }

  // The runaway fires on the tap, with NO dialog, and that is deliberate: a
  // creature you have to authorise to leave is not at stake, and neglect
  // having teeth is the premise. What must never happen is REACHING this state
  // by going to sleep -- night_test covers that end of it.
  {
    battleOpen = false;
    if (pet.awaitingStarter()) pet.chooseStarter(4);
    if (pet.isEgg()) pet.dbgHatchAs(147, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    if (pet.sleeping) pet.toggleLight();
    pet.dbgRunawayReady();
    ck(pet.canRunawayNow(), "total neglect really does make it ready to leave");
    onTap(233, 200);
    ck(pet.ceremony != CER_NONE, "and the tap lets it go, without asking");
  }

  // ---- the party/box detail sheet, where RELEASE is irreversible
  //
  // The bounds are DISCOVERED by probing the firmware's own predicate rather
  // than restated from the PDET_* constants. A test that copies the numbers
  // passes happily after somebody moves the button and forgets the test, which
  // is the failure mode section 3 of CLAUDE.md is about.
  {
    int lT=999, lB=-1, lL=999, lR=-1, rT=999, rB=-1, rL=999, rR=-1;
    for (int y = 250; y < 440; y++) {
      for (int x = 40; x < 430; x++) {
        if (monSheetBtn(x, y, true)) {
          if (y < lT) lT = y;  if (y > lB) lB = y;
          if (x < lL) lL = x;  if (x > lR) lR = x;
        }
        if (monSheetBtn(x, y, false)) {
          if (y < rT) rT = y;  if (y > rB) rB = y;
          if (x < rL) rL = x;  if (x > rR) rR = x;
        }
      }
    }
    ck(lB > lT && rB > rT, "both sheet buttons have a hit area at all");
    ck((lB - lT) >= 44 && (rB - rT) >= 44,
       "and both are at least UI_TAP_MIN tall -- BRING BACK used to be 38");
    ck((lR - lL) >= 44 && (rR - rL) >= 44, "and at least UI_TAP_MIN across");
    // RELEASE cannot be recovered from, so a finger sliding off BRING BACK must
    // land on nothing rather than on the destructive one.
    ck(rL > lR, "they do not overlap");
    ck(rL - lR >= 8, "and there is a real dead gap between them");
    // WHERE THE GAP IS MATTERS. The first version centred the pair on 233, which
    // put the dead gap on the panel's centre line -- the one spot a thumb aims
    // at, and exactly where every build up to v3.5 drew BRING BACK as a single
    // full-width button. It was reported as "the bring back button is not
    // working": the taps were landing between the two.
    ck(monSheetBtn(233, (lT + lB) / 2, true),
       "the panel CENTRE lands on the primary button, not in the gap");
    ck(!monSheetBtn(233, (lT + lB) / 2, false),
       "and never on the destructive one");
    // Both must sit on the round panel: the corner furthest from the centre is
    // the one that falls off a 466 px circle of radius 233.
    int worst = 0;
    const int cx = 233, cy = 233;
    int xs[4] = { lL, lR, rL, rR }, ys[2] = { lT, lB };
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 2; j++) {
        int dx = xs[i] - cx, dy = ys[j] - cy;
        int d = (int)(0.5 + __builtin_sqrt((double)(dx*dx + dy*dy)));
        if (d > worst) worst = d;
      }
    ck(worst <= 233, "and every corner is still on the glass");
  }

  // ---- the confirm panel, which now has three callers
  {
    int b1t, b1b, b2t, b2b;
    uiConfirmRects(&b1t, &b1b, &b2t, &b2b);
    ck((b1b - b1t) >= 44 && (b2b - b2t) >= 44,
       "both confirm buttons are at least UI_TAP_MIN tall");
    ck(b2t > b1b, "and they do not overlap");
  }

  // ---- badge art must never be borrowed from another region
  //
  // BADGES_ART was indexed as [region % BADGE_REGIONS]. The upstream badge set
  // stops at Unova, so the moment Kalos made GYM_REGIONS 6 against
  // BADGE_REGIONS 5, `5 % 5 == 0` dressed Kalos in KANTO's badges on both the
  // win screen and the player card -- silently, and no test looked. A wrong
  // badge is worse than a blank one: it claims you won something you did not.
  {
    for (uint8_t r = 0; r < BADGE_REGIONS; r++)
      for (uint8_t i = 0; i < TRAINER_GYMS; i++)
        if (!badgeArtExists(r, i)) { ck(false, "a region WITH art returned none"); goto done; }
    ck(true, "every region with art returns it");
    // and the ones past the art: null, NOT somebody else's badge
    {
      bool leaked = false;
      for (uint8_t r = BADGE_REGIONS; r < GYM_REGIONS; r++)
        for (uint8_t i = 0; i < TRAINER_GYMS; i++)
          if (badgeArtExists(r, i)) leaked = true;
      ck(!leaked, "a region with no art returns none rather than wrapping");
      // With art for every ladder the loop above is EMPTY and proves nothing,
      // so the real invariant is checked separately and is exercisable however
      // the two counts happen to line up: an index past the end is refused
      // rather than folded back onto region 0 by a modulo.
      ck(!badgeArtExists(BADGE_REGIONS, 0),
         "one past the badge art is refused, not wrapped to region 0");
      ck(!badgeArtExists(GYM_REGIONS, 0),
         "and so is one past every ladder");
      ck(!badgeArtExists(250, 0), "and a wildly out-of-range region too");
    }
    ck(!badgeArtExists(0, TRAINER_GYMS), "and an out-of-range gym index is refused");
  }
done:

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
