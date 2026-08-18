#include "gbsynth.h"

// Duty patterns as a threshold on the phase: a pulse is high while the phase is
// below the threshold. 12.5 / 25 / 50 / 75 %, the Game Boy's four.
static const uint32_t DUTY[4] = {
  0x2000u,   // 12.5%  -- thin and reedy, the classic lead
  0x4000u,   // 25%
  0x8000u,   // 50%    -- the fat square
  0xC000u,   // 75%    (sounds like 25% inverted, as on hardware)
};

// The envelope ticks at 64 Hz on real hardware, so one step is this many
// samples. Keeping the real rate matters: it is why a decay sounds like a Game
// Boy decay rather than an arbitrary fade.
#define ENV_SAMPLES (GB_RATE / 64)

void GbSynth::note(uint8_t ch, uint16_t gbFreq, uint8_t duty, uint8_t vol,
                   int8_t envDir, uint8_t envPeriod, uint32_t ms) {
  if (ch >= 2) return;
  GbVoice &g = v[ch];
  uint32_t hz = gbFreqHz(gbFreq);
  g.noise = false;
  g.step = hz ? (uint32_t)((uint64_t)hz * 65536u / GB_RATE) : 0;
  g.duty = duty & 3;
  g.vol = vol > 15 ? 15 : vol;
  g.envDir = envDir;
  g.envPeriod = envPeriod;
  g.envCount = envPeriod ? (uint32_t)envPeriod * ENV_SAMPLES : 0;
  g.left = (uint32_t)((uint64_t)ms * GB_RATE / 1000);
  g.phase = 0;
}

void GbSynth::noise(uint8_t vol, int8_t envDir, uint8_t envPeriod, uint32_t ms,
                    uint16_t period) {
  GbVoice &g = v[2];
  g.noise = true;
  g.lfsr = 0x7FFF;
  // `period` is how many samples pass between clocks of the shift register:
  // small is a bright hiss, large is a low rumble.
  if (!period) period = 4;
  g.step = 65536u / period;
  g.vol = vol > 15 ? 15 : vol;
  g.envDir = envDir;
  g.envPeriod = envPeriod;
  g.envCount = envPeriod ? (uint32_t)envPeriod * ENV_SAMPLES : 0;
  g.left = (uint32_t)((uint64_t)ms * GB_RATE / 1000);
  g.phase = 0;
}

void GbSynth::silence(uint8_t ch) {
  if (ch < GB_VOICES) { v[ch].left = 0; v[ch].vol = 0; }
}

void GbSynth::allOff() {
  for (int i = 0; i < GB_VOICES; i++) silence((uint8_t)i);
}

bool GbSynth::busy() const {
  for (int i = 0; i < GB_VOICES; i++)
    if (v[i].left) return true;
  return false;
}

void GbSynth::render(int16_t *out, size_t n, uint8_t master) {
  if (master > 10) master = 10;
  // Headroom for three voices at once: each can reach 15, and the sum is
  // divided so a full chord cannot clip. 1500 per unit of master keeps a single
  // voice comfortably audible without the mix distorting.
  const int32_t scale = (int32_t)master * 150;
  for (size_t i = 0; i < n; i++) {
    int32_t mix = 0;
    for (int c = 0; c < GB_VOICES; c++) {
      GbVoice &g = v[c];
      if (!g.left) continue;

      int32_t s;
      if (g.noise) {
        // 15-bit LFSR, exactly as the hardware does it: xor the bottom two bits
        // back into the top. This is the sound of every impact and every faint.
        g.phase += g.step;
        while (g.phase >= 0x10000u) {
          g.phase -= 0x10000u;
          uint16_t x = (uint16_t)((g.lfsr ^ (g.lfsr >> 1)) & 1);
          g.lfsr = (uint16_t)((g.lfsr >> 1) | (x << 14));
        }
        s = (g.lfsr & 1) ? 1 : -1;
      } else if (g.step) {
        g.phase = (g.phase + g.step) & 0xFFFFu;
        s = (g.phase < DUTY[g.duty]) ? 1 : -1;
      } else {
        s = 0;
      }
      mix += s * (int32_t)g.vol;

      // envelope
      if (g.envPeriod && g.envDir) {
        if (g.envCount && --g.envCount == 0) {
          int nv = (int)g.vol + g.envDir;
          if (nv < 0) nv = 0;
          if (nv > 15) nv = 15;
          g.vol = (uint8_t)nv;
          g.envCount = (uint32_t)g.envPeriod * ENV_SAMPLES;
        }
      }
      g.left--;
    }
    int32_t o = mix * scale;
    if (o > 32767) o = 32767;
    if (o < -32768) o = -32768;
    out[i] = (int16_t)o;
  }
}
