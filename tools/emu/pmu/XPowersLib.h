// Stand-in for XPowersLib's AXP2101, for pmu_test only: it compiles the REAL
// rtcbat.cpp. What matters is reproduced exactly as upstream does it
// (XPowersAXP2101.hpp, XPowersCommon.hpp):
//
//   int readRegister(reg)          -> -1 on a failed I2C transaction
//   uint8_t statusRegister[3]      <- getIrqStatus() stores that int raw, so a
//                                     failed read becomes 0xFF: EVERY bit set
//   isPekeyShortPressIrq()         -> bit 3 of statusRegister[1], if enabled
//
// rtcbat.cpp keeps its XPowersPMU file-static, so the chip's state lives in
// these globals where a test can reach it without a hook in the firmware.
#pragma once
#include "Arduino.h"
#define AXP2101_SLAVE_ADDRESS 0x34
#define XPOWERS_POWEROFF_4S 0
#define XPOWERS_AXP2101_ALL_IRQ 0xFFFFFFFFUL
#define XPOWERS_AXP2101_PKEY_SHORT_IRQ (1UL << 11)

inline uint8_t g_pmuStatus[3] = { 0, 0, 0 };   // the chip's three INTSTS registers
inline int g_pmuFailNext = 0;                   // this many reads fail next
inline int g_pmuClears = 0;                     // clearIrqStatus() calls

class XPowersPMU {
public:

  bool begin(FakeWire &, uint8_t, int, int) { return true; }
  void setBLDO1Voltage(int) {}
  void enableBLDO1() {}
  bool isBatteryConnect() { return true; }
  int getBatteryPercent() { return 80; }
  bool isCharging() { return false; }
  bool isVbusIn() { return true; }
  void setPowerKeyPressOffTime(int) {}
  void disableIRQ(uint32_t m) { intRegister[0] &= ~(m & 0xFF); intRegister[1] &= ~((m >> 8) & 0xFF); intRegister[2] &= ~((m >> 16) & 0xFF); }
  void enableIRQ(uint32_t m) { intRegister[0] |= m & 0xFF; intRegister[1] |= (m >> 8) & 0xFF; intRegister[2] |= (m >> 16) & 0xFF; }
  void clearIrqStatus() {
    g_pmuClears++;
    for (int i = 0; i < 3; i++) { g_pmuStatus[i] = 0; statusRegister[i] = 0; }
  }
  uint64_t getIrqStatus() {
    statusRegister[0] = readRegister(0);
    statusRegister[1] = readRegister(1);
    statusRegister[2] = readRegister(2);
    return (uint32_t)(statusRegister[2] << 16) | (uint32_t)(statusRegister[1] << 8) |
           (uint32_t)(statusRegister[0]);
  }
  bool isPekeyShortPressIrq() {
    uint8_t mask = XPOWERS_AXP2101_PKEY_SHORT_IRQ >> 8;
    if (intRegister[1] & mask) return (statusRegister[1] & mask) != 0;
    return false;
  }
private:
  uint8_t statusRegister[3] = { 0, 0, 0 };
  uint8_t intRegister[3] = { 0, 0, 0 };
  int readRegister(int reg) {
    if (g_pmuFailNext > 0) { g_pmuFailNext--; return -1; }
    return g_pmuStatus[reg];
  }
};
