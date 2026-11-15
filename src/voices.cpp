#include "voices.h"

#include <math.h>

namespace voices {

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr float kSilence = 0.0005f;
constexpr float kMaxStartHz = 6000.0f;  // keeps the top of the pitch range below Nyquist
constexpr float kMaxStep = 0.45f;       // oscillator limit, turns per sample (just under Nyquist)

// --- Sine lookup, linearly interpolated. Far cheaper than sinf() per sample. ---
constexpr int kSineSize = 1024;
float sineTable[kSineSize + 1];
bool sineReady = false;

void buildSineTable() {
  if (sineReady) return;
  for (int i = 0; i <= kSineSize; ++i) sineTable[i] = sinf(kTwoPi * i / kSineSize);
  sineReady = true;
}

// turns may be anything above -4.
inline float sine(float turns) {
  float x = (turns + 4.0f) * kSineSize;
  int i = (int)x;
  float frac = x - i;
  i &= kSineSize - 1;
  return sineTable[i] + (sineTable[i + 1] - sineTable[i]) * frac;
}

// --- Random helpers (xorshift32) ---
uint32_t nextRandom(uint32_t& rng) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}
float unit(uint32_t& rng) { return (nextRandom(rng) >> 8) * (1.0f / 16777216.0f); }
float uniform(uint32_t& rng, float lo, float hi) { return lo + (hi - lo) * unit(rng); }
// Even in ratio rather than in absolute terms: an octave is as likely low as high.
float ratio(uint32_t& rng, float lo, float hi) { return lo * powf(hi / lo, unit(rng)); }
bool chance(uint32_t& rng, float probability) { return unit(rng) < probability; }

// Three different instruments out of the four, in random order.
void pickFamilies(uint32_t& rng, Family out[kVoiceCount]) {
  Family all[kFamilyCount] = {kPew, kNoise, kMetal, kBlip};
  for (int i = kFamilyCount - 1; i > 0; --i) {
    int j = nextRandom(rng) % (i + 1);
    Family tmp = all[i];
    all[i] = all[j];
    all[j] = tmp;
  }
  for (int v = 0; v < kVoiceCount; ++v) out[v] = all[v];
}

void capSweep(DrumParams& p) {
  if (p.baseHz + p.sweepHz > kMaxStartHz) p.sweepHz = kMaxStartHz - p.baseHz;
}

DrumParams randomPew(uint32_t& rng) {
  DrumParams p;
  p.family = kPew;
  p.baseHz = ratio(rng, 60, 900);
  p.sweepHz = p.baseHz * ratio(rng, 0.3f, 10);
  capSweep(p);
  p.sweepSeconds = ratio(rng, 0.008f, 0.150f);
  p.ampSeconds = ratio(rng, 0.020f, 0.350f);
  return p;
}

// From a tight bright tick (hat) to a longer, darker burst with a tone under
// it (snare). The tone body is always there, sometimes barely, so that the
// shape lane has something to bring up.
DrumParams randomNoise(uint32_t& rng) {
  DrumParams p;
  p.family = kNoise;
  p.noiseLevel = 1.0f;
  p.noiseHpHz = ratio(rng, 300, 9000);
  float lpLow = p.noiseHpHz * 2 > 2500 ? p.noiseHpHz * 2 : 2500;
  p.noiseLpHz = lpLow >= 16000 ? 16000 : ratio(rng, lpLow, 16000);
  p.noiseSeconds = ratio(rng, 0.015f, 0.250f);
  p.toneLevel = chance(rng, 0.5f) ? uniform(rng, 0.3f, 0.6f) : 0.06f;
  p.baseHz = ratio(rng, 150, 400);
  p.sweepHz = p.baseHz * uniform(rng, 0.2f, 1.5f);
  p.sweepSeconds = ratio(rng, 0.010f, 0.040f);
  p.ampSeconds = ratio(rng, 0.030f, 0.100f);
  return p;
}

// A non-whole-number ratio between the two oscillators is what makes FM
// sound like struck metal or wood rather than a note.
DrumParams randomMetal(uint32_t& rng) {
  DrumParams p;
  p.family = kMetal;
  p.baseHz = ratio(rng, 200, 1800);
  p.sweepHz = p.baseHz * uniform(rng, 0.0f, 0.3f);
  p.sweepSeconds = ratio(rng, 0.010f, 0.050f);
  p.ampSeconds = ratio(rng, 0.040f, 0.400f);
  p.fmRatio = ratio(rng, 1.2f, 7.3f);
  p.fmIndex = ratio(rng, 0.15f, 1.0f);
  p.fmSeconds = ratio(rng, 0.020f, 0.300f);
  return p;
}

