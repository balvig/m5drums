#include "voices.h"

#include <math.h>
#include <string.h>

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
// Starting state for the generator from a kit seed. Scrambles the seed so
// that neighbouring seeds give unrelated kits, and every seed its own: the
// steps are all reversible, so different seeds give different states. (The
// one seed that would give 0, which xorshift cannot start from, shares a
// state with one other.)
uint32_t stateForSeed(uint32_t seed) {
  uint32_t h = seed + 0x9E3779B9u;
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h ? h : 0x9E3779B9u;
}

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

// Three different instruments, in random order, from the analog-drum-machine
// style ones. Left out of the draw for now: PLUCK and ACID (pitched), and
// METAL and COWBELL (too metallic).
void pickFamilies(uint32_t& rng, Family out[kVoiceCount]) {
  Family all[] = {kPew, kNoise, kBlip, kKick, kClap, kTom, kSnare, kRim};
  const int count = sizeof(all) / sizeof(all[0]);
  for (int i = count - 1; i > 0; --i) {
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

// A low thump: the pitch drops very fast onto a bass note, a short burst of
// noise gives the beater click, and drive thickens it.
DrumParams randomKick(uint32_t& rng) {
  DrumParams p;
  p.family = kKick;
  p.baseHz = ratio(rng, 38, 70);
  p.sweepHz = p.baseHz * ratio(rng, 2.5f, 9.0f);
  p.sweepSeconds = ratio(rng, 0.012f, 0.045f);
  p.ampSeconds = ratio(rng, 0.120f, 0.450f);
  p.drive = ratio(rng, 1.0f, 5.0f);
  p.noiseLevel = uniform(rng, 0.05f, 0.25f);
  p.noiseSeconds = ratio(rng, 0.002f, 0.006f);
  p.noiseHpHz = 1500;
  p.noiseLpHz = 9000;
  return p;
}

// Several quick bursts of mid-range noise, like hands not quite landing
// together, then a short tail.
DrumParams randomClap(uint32_t& rng) {
  DrumParams p;
  p.family = kClap;
  p.toneLevel = 0;
  p.noiseLevel = 1.0f;
  p.noiseHpHz = ratio(rng, 600, 1600);
  p.noiseLpHz = ratio(rng, 2500, 7000);
  p.noiseSeconds = ratio(rng, 0.060f, 0.250f);
  p.burstCount = 2 + (int)(nextRandom(rng) % 3);  // 2-4 before the tail
  p.burstGapSeconds = uniform(rng, 0.007f, 0.016f);
  p.burstSeconds = uniform(rng, 0.004f, 0.009f);
  return p;
}

// A burst of noise circulating in a short delay line settles into a pitched,
// string-like ring.
DrumParams randomPluck(uint32_t& rng) {
  DrumParams p;
  p.family = kPluck;
  p.baseHz = ratio(rng, 80, 800);
  p.ampSeconds = ratio(rng, 0.150f, 1.200f);
  p.brightness = uniform(rng, 0.15f, 1.0f);
  return p;
}

// Two square waves a non-musical interval apart, squeezed through a band-pass.
DrumParams randomCowbell(uint32_t& rng) {
  DrumParams p;
  p.family = kCowbell;
  p.wave = kSquare;
  p.toneLevel = 0.6f;
  p.baseHz = ratio(rng, 380, 950);
  p.pairRatio = uniform(rng, 1.30f, 1.62f);
  p.toneHpHz = p.baseHz * 0.8f;
  p.toneLpHz = p.baseHz * uniform(rng, 3.0f, 7.0f);
  p.ampSeconds = ratio(rng, 0.040f, 0.350f);
  return p;
}

// A bass note whose brightness snaps shut: the classic squelch.
DrumParams randomAcid(uint32_t& rng) {
  DrumParams p;
  p.family = kAcid;
  p.wave = kSaw;
  p.toneLevel = 0.8f;
  p.baseHz = ratio(rng, 55, 280);
  p.ampSeconds = ratio(rng, 0.080f, 0.400f);
  p.resonance = uniform(rng, 0.55f, 0.92f);
  p.cutoffDepth = ratio(rng, 4.0f, 30.0f);
  p.cutoffSeconds = ratio(rng, 0.020f, 0.150f);
  return p;
}

// The round "doom" of an analog tom or conga: a sine that sags a little in
// pitch, pushed gently into drive for warmth, with a faint stick click.
DrumParams randomTom(uint32_t& rng) {
  DrumParams p;
  p.family = kTom;
  p.baseHz = ratio(rng, 80, 280);
  p.sweepHz = p.baseHz * uniform(rng, 0.25f, 1.0f);
  p.sweepSeconds = ratio(rng, 0.030f, 0.090f);
  p.ampSeconds = ratio(rng, 0.100f, 0.400f);
  p.drive = uniform(rng, 0.8f, 1.8f);
  p.noiseLevel = uniform(rng, 0.03f, 0.10f);
  p.noiseSeconds = ratio(rng, 0.003f, 0.008f);
  p.noiseHpHz = 1000;
  p.noiseLpHz = 6000;
  return p;
}

// Analog snare: a short tuned thump for the drum shell, and bright noise for
// the wires, each with its own decay.
DrumParams randomSnare(uint32_t& rng) {
  DrumParams p;
  p.family = kSnare;
  p.baseHz = ratio(rng, 150, 270);
  p.sweepHz = p.baseHz * uniform(rng, 0.3f, 0.9f);
  p.sweepSeconds = ratio(rng, 0.008f, 0.025f);
  p.ampSeconds = ratio(rng, 0.040f, 0.110f);
  p.toneLevel = uniform(rng, 0.5f, 0.9f);
  p.noiseLevel = uniform(rng, 0.6f, 1.0f);
  p.noiseSeconds = ratio(rng, 0.070f, 0.220f);
  p.noiseHpHz = ratio(rng, 800, 2500);
  p.noiseLpHz = ratio(rng, 6000, 12000);
  return p;
}

// Rimshot or clave: a high, pure tone that is over almost at once, with a
// touch of noise on the front.
DrumParams randomRim(uint32_t& rng) {
  DrumParams p;
  p.family = kRim;
  p.wave = chance(rng, 0.5f) ? kTriangle : kSine;
  p.baseHz = ratio(rng, 700, 2600);
  p.ampSeconds = ratio(rng, 0.008f, 0.035f);
  p.drive = uniform(rng, 0.5f, 2.0f);
  p.noiseLevel = uniform(rng, 0.10f, 0.35f);
  p.noiseSeconds = ratio(rng, 0.002f, 0.006f);
  p.noiseHpHz = 2000;
  p.noiseLpHz = 10000;
  return p;
}

DrumParams randomDrum(uint32_t& rng, Family family) {
  switch (family) {
    case kTom:
      return randomTom(rng);
    case kSnare:
      return randomSnare(rng);
    case kRim:
      return randomRim(rng);
    case kNoise:
      return randomNoise(rng);
    case kMetal:
      return randomMetal(rng);
    case kBlip:
      return randomBlip(rng);
    case kKick:
      return randomKick(rng);
    case kClap:
      return randomClap(rng);
    case kPluck:
      return randomPluck(rng);
    case kCowbell:
      return randomCowbell(rng);
    case kAcid:
      return randomAcid(rng);
    default:
      return randomPew(rng);
  }
}

// Mix level per voice. The small speaker is far louder at high pitches, so
// high pitched tones are pulled back: full level up to 250 Hz, falling to
// 0.45 at 700 Hz and above.
float gainFor(const DrumParams& p) {
  float g = 1.0f - (p.baseHz - 250.0f) / 450.0f * 0.55f;
  g = g > 1.0f ? 1.0f : (g < 0.45f ? 0.45f : g);
  switch (p.family) {
    case kNoise:
      return 0.6f;
    case kClap:
      return 0.7f;
    case kMetal:
      return g * 0.8f;  // FM adds a lot of bright energy
    case kPluck:
      return g * 0.8f;
    case kAcid:
      return 0.7f;
    case kKick:
    case kTom:
      return 1.0f;
    case kSnare:
      return 0.75f;
    case kRim:
      return 0.6f;
    default:
      return g;
  }
}

// Per-sample multiplier that decays to 1/e in `seconds`.
float decayFor(float seconds) { return expf(-1.0f / (seconds * kSampleRate)); }

// One-pole filter coefficients for a cutoff in Hz.
float highPassCoef(float hz) { return 1.0f / (1.0f + kTwoPi * hz / kSampleRate); }
float lowPassCoef(float hz) {
  float c = kTwoPi * hz / kSampleRate;
  return c > 0.99f ? 0.99f : c;
}

}  // namespace

const char* familyName(Family f) {
  static const char* const kNames[kFamilyCount] = {
      "PEW",   "NOISE",   "METAL", "BLIP", "KICK",  "CLAP",
      "PLUCK", "COWBELL", "ACID",  "TOM",  "SNARE", "RIM",
  };
  return kNames[f];
}

const char* laneName(Family f, int lane) {
  static const char* const kNames[kFamilyCount][kLaneCount] = {
      {"PIT", "DEC", "SWP"},  // PEW: sweep depth
      {"TON", "DEC", "BDY"},  // NOISE: brightness; tone body
      {"PIT", "DEC", "FM"},   // METAL: FM amount
      {"PIT", "DEC", "BND"},  // BLIP: bend depth
      {"PIT", "DEC", "DRV"},  // KICK: drive
      {"TON", "DEC", "SPR"},  // CLAP: brightness; spread between the bursts
      {"PIT", "DEC", "BRT"},  // PLUCK: brightness of the pluck
      {"PIT", "DEC", "DTN"},  // COWBELL: detune between the two oscillators
      {"PIT", "DEC", "ENV"},  // ACID: filter envelope depth
      {"PIT", "DEC", "SWP"},  // TOM: how far the pitch sags
      {"TON", "DEC", "SNP"},  // SNARE: tuning and brightness; snap (noise level)
      {"PIT", "DEC", "CLK"},  // RIM: click (noise level)
  };
  return kNames[f][lane];
}

const char* laneFullName(Family f, int lane) {
  struct Pair {
    const char* brief;
    const char* full;
  };
  static const Pair kFull[] = {
      {"PIT", "PITCH"},  {"DEC", "DECAY"}, {"TON", "TONE"},   {"SWP", "SWEEP"},  {"BDY", "BODY"},
      {"FM", "FM"},      {"BND", "BEND"},  {"DRV", "DRIVE"},  {"SPR", "SPREAD"}, {"BRT", "BRIGHT"},
      {"DTN", "DETUNE"}, {"ENV", "ENVELOPE"}, {"SNP", "SNAP"}, {"CLK", "CLICK"},
  };
  const char* brief = laneName(f, lane);
  for (const Pair& pair : kFull) {
    if (strcmp(pair.brief, brief) == 0) return pair.full;
  }
  return brief;
}

void familiesForSeed(uint32_t seed, Family out[kVoiceCount]) {
  uint32_t rng = stateForSeed(seed);
  pickFamilies(rng, out);
}

void Drum::init(const DrumParams& p, uint32_t noiseSeed) {
  family_ = p.family;
  wave_ = p.wave;
  toneLevel_ = p.toneLevel;
  noiseLevel_ = p.noiseLevel;
  baseHz_ = p.baseHz;
  baseStep_ = p.baseHz / kSampleRate;
  sweepStep_ = p.sweepHz / kSampleRate;
  sweepDecay_ = decayFor(p.sweepSeconds);
  fmRatio_ = p.fmRatio;
  fmIndex_ = p.fmIndex;
  fmDecay_ = decayFor(p.fmSeconds);
  pairRatio_ = p.pairRatio;
  toneHpCoef_ = p.toneHpHz > 0 ? highPassCoef(p.toneHpHz) : 0;
  toneLpCoef_ = p.toneLpHz > 0 ? lowPassCoef(p.toneLpHz) : 0;
  resonanceDamp_ = p.resonance > 0 ? 2.0f * (1.0f - p.resonance) : 0;  // 0 = no filter
  cutoffBase_ = kTwoPi * p.baseHz / kSampleRate;
  cutoffDepth_ = p.cutoffDepth;
  cutoffDecay_ = decayFor(p.cutoffSeconds);
  drive_ = p.drive;
  hpOmega_ = kTwoPi * p.noiseHpHz / kSampleRate;
  lpOmega_ = kTwoPi * p.noiseLpHz / kSampleRate;
  burstCount_ = p.burstCount;
  burstGapSamples_ = p.burstGapSeconds * kSampleRate;
  burstDecay_ = decayFor(p.burstSeconds);
  brightness_ = p.brightness;
  for (int d = -kDecayRange; d <= kDecayRange; ++d) {
    float scale = powf(2.0f, d / 4.0f);
    ampDecays_[d + kDecayRange] = decayFor(p.ampSeconds * scale);
    noiseDecays_[d + kDecayRange] = decayFor(p.noiseSeconds * scale);
  }
  rng_ = noiseSeed | 1;
  amp_ = 0;  // a new sound starts silent
  noiseAmp_ = 0;
  burstsLeft_ = 0;
  hpX_ = hpY_ = lpY_ = 0;
  toneHpX_ = toneHpY_ = toneLpY_ = 0;
  filterLow_ = filterBand_ = 0;
}

void Drum::trigger(float pitchMul, int decayOffset, float shapeMul, float level) {
  // No two hits of an analog drum are quite alike: let the pitch wander by
  // about half a percent and the level by a few percent from hit to hit.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  pitchMul *= 1.0f + (int16_t)(rng_ >> 16) * (0.006f / 32768.0f);
  level *= 1.0f + (int16_t)(rng_ & 0xFFFF) * (0.05f / 32768.0f);

  phase_ = 0;
  modPhase_ = 0;
  pairPhase_ = 0;
  level_ = level;
  amp_ = level;
  noiseAmp_ = level;
  sweep_ = 1.0f;
  fmEnv_ = 1.0f;
  cutoffEnv_ = 1.0f;
  pitchMul_ = pitchMul;
  ampDecay_ = ampDecays_[decayOffset + kDecayRange];
  noiseDecay_ = noiseDecays_[decayOffset + kDecayRange];

  // The shape lane drives a different parameter on each instrument.
  sweepMul_ = (family_ == kPew || family_ == kBlip || family_ == kTom) ? shapeMul : 1.0f;
  noiseMul_ = (family_ == kSnare || family_ == kRim) ? shapeMul : 1.0f;
  toneMul_ = family_ == kNoise ? shapeMul : 1.0f;
  fmMul_ = family_ == kMetal ? shapeMul : 1.0f;
  driveMul_ = family_ == kKick ? shapeMul : 1.0f;
  pairMul_ = family_ == kCowbell ? shapeMul : 1.0f;
  cutoffMul_ = family_ == kAcid ? shapeMul : 1.0f;

  // One-pole noise filters whose cutoffs follow the pitch lane.
  hpCoef_ = 1.0f / (1.0f + hpOmega_ * pitchMul);
  float lp = lpOmega_ * pitchMul;
  lpCoef_ = lp > 0.99f ? 0.99f : lp;

  // Clap: the gap between bursts follows the shape lane.
  burstsLeft_ = burstCount_;
  burstGap_ = burstGapSamples_ * (family_ == kClap ? shapeMul : 1.0f);
  burstTimer_ = burstGap_;

  if (family_ == kPluck) {
    // Fill one period of the string with noise; a duller pluck is the same
    // noise through a low-pass. The shape lane sets how bright.
    float hz = baseHz_ * pitchMul;
    int length = (int)(kSampleRate / hz);
    stringLength_ = length < 8 ? 8 : (length > kStringMax ? kStringMax : length);
    stringPos_ = 0;
    float bright = brightness_ * shapeMul;
    bright = bright > 1.0f ? 1.0f : (bright < 0.03f ? 0.03f : bright);
    float makeUp = 1.0f / sqrtf(bright);  // a dull pluck would otherwise be quiet
    if (makeUp > 3.0f) makeUp = 3.0f;
    float smooth = 0;
    for (int i = 0; i < stringLength_; ++i) {
      rng_ ^= rng_ << 13;
      rng_ ^= rng_ >> 17;
      rng_ ^= rng_ << 5;
      float white = (int32_t)rng_ * (1.0f / 2147483648.0f);
      smooth += bright * (white - smooth);
      string_[i] = smooth * makeUp;
    }
  }
}

bool Drum::active() const {
  return amp_ * toneLevel_ * toneMul_ > kSilence ||
         noiseAmp_ * noiseLevel_ * noiseMul_ > kSilence || burstsLeft_ > 0;
}

float Drum::nextNoise() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  float white = (int32_t)rng_ * (1.0f / 2147483648.0f);
  lpY_ += lpCoef_ * (white - lpY_);
  hpY_ = hpCoef_ * (hpY_ + lpY_ - hpX_);
  hpX_ = lpY_;
  float out = hpY_ * noiseAmp_ * noiseLevel_ * noiseMul_;

  if (burstsLeft_ > 0) {
    // Still in the opening bursts: each dies quickly, then the next one hits.
    noiseAmp_ *= burstDecay_;
    burstTimer_ -= 1.0f;
    if (burstTimer_ <= 0) {
      noiseAmp_ = level_;
      burstTimer_ = burstGap_;
      --burstsLeft_;
    }
  } else {
    noiseAmp_ *= noiseDecay_;
  }
  return out;
}

