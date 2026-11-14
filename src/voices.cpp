#include "voices.h"

#include <math.h>

namespace voices {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kSilence = 0.0005f;

struct Range {
  float lo, hi;
};

// Each voice gets a random tuning from its own band. The bands do not overlap
// in pitch, so the three stay distinct and LOW/MID/HI stay true.
struct TomRanges {
  Range baseHz, sweepHz, sweepSeconds, ampSeconds;
};
const TomRanges kTomRanges[kVoiceCount] = {
    {{70, 160}, {200, 900}, {0.040f, 0.140f}, {0.15f, 0.35f}},     // low: deep, long
    {{190, 400}, {600, 2200}, {0.030f, 0.090f}, {0.08f, 0.20f}},   // mid: the "pew"
    {{480, 1100}, {1200, 3200}, {0.008f, 0.025f}, {0.02f, 0.05f}}, // high: very short blip
};

// xorshift32 mapped onto a range
float pick(uint32_t& rng, Range r) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return r.lo + (r.hi - r.lo) * (rng >> 8) * (1.0f / 16777216.0f);
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
    const TomRanges& r = kTomRanges[v];
    TomParams p;
    p.baseHz = pick(rng, r.baseHz);
    p.sweepHz = pick(rng, r.sweepHz);
    p.sweepSeconds = pick(rng, r.sweepSeconds);
    p.ampSeconds = pick(rng, r.ampSeconds);
    toms_[v].init(p);
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