DrumParams randomBlip(uint32_t& rng) {
  DrumParams p;
  p.family = kBlip;
  p.wave = chance(rng, 0.5f) ? kSquare : kTriangle;
  p.toneLevel = p.wave == kSquare ? 0.5f : 1.0f;  // a square is much louder
  p.baseHz = ratio(rng, 150, 1600);
  // Bends down to the note, or up to it.
  p.sweepHz = chance(rng, 0.5f) ? p.baseHz * ratio(rng, 0.05f, 2.0f)
                                : -p.baseHz * uniform(rng, 0.1f, 0.6f);
  capSweep(p);
  p.sweepSeconds = ratio(rng, 0.010f, 0.080f);
  p.ampSeconds = ratio(rng, 0.020f, 0.200f);
  return p;
}

// The small speaker is far louder at high pitches, so high voices are pulled
// back: full level up to 250 Hz, falling to 0.45 at 700 Hz and above.
float gainFor(const DrumParams& p) {
  if (p.family == kNoise) return 0.6f;
  float g = 1.0f - (p.baseHz - 250.0f) / 450.0f * 0.55f;
  g = g > 1.0f ? 1.0f : (g < 0.45f ? 0.45f : g);
  return p.family == kMetal ? g * 0.8f : g;  // FM adds a lot of bright energy
}

// Per-sample multiplier that decays to 1/e in `seconds`.
float decayFor(float seconds) { return expf(-1.0f / (seconds * kSampleRate)); }

}  // namespace

const char* familyName(Family f) {
  static const char* const kNames[kFamilyCount] = {"PEW", "NOISE", "METAL", "BLIP"};
  return kNames[f];
}

const char* laneName(Family f, int lane) {
  static const char* const kNames[kFamilyCount][kLaneCount] = {
      {"PIT", "DEC", "SWP"},  // sweep depth
      {"TON", "DEC", "BDY"},  // brightness; tone body
      {"PIT", "DEC", "FM"},   // FM amount
      {"PIT", "DEC", "BND"},  // bend depth
  };
  return kNames[f][lane];
}

void familiesForSeed(uint32_t seed, Family out[kVoiceCount]) {
  uint32_t rng = seed | 1;
  pickFamilies(rng, out);
}

void Drum::init(const DrumParams& p, uint32_t noiseSeed) {
  family_ = p.family;
  wave_ = p.wave;
  toneLevel_ = p.toneLevel;
  noiseLevel_ = p.noiseLevel;
  baseStep_ = p.baseHz / kSampleRate;
  sweepStep_ = p.sweepHz / kSampleRate;
  sweepDecay_ = decayFor(p.sweepSeconds);
  fmRatio_ = p.fmRatio;
  fmIndex_ = p.fmIndex;
  fmDecay_ = decayFor(p.fmSeconds);
  hpOmega_ = kTwoPi * p.noiseHpHz / kSampleRate;
  lpOmega_ = kTwoPi * p.noiseLpHz / kSampleRate;
  for (int d = -kDecayRange; d <= kDecayRange; ++d) {
    float scale = powf(2.0f, d / 4.0f);
    ampDecays_[d + kDecayRange] = decayFor(p.ampSeconds * scale);
    noiseDecays_[d + kDecayRange] = decayFor(p.noiseSeconds * scale);
  }
  rng_ = noiseSeed | 1;
  amp_ = 0;  // a new sound starts silent
  noiseAmp_ = 0;
  hpX_ = hpY_ = lpY_ = 0;
}

void Drum::trigger(float pitchMul, int decayOffset, float shapeMul, float level) {
  phase_ = 0;
  modPhase_ = 0;
  amp_ = level;
  noiseAmp_ = level;
  sweep_ = 1.0f;
  fmEnv_ = 1.0f;
  pitchMul_ = pitchMul;
  ampDecay_ = ampDecays_[decayOffset + kDecayRange];
  noiseDecay_ = noiseDecays_[decayOffset + kDecayRange];

  // The shape lane drives a different parameter on each instrument.
  sweepMul_ = (family_ == kPew || family_ == kBlip) ? shapeMul : 1.0f;
  toneMul_ = family_ == kNoise ? shapeMul : 1.0f;
  fmMul_ = family_ == kMetal ? shapeMul : 1.0f;

  // One-pole filters whose cutoffs follow the pitch lane.
  hpCoef_ = 1.0f / (1.0f + hpOmega_ * pitchMul);
  float lp = lpOmega_ * pitchMul;
  lpCoef_ = lp > 0.99f ? 0.99f : lp;
}