// Karplus-Strong: each sample leaving the delay line is averaged with its
// neighbour and fed back in, so the noise mellows into a note.
float Drum::nextPluck() {
  int next = stringPos_ + 1;
  if (next >= stringLength_) next = 0;
  float out = string_[stringPos_];
  string_[stringPos_] = (out + string_[next]) * 0.498f;
  stringPos_ = next;
  out *= amp_;
  amp_ *= ampDecay_;
  return out;
}

float Drum::next() {
  if (family_ == kPluck) return nextPluck();

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
    } else if (wave_ == kSaw) {
      osc = 2.0f * phase_ - 1.0f;
    } else {
      osc = phase_ < 0.5f ? 1.0f : -1.0f;
      if (pairRatio_ > 0) {
        // Second square, detuned by the pair ratio (stretched by the shape lane).
        float pairStep = step * (1.0f + (pairRatio_ - 1.0f) * pairMul_);
        if (pairStep > kMaxStep) pairStep = kMaxStep;
        pairPhase_ += pairStep;
        if (pairPhase_ >= 1.0f) pairPhase_ -= 1.0f;
        osc = 0.5f * (osc + (pairPhase_ < 0.5f ? 1.0f : -1.0f));
      }
    }

    if (resonanceDamp_ > 0) {
      // State-variable low-pass. The cutoff starts high and falls to the note.
      float f = cutoffBase_ * pitchMul_ * (1.0f + cutoffDepth_ * cutoffMul_ * cutoffEnv_);
      if (f > 0.9f) f = 0.9f;  // stability limit for this filter form
      filterLow_ += f * filterBand_;
      float high = osc - filterLow_ - resonanceDamp_ * filterBand_;
      filterBand_ += f * high;
      osc = filterLow_;
      cutoffEnv_ *= cutoffDecay_;
    }

    if (toneLpCoef_ > 0) {
      toneLpY_ += toneLpCoef_ * (osc - toneLpY_);
      toneHpY_ = toneHpCoef_ * (toneHpY_ + toneLpY_ - toneHpX_);
      toneHpX_ = toneLpY_;
      osc = toneHpY_ * 2.0f;  // make up for what the band-pass removes
    }

    float tone = osc * amp_ * toneLevel_ * toneMul_;
    if (drive_ > 0) {
      // Soft clip: louder in, flatter top. Also lifts the tail, for punch.
      float driven = tone * drive_ * driveMul_;
      tone = driven / (1.0f + fabsf(driven));
    }
    out = tone;
    amp_ *= ampDecay_;
    sweep_ *= sweepDecay_;
  }

  if (noiseLevel_ > 0) out += nextNoise();

  return out;
}

void DrumKit::init(uint32_t seed) {
  buildSineTable();
  uint32_t rng = stateForSeed(seed);
  Family families[kVoiceCount];
  pickFamilies(rng, families);

  for (int v = 0; v < kVoiceCount; ++v) {
    DrumParams p = randomDrum(rng, families[v]);
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

    // Leave headroom for three voices, then round off the peaks the way an
    // overdriven analog mixer does, instead of clipping them flat: a cubic
    // curve that is nearly straight for quiet signals and levels out at 1.
    mix *= 0.5f;
    if (mix > 1.5f) mix = 1.5f;
    if (mix < -1.5f) mix = -1.5f;
    mix -= (4.0f / 27.0f) * mix * mix * mix;
    out[i] = (int16_t)(mix * 32767.0f);
  }
}

}  // namespace voices
