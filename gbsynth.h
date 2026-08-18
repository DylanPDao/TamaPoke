#pragma once
#include <stdint.h>
#include <stddef.h>

// A small Game Boy style sound chip.
//
// The old synthesiser was one 50% square wave, one note at a time, blocking --
// which is why everything came out as beeps no matter what notes it was given.
// What the ear recognises as Game Boy music is three things this now has:
//
//   * TWO pulse voices at once, so a tune has a melody AND a bass line instead
//     of one note in isolation.
//   * FOUR duty cycles. 12.5% is thin and reedy, 50% is the fat square. Varying
//     it is most of the character of a lead.
//   * VOLUME ENVELOPES, so a note decays instead of sitting flat until it stops.
//     A flat note is a beep; a decaying one is an instrument.
//
// Plus a noise channel, which is what makes a hit sound like an impact rather
// than another tone.
//
// It deliberately has NO Arduino dependency: it fills a buffer of samples and
// somebody else decides what to do with them. On the board that is the I2S
// task; in the emulator it is a WAV file, which is the only way anyone can hear
// this without flashing a board first.

#define GB_RATE 16000        // samples per second, matching the I2S setup
#define GB_VOICES 3          // pulse 1, pulse 2, noise

// Frequency in the Game Boy's own units: Hz = 131072 / (2048 - f), f < 2048.
// The music and cry data in the pret disassemblies is written in these, so
// keeping them avoids a lossy conversion at build time.
static inline uint32_t gbFreqHz(uint16_t f) {
  return f < 2048 ? (131072u / (2048u - f)) : 0;
}

struct GbVoice {
  uint32_t phase = 0;        // 16.16 fixed point
  uint32_t step = 0;         // phase advance per sample
  uint8_t duty = 2;          // 0..3 -> 12.5 / 25 / 50 / 75 %
  uint8_t vol = 0;           // 0..15, the envelope's current level
  uint8_t envPeriod = 0;     // envelope steps every N/64 s; 0 = hold
  int8_t envDir = 0;         // -1 fades out, +1 fades in, 0 steady
  uint32_t envCount = 0;     // samples until the next envelope step
  uint32_t left = 0;         // samples remaining in this note
  bool noise = false;
  uint16_t lfsr = 0x7FFF;    // the noise channel's shift register
};

class GbSynth {
 public:
  // A pulse note. `ch` is 0 or 1; `ms` is how long it sounds for.
  void note(uint8_t ch, uint16_t gbFreq, uint8_t duty, uint8_t vol,
            int8_t envDir, uint8_t envPeriod, uint32_t ms);
  // Percussion. `period` is samples between clocks of the shift register:
  // small is a bright hiss, large is a low rumble.
  void noise(uint8_t vol, int8_t envDir, uint8_t envPeriod, uint32_t ms,
             uint16_t period);
  void silence(uint8_t ch);
  void allOff();
  bool busy() const;

  // Fills `n` MONO samples. Master is 0..10, matching the settings screen.
  void render(int16_t *out, size_t n, uint8_t master);

 private:
  GbVoice v[GB_VOICES];
};
