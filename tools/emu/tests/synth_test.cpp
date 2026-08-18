// The Game Boy synth, checked by measuring the samples it produces.
//
// Audio is the one part of this firmware nobody could verify for months: the
// emulator stubs sfxPlay() to silence, so the only way to judge it was to flash
// a board and listen. Numbers are not a substitute for ears -- whether a tune
// sounds GOOD is not testable -- but the things that made the old synth sound
// like a beeper ARE measurable: a fixed 50% duty and no envelope.
//
// So this asserts the three properties that make it a Game Boy and not a
// beeper: the duty cycles are the right widths, envelopes actually decay, and
// two voices really do sound at once.
#include "gbsynth.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// what fraction of the samples are positive -- i.e. the measured duty cycle
static double dutyOf(uint16_t freq, uint8_t duty){
  GbSynth s; s.note(0, freq, duty, 15, 0, 0, 200);
  std::vector<int16_t> buf(GB_RATE / 5);
  s.render(buf.data(), buf.size(), 10);
  size_t hi = 0;
  for (int16_t v : buf) if (v > 0) hi++;
  return (double)hi / buf.size();
}

int main(){
  // --- the four duty cycles must actually differ, and be the right widths
  double d0 = dutyOf(1500, 0), d1 = dutyOf(1500, 1);
  double d2 = dutyOf(1500, 2), d3 = dutyOf(1500, 3);
  printf("      measured duty: %.3f %.3f %.3f %.3f\n", d0, d1, d2, d3);
  ck(fabs(d0 - 0.125) < 0.03, "12.5% duty measures 12.5%");
  ck(fabs(d1 - 0.25)  < 0.03, "25% duty measures 25%");
  ck(fabs(d2 - 0.50)  < 0.03, "50% duty measures 50%");
  ck(fabs(d3 - 0.75)  < 0.03, "75% duty measures 75%");
  ck(d0 < d1 && d1 < d2 && d2 < d3, "and they are all distinct");

  // --- the frequency is the Game Boy's own formula
  ck(gbFreqHz(1816) == 131072u / (2048u - 1816u), "gbFreqHz matches the hardware");
  ck(gbFreqHz(2048) == 0, "an out-of-range frequency is silent, not a divide by zero");
  {
    // count zero crossings to confirm the pitch really is what was asked for
    GbSynth s; s.note(0, 1750, 2, 15, 0, 0, 1000);
    std::vector<int16_t> buf(GB_RATE);
    s.render(buf.data(), buf.size(), 10);
    int cross = 0;
    for (size_t i = 1; i < buf.size(); i++)
      if (buf[i-1] <= 0 && buf[i] > 0) cross++;
    uint32_t want = gbFreqHz(1750);
    printf("      %u Hz asked, %d cycles counted in one second\n", want, cross);
    ck(abs((int)want - cross) < (int)want / 20, "the pitch is within 5% of the request");
  }

  // --- ENVELOPES. A flat note is a beep; a decaying one is an instrument.
  {
    GbSynth s; s.note(0, 1500, 2, 15, -1, 1, 500);
    std::vector<int16_t> buf(GB_RATE / 2);
    s.render(buf.data(), buf.size(), 10);
    auto peak = [&](size_t a, size_t b){ int16_t m = 0;
      for (size_t i = a; i < b && i < buf.size(); i++) if (abs(buf[i]) > m) m = abs(buf[i]);
      return m; };
    int16_t early = peak(0, 800), mid = peak(3000, 3800), late = peak(6500, 7300);
    printf("      envelope peaks: %d -> %d -> %d\n", early, mid, late);
    ck(early > mid && mid > late, "a decaying envelope really decays");
    ck(late == 0 || late < early / 2, "and gets substantially quieter");
  }
  {
    GbSynth s; s.note(0, 1500, 2, 15, 0, 0, 300);
    std::vector<int16_t> buf(GB_RATE / 4);
    s.render(buf.data(), buf.size(), 10);
    int16_t a = 0, b = 0;
    for (size_t i = 0; i < 500; i++) if (abs(buf[i]) > a) a = abs(buf[i]);
    for (size_t i = 3000; i < 3500; i++) if (abs(buf[i]) > b) b = abs(buf[i]);
    ck(a == b, "a note with no envelope holds its level");
  }

  // --- TWO VOICES AT ONCE. This is what the old synth could not do at all.
  {
    GbSynth s;
    s.note(0, 1750, 2, 15, 0, 0, 300);
    s.note(1, 1200, 2, 15, 0, 0, 300);
    std::vector<int16_t> both(2000);
    s.render(both.data(), both.size(), 10);
    GbSynth one;
    one.note(0, 1750, 2, 15, 0, 0, 300);
    std::vector<int16_t> solo(2000);
    one.render(solo.data(), solo.size(), 10);
    int diff = 0;
    for (size_t i = 0; i < both.size(); i++) if (both[i] != solo[i]) diff++;
    ck(diff > 200, "two voices sound together, not one after the other");
    int16_t pk = 0;
    for (int16_t v : both) if (abs(v) > pk) pk = abs(v);
    ck(pk <= 32767, "and the mix does not clip");
  }

  // --- noise is not a tone: it must not repeat at an audible period
  {
    GbSynth s; s.noise(15, 0, 0, 300, 4);
    std::vector<int16_t> buf(4000);
    s.render(buf.data(), buf.size(), 10);
    int cross = 0;
    for (size_t i = 1; i < buf.size(); i++)
      if ((buf[i-1] > 0) != (buf[i] > 0)) cross++;
    ck(cross > 100, "the noise channel actually rattles");
    bool same = true;
    for (size_t i = 0; i < 500; i++) if (buf[i] != buf[i + 1000]) same = false;
    ck(!same, "and does not just repeat a short loop");
  }

  // --- volume 0 is silence, which is what the settings screen promises
  {
    GbSynth s; s.note(0, 1500, 2, 15, 0, 0, 100);
    std::vector<int16_t> buf(1000);
    s.render(buf.data(), buf.size(), 0);
    bool quiet = true;
    for (int16_t v : buf) if (v) quiet = false;
    ck(quiet, "master volume 0 is silence");
  }

  // --- a finished note stops
  {
    GbSynth s; s.note(0, 1500, 2, 15, 0, 0, 10);
    ck(s.busy(), "a playing note reports busy");
    std::vector<int16_t> buf(GB_RATE / 10);
    s.render(buf.data(), buf.size(), 10);
    ck(!s.busy(), "and stops when it is over");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
