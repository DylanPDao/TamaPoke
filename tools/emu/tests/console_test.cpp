// EXPORT and IMPORT as a player would actually use them: the hex really goes
// out through the console and really comes back in through it. save_test covers
// the blob; this covers the glue around it, which is where a paste breaks.
//
// The point of the format is that EXPORT prints the exact commands that restore
// it, so this test does literally nothing clever -- it feeds the output back in
// line for line.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;

// scripted console input
static std::vector<std::string> gIn;
static size_t gAt = 0;
int FakeSerial::available(){ return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char){
  if (gAt >= gIn.size()) return String("");
  return String(gIn[gAt++].c_str());
}
static bool gRestarted = false;
void FakeESP::restart(){ gRestarted = true; }   // not exit(): the test continues

void setup(); void render(); void handleSerial();
extern Pet pet;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// Runs the queued lines through the console with stdout captured, and returns
// everything it printed.
static std::string runConsole(const std::vector<std::string> &lines){
  gIn = lines; gAt = 0;
  char path[] = "/tmp/tamapoke_console_XXXXXX";
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
  std::string out;
  char b[4096]; ssize_t n;
  while ((n = read(fd, b, sizeof(b))) > 0) out.append(b, (size_t)n);
  close(fd);
  remove(path);
  return out;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(59,false);
  pet.ageMinutes = 61UL*MINUTES_PER_LEVEL;
  pet.ivAtk=29; pet.ivHp=13;
  pet.badges = 0x0055;
  pet.streak = 8;
  for (int i=0;i<3;i++){ PartyMon m; m.dex=100+i; m.level=44+i;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=17; party.replaceAt(i,m); }
  pet.renameTrainer("ASH");        // persists everything above

  std::string out = runConsole({"EXPORT"});
  ck(out.find("# TamaPoke save") != std::string::npos, "EXPORT says what it is");

  // every IMPORT line it printed, in order
  std::vector<std::string> back;
  size_t at = 0;
  while (at < out.size()) {
    size_t nl = out.find('\n', at);
    if (nl == std::string::npos) nl = out.size();
    std::string ln = out.substr(at, nl - at);
    if (ln.rfind("IMPORT", 0) == 0) back.push_back(ln);
    at = nl + 1;
  }
  ck(back.size() > 2, "and prints the save as a block of IMPORT commands");
  ck(back.back() == "IMPORT", "the last of which commits");
  printf("      (%u lines)\n", (unsigned)back.size());

  // --- wipe, then paste the block straight back
  pet.factoryReset();
  Pet gone; gone.begin();
  ck(strcmp(gone.trainerName,"ASH") != 0, "the wipe really took the save");

  gRestarted = false;
  std::string res = runConsole(back);
  ck(res.find("IMPORT OK") != std::string::npos, "pasting the block back is accepted");
  ck(gRestarted, "and it reboots, as a restore has to");

  Pet p2; Party q2;
  p2.begin(); q2.begin();
  ck(!strcmp(p2.trainerName,"ASH"), "the trainer name came back");
  ck(p2.speciesId==59 && p2.ageMinutes==61UL*MINUTES_PER_LEVEL,
     "so did the creature and its age");
  ck(p2.ivAtk==29 && p2.ivHp==13 && p2.badges==0x0055 && p2.streak==8,
     "and the IVs, badges and streak");
  ck(q2.count()==3 && q2.slots[1].dex==101 && q2.slots[1].level==45,
     "and the party");

  // --- a mistyped paste must not be applied
  {
    std::vector<std::string> mangled = back;
    // corrupt one hex digit in the middle of the block
    std::string &ln = mangled[mangled.size()/2];
    size_t k = ln.size()/2;
    ln[k] = (ln[k] == 'A') ? 'B' : 'A';
    gRestarted = false;
    std::string r2 = runConsole(mangled);
    ck(r2.find("IMPORT REJECTED") != std::string::npos,
       "a single mistyped digit is rejected");
    ck(!gRestarted, "and nothing reboots on a rejection");
    Pet p3; p3.begin();
    ck(!strcmp(p3.trainerName,"ASH"), "the save it was pasted over is untouched");
  }
  {
    // an odd number of digits is a truncated paste, not a valid blob
    gRestarted = false;
    std::string r3 = runConsole({"IMPORT 544B5053AB", "IMPORT 1", "IMPORT"});
    ck(r3.find("IMPORT ODD") != std::string::npos, "a half-copied line is caught");
    ck(!gRestarted, "and does not reboot either");
  }
  {
    // non-hex rubbish
    std::string r4 = runConsole({"IMPORT zzzz", "IMPORT"});
    ck(r4.find("IMPORT BAD") != std::string::npos, "so is a line that is not hex");
  }
  {
    std::string r5 = runConsole({"IMPORT"});
    ck(r5.find("IMPORT EMPTY") != std::string::npos,
       "committing nothing says so rather than wiping the save");
    Pet p4; p4.begin();
    ck(!strcmp(p4.trainerName,"ASH"), "and the save is still there");
  }

  // TR sets the training, and is bounded by the same IV ceiling the game is --
  // a debug command that could exceed trMaxFor() would let a test (or a curious
  // player) build a creature the balance table says cannot exist.
  {
    pet.ivAtk = 31; pet.ivDef = 8; pet.ivSpe = 31;
    pet.trAtk = pet.trDef = pet.trSpe = 0;
    std::string out = runConsole({"TR 100 100 100"});
    ck(pet.trAtk == pet.trMaxAtk(), "TR fills the training to the cap");
    ck(pet.trDef == pet.trMaxDef() && pet.trDef < 100,
       "and a low IV still gets its lower ceiling, not 100");
    ck(out.find("tr=") != std::string::npos, "and it reports what it set");
    runConsole({"TR 0"});
    ck(pet.trAtk == 0 && pet.trDef == 0 && pet.trSpe == 0,
       "one argument sets all three, so TR 0 clears them");
    Pet again; again.begin();
    ck(again.trAtk == 0, "and the change is persisted rather than lost on reload");
    runConsole({"TR -5 999 20"});
    ck(pet.trAtk == 0 && pet.trDef == pet.trMaxDef() && pet.trSpe == 20,
       "nonsense arguments clamp instead of wrapping");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
