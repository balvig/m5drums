// Virtual-analog drum voices. Pure DSP: no Arduino, no I/O, no allocation.
// The same render() output can feed the speaker or a WAV file.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace voices {

constexpr int kSampleRate = 44100;

enum Voice : uint8_t { kVoiceA = 0, kVoiceB = 1, kVoiceC = 2, kVoiceCount = 3 };

// The instruments a voice can be. All are built from the same three
// ingredients (an oscillator with a pitch sweep, FM, filtered noise) in
// different proportions.
enum Family : uint8_t {
  kPew = 0,    // sine with a falling pitch: toms, zaps
  kNoise = 1,  // filtered noise, sometimes over a tone: hats, shakers, snares
  kMetal = 2,  // FM: bells, clangs, woodblocks
  kBlip = 3,   // square or triangle with a small bend: chiptune percussion
  kFamilyCount = 4,
};

// Every step carries three offsets, all centred on 0. The first two mean
// much the same on every instrument; the third is the instrument's own.
constexpr int kLaneCount = 3;
constexpr int kPitchRange = 18;  // +/- semitones (on NOISE: how bright)
constexpr int kDecayRange = 12;  // +/- levels; every 4 doubles or halves the length
constexpr int kShapeRange = 8;   // +/- levels; every 4 doubles or halves the amount of:
                                 //   PEW sweep depth, NOISE tone body,
                                 //   METAL FM amount, BLIP bend depth

const char* familyName(Family f);
// Three-letter name of a lane (0 = pitch, 1 = decay, 2 = shape) on an instrument.
const char* laneName(Family f, int lane);
// The instruments a seed gives the three voices, without building the kit.
void familiesForSeed(uint32_t seed, Family out[kVoiceCount]);

enum Wave : uint8_t { kSine, kTriangle, kSquare };

struct DrumParams {
  Family family = kPew;

  // Tone: an oscillator whose pitch moves from (base + sweep) to base. A
  // negative sweep rises instead of falling.
  Wave wave = kSine;
  float toneLevel = 1.0f;
  float baseHz = 200.0f;
  float sweepHz = 0.0f;
  float sweepSeconds = 0.05f;  // time for the sweep to fall to 1/e
  float ampSeconds = 0.1f;     // time for the tone to fall to 1/e

  // FM (sine only): a second oscillator at baseHz * ratio bends the phase.
  float fmRatio = 1.0f;
  float fmIndex = 0.0f;  // peak phase deviation, in turns
  float fmSeconds = 0.1f;

  // Noise through a low-pass then a high-pass.
  float noiseLevel = 0.0f;
  float noiseSeconds = 0.05f;
  float noiseHpHz = 1000.0f;
  float noiseLpHz = 8000.0f;
};

class Drum {
 public:
  // Computes coefficients and silences the voice. Not for use while sounding
  // from another thread.
  void init(const DrumParams& params, uint32_t noiseSeed);
  // pitchMul scales the oscillator and the noise filters; decayOffset is
  // -kDecayRange..+kDecayRange; shapeMul scales the instrument's own parameter.
  void trigger(float pitchMul, int decayOffset, float shapeMul);
  bool active() const;
  float next();

 private:
  Family family_ = kPew;
  Wave wave_ = kSine;
  float toneLevel_ = 0, noiseLevel_ = 0;
  float baseStep_ = 0, sweepStep_ = 0;  // turns per sample
  float sweepDecay_ = 0;
  float fmRatio_ = 1, fmIndex_ = 0, fmDecay_ = 0;
  float hpOmega_ = 0, lpOmega_ = 0;  // filter cutoffs, radians per sample
  float ampDecays_[2 * kDecayRange + 1] = {};  // one per decay offset
  float noiseDecays_[2 * kDecayRange + 1] = {};

  // State of the hit in progress.
  float phase_ = 0, modPhase_ = 0;
  float amp_ = 0, sweep_ = 0, fmEnv_ = 0, noiseAmp_ = 0;
  float pitchMul_ = 1, sweepMul_ = 1, toneMul_ = 1, fmMul_ = 1;
  float ampDecay_ = 0, noiseDecay_ = 0;
  float hpCoef_ = 0, lpCoef_ = 0, hpX_ = 0, hpY_ = 0, lpY_ = 0;
  uint32_t rng_ = 1;
};

class DrumKit {
 public:
  // Picks three different instruments and a random sound for each from the
  // seed. The same seed always gives the same kit. No allocation or blocking.
  void init(uint32_t seed);

  // pitch: -kPitchRange..+kPitchRange. decay: -kDecayRange..+kDecayRange.
  // shape: -kShapeRange..+kShapeRange.
  void trigger(Voice v, int pitch = 0, int decay = 0, int shape = 0);
  void setVolume(Voice v, float gain) { gain_[v] = gain; }

  // Renders n mono 16-bit samples of all voices mixed together.
  void render(int16_t* out, size_t n);

  // True while any voice is still ringing.
  bool active() const;

 private:
  Drum drums_[kVoiceCount];
  float gain_[kVoiceCount] = {1.0f, 1.0f, 1.0f};
  float pitchMuls_[2 * kPitchRange + 1] = {};  // one per semitone offset
  float shapeMuls_[2 * kShapeRange + 1] = {};  // one per shape offset
};

}  // namespace voices
