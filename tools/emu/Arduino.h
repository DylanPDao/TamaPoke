// Arduino runtime shim for the desktop emulator.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <chrono>

#define IRAM_ATTR
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define INPUT_PULLUP 2
#define INPUT 0
#define OUTPUT 1
#define FALLING 2
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

inline void *ps_malloc(size_t n) { return malloc(n); }

uint32_t millis();          // clock.cpp
void emuSetTimeScale(uint32_t s);
uint32_t emuTimeScale();
inline void delay(uint32_t ms) {
  // the emulator must keep pumping SDL events, so delay is a no-op; nothing in
  // the sketch relies on it for correctness, only for boot-time settling
  (void)ms;
}
inline void pinMode(int, int) {}
inline int digitalPinToInterrupt(int p) { return p; }

// There is no interrupt controller here, so the handler is kept in a slot and
// the SDL layer raises the pin by hand (emuFireInterrupt) on mouse activity.
// Without this the sketch's INT gate in handleTouch() never opens and touch is
// dead. One slot is enough: the touch INT is the only interrupt the firmware
// attaches. Warn rather than overwrite silently, so a second one is not another
// afternoon of wondering why the panel ignores you.
inline void (*g_isr)() = nullptr;
inline void attachInterrupt(int pin, void (*fn)(), int) {
  if (g_isr && g_isr != fn)
    fprintf(stderr, "emu: attachInterrupt(pin %d) displaced the previous handler\n", pin);
  g_isr = fn;
}
inline void emuFireInterrupt() {
  if (g_isr) g_isr();
}

extern uint32_t g_seed;
inline long random(long n) {
  if (n <= 0) return 0;
  g_seed = g_seed * 1103515245u + 12345u;
  return (long)((g_seed >> 16) % (uint32_t)n);
}
inline long random(long lo, long hi) { return lo + random(hi - lo); }
inline void randomSeed(unsigned long s) { g_seed = (uint32_t)s; }

template <typename T> T min(T a, T b) { return a < b ? a : b; }
template <typename T> T max(T a, T b) { return a > b ? a : b; }

// --- String: only the slice of the API the sketch actually uses ---
class String {
public:
  std::string s;
  String() {}
  String(const char *p) : s(p ? p : "") {}
  String(const std::string &o) : s(o) {}
  void trim() {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
  }
  size_t length() const { return s.size(); }
  bool startsWith(const char *p) const { return s.rfind(p, 0) == 0; }
  String substring(int a) const { return String(a <= (int)s.size() ? s.substr(a) : ""); }
  String substring(int a, int b) const {
    if (a >= (int)s.size() || b <= a) return String("");
    return String(s.substr(a, b - a));
  }
  long toInt() const { return atol(s.c_str()); }
  const char *c_str() const { return s.c_str(); }
  int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  bool operator==(const char *p) const { return s == p; }
  bool operator!=(const char *p) const { return s != p; }
  String operator+(const String &o) const { return String(s + o.s); }
};
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }

// --- Serial: reads real stdin so you can type STATS / IV / EGG at the terminal ---
struct FakeSerial {
  void begin(unsigned long) {}
  void setRxBufferSize(int) {}
  void setTxTimeoutMs(int) {}
  void setTimeout(unsigned long) {}
  int available();
  String readStringUntil(char);
  size_t readBytes(uint8_t *, size_t) { return 0; }
  template <typename... A> void printf(const char *f, A... a) { std::printf(f, a...); fflush(stdout); }
  void println(const char *s = "") { std::printf("%s\n", s); fflush(stdout); }
  void println(int v) { std::printf("%d\n", v); fflush(stdout); }
  void print(const char *s) { std::printf("%s", s); fflush(stdout); }
};
extern FakeSerial Serial;

struct FakeESP {
  uint32_t getFreeHeap() { return 294024; }
  uint32_t getMinFreeHeap() { return 281000; }
  uint32_t getFreePsram() { return 4L * 1024 * 1024; }
  void restart();
};

// The crash breadcrumb (see bootReport() in the sketch). On the board these
// live in RTC memory, which survives a panic reset; here they are ordinary
// globals, which is enough for the tests to exercise the same code.
#define RTC_NOINIT_ATTR
typedef int esp_reset_reason_t;
enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW,
       ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
       ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
extern FakeESP ESP;

struct FakeWire {
  void begin(int, int) {}
  void setTimeOut(int) {}
};
extern FakeWire Wire;
