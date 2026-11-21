// Virtual-analog drum voices. Pure DSP: no Arduino, no I/O, no allocation.
// The same render() output can feed the speaker or a WAV file.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace voices {

constexpr int kSampleRate = 44100;

enum Voice : uint8_t { kVoiceA = 0, kVoiceB = 1, kVoiceC = 2, kVoiceCount = 3 };

// The instruments a voice can be.
enum Family : uint8_t {
  kPew = 0,      // sine with a falling pitch: toms, zaps
  kNoise = 1,    // filtered noise, sometimes over a tone: hats, shakers, snares
  kMetal = 2,    // FM: bells, clangs, woodblocks
  kBlip = 3,     // square or triangle with a small bend: chiptune percussion
  kKick = 4,     // low sine with a fast drop, a click and drive
  kClap = 5,     // band-passed noise in a few quick bursts, then a tail
  kPluck = 6,    // plucked string (Karplus-Strong): kalimba, marimba, muted guitar
  kCowbell = 7,  // two detuned square waves through a band-pass
  kAcid = 8,     // sawtooth through a resonant filter that snaps shut
  kTom = 9,      // round sine with a gentle pitch drop and a little warmth
  kSnare = 10,   // a short tuned body under a burst of snappy noise
  kRim = 11,     // a very short, high, woody tick: rimshot, clave
  kFamilyCount = 12,
};

// Every step carries three offsets, all centred on 0. The first two mean
// much the same on every instrument; the third is the instrument's own.
constexpr int kLaneCount = 3;
constexpr int kPitchRange = 18;  // +/- semitones (on NOISE and CLAP: how bright)
constexpr int kDecayRange = 12;  // +/- levels; every 4 doubles or halves the length
constexpr int kShapeRange = 8;   // +/- levels; every 4 doubles or halves the amount of
                                 // the instrument's own parameter (see laneName)

const char* familyName(Family f);
// Three-letter name of a lane (0 = pitch, 1 = decay, 2 = shape) on an instrument.
const char* laneName(Family f, int lane);
// The same in full, 8 letters at most: PITCH, DECAY, SWEEP...
const char* laneFullName(Family f, int lane);
// The instruments a seed gives the three voices, without building the kit.
void familiesForSeed(uint32_t seed, Family out[kVoiceCount]);

enum Wave : uint8_t { kSine, kTriangle, kSquare, kSaw };

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

  // Second square oscillator at baseHz * ratio (square only; 0 = none).
  float pairRatio = 0.0f;
  // Band-pass on the tone (0 = none).
  float toneHpHz = 0.0f;
  float toneLpHz = 0.0f;

  // Resonant low-pass on the tone, whose cutoff starts at
  // baseHz * (1 + cutoffDepth) and falls to baseHz (resonance 0 = none).
  float resonance = 0.0f;  // 0..1
  float cutoffDepth = 0.0f;
  float cutoffSeconds = 0.05f;

  // Soft clipping of the tone (0 = none).
  float drive = 0.0f;

  // Noise through a low-pass then a high-pass.
  float noiseLevel = 0.0f;
  float noiseSeconds = 0.05f;
  float noiseHpHz = 1000.0f;
  float noiseLpHz = 8000.0f;
  // Noise retriggered a few times before its tail (0 = a single hit).
  int burstCount = 0;
  float burstGapSeconds = 0.010f;
  float burstSeconds = 0.006f;

  // Plucked string: how bright the pluck is, 0..1.
  float brightness = 1.0f;
};

class Drum {
 public:
  // Computes coefficients and silences the voice. Not for use while sounding
  // from another thread.
  void init(const DrumParams& params, uint32_t noiseSeed);
  // pitchMul scales the oscillator and the noise filters; decayOffset is
  // -kDecayRange..+kDecayRange; shapeMul scales the instrument's own
  // parameter; level is the starting amplitude (1 = normal).
  void trigger(float pitchMul, int decayOffset, float shapeMul, float level);
  bool active() const;
  float next();

 private:
  static constexpr int kStringMax = 1024;  // longest plucked-string period, in samples

  float nextNoise();
  float nextPluck();

  Family family_ = kPew;
  Wave wave_ = kSine;
  float toneLevel_ = 0, noiseLevel_ = 0;
  float baseHz_ = 0;
  float baseStep_ = 0, sweepStep_ = 0;  // turns per sample
  float sweepDecay_ = 0;
  float fmRatio_ = 1, fmIndex_ = 0, fmDecay_ = 0;
  float pairRatio_ = 0;
  float toneHpCoef_ = 0, toneLpCoef_ = 0;
  float resonanceDamp_ = 0, cutoffBase_ = 0, cutoffDepth_ = 0, cutoffDecay_ = 0;
  float drive_ = 0;
  float hpOmega_ = 0, lpOmega_ = 0;  // noise filter cutoffs, radians per sample
  int burstCount_ = 0;
  float burstGapSamples_ = 0, burstDecay_ = 0;
  float brightness_ = 1;
  float ampDecays_[2 * kDecayRange + 1] = {};  // one per decay offset
  float noiseDecays_[2 * kDecayRange + 1] = {};

  // State of the hit in progress.
  float phase_ = 0, modPhase_ = 0, pairPhase_ = 0;
  float amp_ = 0, sweep_ = 0, fmEnv_ = 0, noiseAmp_ = 0, cutoffEnv_ = 0;
  float level_ = 1;
  float pitchMul_ = 1, sweepMul_ = 1, toneMul_ = 1, fmMul_ = 1;
  float driveMul_ = 1, pairMul_ = 1, cutoffMul_ = 1, noiseMul_ = 1;
  float ampDecay_ = 0, noiseDecay_ = 0;
  float hpCoef_ = 0, lpCoef_ = 0, hpX_ = 0, hpY_ = 0, lpY_ = 0;
  float toneHpX_ = 0, toneHpY_ = 0, toneLpY_ = 0;
  float filterLow_ = 0, filterBand_ = 0;
  int burstsLeft_ = 0;
  float burstTimer_ = 0, burstGap_ = 0;
  float string_[kStringMax] = {};
  int stringLength_ = 0, stringPos_ = 0;
  uint32_t rng_ = 1;
};

class DrumKit {
 public:
  // Picks three different instruments and a random sound for each from the
  // seed. The same seed always gives the same kit. No allocation or blocking.
  void init(uint32_t seed);

  // pitch: -kPitchRange..+kPitchRange. decay: -kDecayRange..+kDecayRange.
  // shape: -kShapeRange..+kShapeRange. level: starting amplitude, 1 = normal.
  void trigger(Voice v, int pitch = 0, int decay = 0, int shape = 0, float level = 1.0f);
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
