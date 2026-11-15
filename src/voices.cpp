#include "voices.h"

#include <math.h>

namespace voices {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kSilence = 0.0005f;

struct Range {
  float lo, hi;
};

// Every voice draws from the same wide ranges, so any of them can be anything
// from a deep boom to a tiny blip.
const Range kBaseHz = {60.0f, 900.0f};
const Range kSweepRatio = {0.3f, 10.0f};  // sweep depth as a multiple of the base
const Range kSweepSeconds = {0.008f, 0.150f};
const Range kAmpSeconds = {0.020f, 0.350f};
constexpr float kMaxStartHz = 6000.0f;  // keeps +12 semitones below Nyquist

// xorshift32 mapped onto a range, evenly in ratio rather than in absolute
// terms: an octave is as likely to be low as high.
float pick(uint32_t& rng, Range r) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  float u = (rng >> 8) * (1.0f / 16777216.0f);
  return r.lo * powf(r.hi / r.lo, u);
}

// The small speaker is far louder at high pitches, so high voices are pulled
// back: full level up to 250 Hz, falling to 0.45 at 700 Hz and above.
float gainFor(float baseHz) {
  float g = 1.0f - (baseHz - 250.0f) / 450.0f * 0.55f;
  return g > 1.0f ? 1.0f : (g < 0.45f ? 0.45f : g);
}

// Per-sample multiplier that decays to 1/e in `seconds`.
float decayFor(float seconds) { return expf(-1.0f / (seconds * kSampleRate)); }

}  // namespace

void Tom::init(const TomParams& p) {
  baseStep_ = kTwoPi * p.baseHz / kSampleRate;
  sweepStep_ = kTwoPi * p.sweepHz / kSampleRate;
  sweepDecay_ = decayFor(p.sweepSeconds);
  for (int d = -kDecayRange; d <= kDecayRange; ++d) {
    ampDecays_[d + kDecayRange] = decayFor(p.ampSeconds * powf(2.0f, d / 4.0f));
  }
  ampDecay_ = ampDecays_[kDecayRange];
}

void Tom::trigger(float pitchMul, int decayOffset) {
  phase_ = 0;
  amp_ = 1.0f;
  sweep_ = 1.0f;
  pitchMul_ = pitchMul;
  ampDecay_ = ampDecays_[decayOffset + kDecayRange];
}

bool Tom::active() const { return amp_ > kSilence; }

float Tom::next() {
  phase_ += (baseStep_ + sweepStep_ * sweep_) * pitchMul_;
  if (phase_ > kTwoPi) phase_ -= kTwoPi;
  float s = sinf(phase_) * amp_;
  amp_ *= ampDecay_;
  sweep_ *= sweepDecay_;
  return s;
}

void DrumKit::init(uint32_t seed) {
  uint32_t rng = seed | 1;  // xorshift must not start at 0
  for (int v = 0; v < kVoiceCount; ++v) {
    TomParams p;
    p.baseHz = pick(rng, kBaseHz);
    p.sweepHz = p.baseHz * pick(rng, kSweepRatio);
    if (p.baseHz + p.sweepHz > kMaxStartHz) p.sweepHz = kMaxStartHz - p.baseHz;
    p.sweepSeconds = pick(rng, kSweepSeconds);
    p.ampSeconds = pick(rng, kAmpSeconds);
    toms_[v].init(p);
    gain_[v] = gainFor(p.baseHz);
  }
  for (int s = -kPitchRange; s <= kPitchRange; ++s) {
    pitchMuls_[s + kPitchRange] = powf(2.0f, s / 12.0f);
  }
}

void DrumKit::trigger(Voice v, int pitch, int decay) {
  toms_[v].trigger(pitchMuls_[pitch + kPitchRange], decay);
}

void DrumKit::render(int16_t* out, size_t n) {
  bool on[kVoiceCount];
  for (int v = 0; v < kVoiceCount; ++v) on[v] = toms_[v].active();

  for (size_t i = 0; i < n; ++i) {
    float mix = 0;
    for (int v = 0; v < kVoiceCount; ++v) {
      if (on[v]) mix += toms_[v].next() * gain_[v];
    }

    // Leave headroom for three voices, then hard-limit.
    mix *= 0.5f;
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;
    out[i] = (int16_t)(mix * 32767.0f);
  }
}

}  // namespace voices
