// In-memory NVS, persisted to a file so the pet survives between runs.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

typedef std::map<std::string, std::vector<uint8_t>> NvsStore;
inline NvsStore &nvs() { static NvsStore s; return s; }
void nvsLoad(const char *path);
void nvsSave(const char *path);

class Preferences {
public:
  NvsStore &kv = nvs();
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  void clear() { kv.clear(); }
  bool isKey(const char *k) { return kv.count(k) != 0; }

  template <typename T> void putT(const char *k, T v) {
    std::vector<uint8_t> b(sizeof(T));
    memcpy(b.data(), &v, sizeof(T));
    kv[k] = b;
  }
  template <typename T> T getT(const char *k, T d) {
    auto it = kv.find(k);
    if (it == kv.end() || it->second.size() != sizeof(T)) return d;
    T v; memcpy(&v, it->second.data(), sizeof(T)); return v;
  }
  void putUChar(const char *k, uint8_t v) { putT(k, v); }
  uint8_t getUChar(const char *k, uint8_t d = 0) { return getT(k, d); }
  void putChar(const char *k, int8_t v) { putT(k, v); }
  int8_t getChar(const char *k, int8_t d = 0) { return getT(k, d); }
  void putBool(const char *k, bool v) { putT(k, v); }
  bool getBool(const char *k, bool d = false) { return getT(k, d); }
  void putUInt(const char *k, uint32_t v) { putT(k, v); }
  uint32_t getUInt(const char *k, uint32_t d = 0) { return getT(k, d); }
  void putShort(const char *k, int16_t v) { putT(k, v); }
  int16_t getShort(const char *k, int16_t d = 0) { return getT(k, d); }
  void putUShort(const char *k, uint16_t v) { putT(k, v); }
  uint16_t getUShort(const char *k, uint16_t d = 0) { return getT(k, d); }
  void putBytes(const char *k, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    kv[k] = std::vector<uint8_t>(b, b + n);
  }
  size_t getBytes(const char *k, void *p, size_t n) {
    auto it = kv.find(k);
    if (it == kv.end()) return 0;
    size_t c = it->second.size() < n ? it->second.size() : n;
    memcpy(p, it->second.data(), c);
    return c;
  }
  void putString(const char *k, const char *v) {
    kv[k] = std::vector<uint8_t>(v, v + strlen(v) + 1);
  }
  size_t getString(const char *k, char *out, size_t n) {
    auto it = kv.find(k);
    if (it == kv.end()) { if (n) out[0] = 0; return 0; }
    size_t c = it->second.size() < n ? it->second.size() : n - 1;
    memcpy(out, it->second.data(), c);
    out[c ? c - 1 : 0] = 0;
    return c;
  }
};
