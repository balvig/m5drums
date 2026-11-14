// Virtual-analog drum voices. Pure DSP: no Arduino, no I/O, no allocation.
// The same render() output can feed the speaker or a WAV file.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace voices {

constexpr int kSampleRate = 44100;

enum Voice : uint8_t { kTomLow = 0, kTomMid = 1, kTomHigh = 2, kVoiceCount = 3 };

// Per-hit offsets, both centred on 0.
constexpr int kPitchRange = 12;  // +/- semitones
constexpr int kDecayRange = 8;   // +/- levels; every 4 levels doubles or halves the length

// Synth tom ("pew"): a sine whose pitch sweeps from (base + sweep) down to
// base, with an exponential amplitude decay.
struct TomParams {
  float baseHz;
  float sweepHz;
  float sweepSeconds;  // time for the sweep to fall to 1/e
  float ampSeconds;    // time for the level to fall to 1/e
};

class Tom {
 public:
  // Computes envelope coefficients. Call once, outside the audio path.
  void init(const TomParams& params);
  // pitchMul scales the whole sweep; decayOffset is -kDecayRange..+kDecayRange.
  void trigger(float pitchMul, int decayOffset);
  bool active() const;
  float next();

 private:
  float baseStep_ = 0, sweepStep_ = 0;  // radians per sample
  float sweepDecay_ = 0;
  float ampDecays_[2 * kDecayRange + 1] = {};  // one per decay offset
  float phase_ = 0, amp_ = 0, sweep_ = 0;
  float pitchMul_ = 1.0f, ampDecay_ = 0;
};

class DrumKit {
 public:
  // Picks a random tuning for each voice from the seed, so every seed gives a
  // different kit. Call once, outside the audio path.
  void init(uint32_t seed);

  // pitch: -kPitchRange..+kPitchRange semitones. decay: -kDecayRange..+kDecayRange.
  void trigger(Voice v, int pitch = 0, int decay = 0);
  void setVolume(Voice v, float gain) { gain_[v] = gain; }

  // Renders n mono 16-bit samples of all voices mixed together.
  void render(int16_t* out, size_t n);

 private:
  Tom toms_[kVoiceCount];
  // The small speaker is far louder in the high voice's range, so it is pulled back.
  float gain_[kVoiceCount] = {1.0f, 0.9f, 0.45f};
  float pitchMuls_[2 * kPitchRange + 1] = {};  // one per semitone offset
};

}  // namespace voices
