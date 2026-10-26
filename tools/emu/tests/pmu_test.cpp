// A failed I2C read of the PMU's IRQ status must not count as a PWR press.
//
// XPowersLib's getIrqStatus() stores readRegister()'s int straight into a
// uint8_t array, and readRegister() returns -1 when the I2C transaction fails.
// -1 is 0xFF: EVERY IRQ bit set, the PWR short press among them. So a single
// failed read on the bus the touch controller shares flipped screenOff -- the
// panel went black until it was touched, and at night pet.setScreenOff() put
// the creature to sleep. Verified against the library source; how often the
// bus actually fails on a board is what HEALTH's pmu_err= now counts.
//
// This compiles the REAL rtcbat.cpp against stand-ins that reproduce that
// library behaviour (tools/emu/pmu/), so it tests the caller, not a copy.
#include "Arduino.h"
#include "XPowersLib.h"
#include "rtcbat.h"
#include <cstdio>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
uint32_t millis(){ static uint32_t t = 0; return t += 3000; }   // past the 2 s power cache
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  ck(batBegin(), "the PMU comes up");
  pwrSetup();

  // the mechanism: a real press IS seen, or the checks below prove nothing
  g_pmuStatus[1] = 0x08;                       // PKEY short press latched
  ck(pwrShortPressed(), "a real short press is reported");
  ck(g_pmuStatus[1] == 0, "and acknowledged (status cleared)");
  ck(!pwrShortPressed(), "once only");

  // a failed read of every status register: 0xFF in the library's array
  uint32_t errs0 = pmuReadErrors();
  int clears0 = g_pmuClears;
  g_pmuFailNext = 3;
  ck(!pwrShortPressed(), "a failed I2C read is NOT a press");
  ck(pmuReadErrors() == errs0 + 1, "and is counted for HEALTH");
  ck(g_pmuClears == clears0, "and nothing is acknowledged on the strength of it");

  // a failure part-way through the burst: registers 0 and 1 fail, 2 reads
  g_pmuFailNext = 0;
  g_pmuStatus[1] = 0;
  ck(!pwrShortPressed(), "(an idle bus reads clean)");
  g_pmuFailNext = 2;
  ck(!pwrShortPressed(), "a partly failed read is not a press either");

  // and a press that arrives after a failed read is still seen, not swallowed
  g_pmuFailNext = 0;
  g_pmuStatus[1] = 0x08;
  ck(pwrShortPressed(), "a real press after a failed read still gets through");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
