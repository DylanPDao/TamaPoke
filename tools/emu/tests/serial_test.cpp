// The console must never stall the game waiting for the rest of a line.
//
// handleSerial() used Serial.readStringUntil('\n'), whose default timeout is
// 1 s: a terminal that sends keystrokes as they are typed (most do) froze the
// whole loop -- touch, animation, audio pacing -- for up to a second per
// keystroke. It now gathers bytes as they come and dispatches on '\n'.
//
// Two things it must keep exactly:
//   * the byte boundary. PUT's payload follows its header line on the same
//     stream and is read by sdSerialCommand() with Serial.readBytes(), so not
//     one byte past the '\n' may be swallowed into the line buffer.
//   * a line with no '\n' at all still runs once the sender goes quiet for a
//     second, which is what the old timeout did for a terminal set to send no
//     line ending.
//
// The emulator's FakeSerial replays each test's lines byte by byte through
// read(); `raw` lets this test feed exact bytes, partial lines included.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <unistd.h>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){ return 0; }
String FakeSerial::readStringUntil(char){ return String(""); }
void FakeESP::restart(){}
void setup(); void handleSerial();
extern Pet pet;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// one handleSerial() call with stdout captured; returns what it printed and
// how long it took
static std::string once(double *ms){
  char path[] = "/tmp/tamapoke_serial_XXXXXX";
  int fd = mkstemp(path);
  fflush(stdout);
  int saved = dup(fileno(stdout));
  dup2(fd, fileno(stdout));
  auto t0 = std::chrono::steady_clock::now();
  handleSerial();
  auto t1 = std::chrono::steady_clock::now();
  fflush(stdout);
  dup2(saved, fileno(stdout));
  close(saved);
  lseek(fd, 0, SEEK_SET);
  std::string out; char b[4096]; ssize_t n;
  while ((n = read(fd, b, sizeof(b))) > 0) out.append(b, (size_t)n);
  close(fd); remove(path);
  if (ms) *ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return out;
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);

  // a half-typed command: returns at once and runs nothing
  Serial.raw = "STA";
  double ms = 0;
  std::string out = once(&ms);
  printf("     a partial line took %.2f ms\n", ms);
  ck(ms < 50, "a partial line does not block the loop");
  ck(out.find("spec=") == std::string::npos, "and is not run yet");
  ck(Serial.raw.empty(), "its bytes are taken into the line buffer");
  // the rest arrives: now it runs, whole
  Serial.raw = "TS\r\n";
  out = once(nullptr);
  ck(out.find("spec=") != std::string::npos, "the completed line runs as STATS");

  // the byte boundary PUT depends on: nothing after the '\n' is consumed
  Serial.raw = std::string("HEALTH\n") + "\x01\x02PAYLOAD";
  out = once(nullptr);
  ck(out.find("psram=") != std::string::npos, "HEALTH runs");
  // the soak test reads these: saves for the write rate, nvs for the heap steps
  ck(out.find("saves=") != std::string::npos && out.find(" nvs=") != std::string::npos,
     "and reports the save count and NVS entry use");
  ck(Serial.raw == std::string("\x01\x02PAYLOAD"),
     "and every byte after its '\\n' is still unread, for readBytes() to take");
  Serial.raw.clear();

  // one line per call, as before: two queued lines take two passes
  Serial.raw = "HEALTH\nSTATS\n";
  out = once(nullptr);
  ck(out.find("psram=") != std::string::npos && out.find("spec=") == std::string::npos,
     "the first pass runs only the first line");
  out = once(nullptr);
  ck(out.find("spec=") != std::string::npos, "the second runs the next");

  // no line ending at all: runs once the sender has been quiet for a second
  Serial.raw = "STATS";
  out = once(nullptr);
  ck(out.find("spec=") == std::string::npos, "a line with no ending waits while it may still be typed");
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  out = once(nullptr);
  ck(out.find("spec=") != std::string::npos, "and runs after a second of silence, as the old timeout did");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