bool Drum::active() const {
  return amp_ * toneLevel_ * toneMul_ > kSilence || noiseAmp_ * noiseLevel_ > kSilence;
}

float Drum::next() {
  float out = 0;

  if (toneLevel_ > 0) {
    float step = (baseStep_ + sweepStep_ * sweepMul_ * sweep_) * pitchMul_;
    if (step > kMaxStep) step = kMaxStep;
    if (step < 0) step = 0;
    phase_ += step;
    if (phase_ >= 1.0f) phase_ -= 1.0f;

    float osc;
    if (wave_ == kSine) {
      float turns = phase_;
      if (fmIndex_ > 0) {
        modPhase_ += step * fmRatio_;
        modPhase_ -= (int)modPhase_;
        turns += fmIndex_ * fmMul_ * fmEnv_ * sine(modPhase_);
        fmEnv_ *= fmDecay_;
      }
      osc = sine(turns);
    } else if (wave_ == kTriangle) {
      osc = 4.0f * fabsf(phase_ - 0.5f) - 1.0f;
    } else {
      osc = phase_ < 0.5f ? 1.0f : -1.0f;
    }
    out = osc * amp_ * toneLevel_ * toneMul_;
    amp_ *= ampDecay_;
    sweep_ *= sweepDecay_;
  }

  if (noiseLevel_ > 0) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    float white = (int32_t)rng_ * (1.0f / 2147483648.0f);
    lpY_ += lpCoef_ * (white - lpY_);
    hpY_ = hpCoef_ * (hpY_ + lpY_ - hpX_);
    hpX_ = lpY_;
    out += hpY_ * noiseAmp_ * noiseLevel_;
    noiseAmp_ *= noiseDecay_;
  }

  return out;
}

void DrumKit::init(uint32_t seed) {
  buildSineTable();
  uint32_t rng = seed | 1;  // xorshift must not start at 0
  Family families[kVoiceCount];
  pickFamilies(rng, families);

  for (int v = 0; v < kVoiceCount; ++v) {
    DrumParams p;
    switch (families[v]) {
      case kNoise:
        p = randomNoise(rng);
        break;
      case kMetal:
        p = randomMetal(rng);
        break;
      case kBlip:
        p = randomBlip(rng);
        break;
      default:
        p = randomPew(rng);
        break;
    }
    drums_[v].init(p, nextRandom(rng));
    gain_[v] = gainFor(p);
  }

  for (int s = -kPitchRange; s <= kPitchRange; ++s) {
    pitchMuls_[s + kPitchRange] = powf(2.0f, s / 12.0f);
  }
  for (int s = -kShapeRange; s <= kShapeRange; ++s) {
    shapeMuls_[s + kShapeRange] = powf(2.0f, s / 4.0f);
  }
}

void DrumKit::trigger(Voice v, int pitch, int decay, int shape, float level) {
  drums_[v].trigger(pitchMuls_[pitch + kPitchRange], decay, shapeMuls_[shape + kShapeRange],
                    level);
}

bool DrumKit::active() const {
  for (int v = 0; v < kVoiceCount; ++v) {
    if (drums_[v].active()) return true;
  }
  return false;
}

void DrumKit::render(int16_t* out, size_t n) {
  bool on[kVoiceCount];
  for (int v = 0; v < kVoiceCount; ++v) on[v] = drums_[v].active();

  for (size_t i = 0; i < n; ++i) {
    float mix = 0;
    for (int v = 0; v < kVoiceCount; ++v) {
      if (on[v]) mix += drums_[v].next() * gain_[v];
    }

    // Leave headroom for three voices, then hard-limit.
    mix *= 0.5f;
    if (mix > 1.0f) mix = 1.0f;
    if (mix < -1.0f) mix = -1.0f;
    out[i] = (int16_t)(mix * 32767.0f);
  }
}

}  // namespace voices
