// Step sequencer driving the drum kit. Timing is counted in rendered samples,
// so it cannot drift against the audio. No Arduino, no I/O, no allocation.
#pragma once
#include <atomic>

#include "voices.h"

class Sequencer {
 public:
  static constexpr int kSteps = 8;
  static constexpr int kTracks = voices::kVoiceCount;
  static constexpr int kMinBpm = 40, kMaxBpm = 240;
  // Swing, MPC style: the share of each pair of 16ths taken by the first one.
  // 50 = straight, 66 = triplet feel, 75 = hard shuffle.
  static constexpr int kMinSwing = 50, kMaxSwing = 75;

  // Call once before the audio task starts. The seed picks the kit's sounds.
  void init(uint32_t seed);

  // Makes this a stopped copy of another sequencer: same sounds, pattern,
  // automation, tempo and swing. Only for a sequencer no audio task is using.
  void copyFrom(const Sequencer& other);

  // Replaces the kit's sounds with new ones picked from the seed. Takes effect
  // at the start of the next audio buffer.
  void newKit(uint32_t seed) {
    kitSeed_.store(seed);
    kitPending_.store(true);
  }
  uint32_t kitSeed() const { return kitSeed_.load(); }

  // --- Control side (UI loop). All lock-free. ---
  void toggleStep(int track, int step) { pattern_[track].fetch_xor(1u << step); }
  bool stepOn(int track, int step) const { return pattern_[track].load() & (1u << step); }
  // One bit per step.
  uint8_t pattern(int track) const { return pattern_[track].load(); }
  void setPattern(int track, uint8_t bits) { pattern_[track].store(bits); }
  // Turns every step off and flattens the automation.
  void clearPattern();

  // Per-step automation, applied each time the step fires.
  void setStepPitch(int track, int step, int semitones) {
    pitch_[track][step].store(clamp(semitones, -voices::kPitchRange, voices::kPitchRange));
  }
  int stepPitch(int track, int step) const { return pitch_[track][step].load(); }
  void setStepDecay(int track, int step, int level) {
    decay_[track][step].store(clamp(level, -voices::kDecayRange, voices::kDecayRange));
  }
  int stepDecay(int track, int step) const { return decay_[track][step].load(); }
  void setStepLevel(int track, int step, int level) {
    level_[track][step].store(clamp(level, -voices::kLevelRange, voices::kLevelRange));
  }
  int stepLevel(int track, int step) const { return level_[track][step].load(); }

  void setPlaying(bool on) { playRequested_.store(on); }
  bool playing() const { return playRequested_.load(); }
  void setTempo(int bpm) { bpm_.store(clamp(bpm, kMinBpm, kMaxBpm)); }
  int tempo() const { return bpm_.load(); }
  void setSwing(int percent) { swing_.store(clamp(percent, kMinSwing, kMaxSwing)); }
  int swing() const { return swing_.load(); }
  // Step currently sounding, or -1 when stopped.
  int currentStep() const { return currentStep_.load(); }

  // --- Audio side ---
  // Renders n mono 16-bit samples.
  void render(int16_t* out, size_t n);
  // False when stopped with nothing ringing: render() would produce pure
  // silence, so the output can be left to switch off.
  bool hasAudio() const {
    return playRequested_.load() || running_ || kitPending_.load() || kit_.active();
  }

 private:
  static int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
  void triggerStep(int track, int step);

  voices::DrumKit kit_;

  std::atomic<uint32_t> kitSeed_{0};
  std::atomic<bool> kitPending_{false};
  std::atomic<uint8_t> pattern_[kTracks] = {};
  std::atomic<int8_t> pitch_[kTracks][kSteps] = {};
  std::atomic<int8_t> decay_[kTracks][kSteps] = {};
  std::atomic<int8_t> level_[kTracks][kSteps] = {};
  std::atomic<uint16_t> bpm_{120};
  std::atomic<uint8_t> swing_{kMinSwing};
  std::atomic<bool> playRequested_{false};
  std::atomic<int8_t> currentStep_{-1};

  // Audio-side state only.
  bool running_ = false;
  int nextStep_ = 0;
  uint32_t samplesToNextStep_ = 0;
};
