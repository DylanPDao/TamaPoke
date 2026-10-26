// What the board says, and shows, as it comes up.
//
// 1. The crash report must still be readable LATER. bootReport() prints it at
//    the very start of setup(), before USB CDC has enumerated -- the port IS the
//    firmware -- and with a TX timeout of 0, so on a real board that line is
//    usually gone, after exactly the crash it exists to explain. HEALTH now
//    repeats it.
// 2. The panel stays dark until there is a frame to show. It was lit at 180
//    straight after gfx->begin(), over an uninitialised PSRAM framebuffer, for
//    the whole of setup() (touch retries, SD mount, offline catch-up).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
uint32_t g_seed=8; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
static std::vector<std::string> gIn; static size_t gAt = 0;
int FakeSerial::available(){ return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char){ return String(gIn[gAt++].c_str()); }
void setup(); void loop(); void handleSerial();
void emuSetResetReason(int r);
extern Arduino_CO5300 *panel;
extern Arduino_Canvas *gfx;
extern uint32_t gCrumbMagic, gCrumbScreen, gCrumbHeap;
extern const char *const SCREEN_NAME[];

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static std::string runConsole(const std::vector<std::string> &lines){
  gIn = lines; gAt = 0;
  char path[] = "/tmp/tamapoke_boot_XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return "";
  fflush(stdout);
  int saved = dup(fileno(stdout));
  dup2(fd, fileno(stdout));
  while (gAt < gIn.size()) handleSerial();
  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  lseek(fd, 0, SEEK_SET);
  std::string out; char b[4096]; ssize_t n;
  while ((n = read(fd, b, sizeof(b))) > 0) out.append(b, (size_t)n);
  close(fd); remove(path);
  return out;
}

int main(){
  // the last run died on the battle screen
  const uint32_t SCR_BATTLE_IDX = 15;
  ck(!strcmp(SCREEN_NAME[SCR_BATTLE_IDX], "battle"), "(index 15 is the battle screen)");
  emuSetResetReason(ESP_RST_PANIC);
  gCrumbMagic = 0x7AABE10C; gCrumbScreen = SCR_BATTLE_IDX; gCrumbHeap = 123456;

  setup();
  ck(panel->brightness == 0, "after setup() the panel is still DARK");
  bool black = true;
  for (uint16_t px : gfx->fb) if (px) { black = false; break; }
  ck(black, "and what it holds is a black frame, not whatever PSRAM had");
  loop();
  ck(panel->brightness > 0, "the first loop() lights it");
  bool drawn = false;
  for (uint16_t px : gfx->fb) if (px) { drawn = true; break; }
  ck(drawn, "in the same loop() that draws the first frame");

  std::string out = runConsole({ "HEALTH" });
  ck(out.find("last boot: reset=PANIC") != std::string::npos, "HEALTH repeats why the last run ended");
  ck(out.find("crash on 'battle' heap=123456") != std::string::npos,
     "and where it died, long after the boot line was lost");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
