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
  // Evolve: over an 8-bar phrase, the pattern moves away from what is set,
  // following a named theme, and is back home when the phrase restarts. A
  // theme can move every hit's three values and its volume, drop hits, add
  // hits on empty steps, turn hits into rolls, and crush the whole output. The first pass of the pattern is always exactly as set, and
  // the stored values never change. What a theme does is fixed by the theme,
  // the kit seed, the hit and the pass, so a phrase sounds the same every
  // time, live and exported.
  static constexpr int kEvolveBars = 8;
  static constexpr int kPassesPerBar = 2;  // the 8-step pattern is half a bar
  static constexpr int kEvolvePasses = kEvolveBars * kPassesPerBar;
  static constexpr int kEvolveThemes = 8;  // themes are numbered 1..8; 0 = off
  static const char* evolveThemeName(int theme);

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
  void setStepShape(int track, int step, int shape) {
    shape_[track][step].store(clamp(shape, -voices::kShapeRange, voices::kShapeRange));
  }
  int stepShape(int track, int step) const { return shape_[track][step].load(); }

  void setPlaying(bool on) { playRequested_.store(on); }
  bool playing() const { return playRequested_.load(); }
  void setTempo(int bpm) { bpm_.store(clamp(bpm, kMinBpm, kMaxBpm)); }
  int tempo() const { return bpm_.load(); }
  void setSwing(int percent) { swing_.store(clamp(percent, kMinSwing, kMaxSwing)); }
  int swing() const { return swing_.load(); }
  void setEvolveTheme(int theme) { evolveTheme_.store(clamp(theme, 0, kEvolveThemes)); }
  int evolveTheme() const { return evolveTheme_.load(); }
  // Passes of the pattern in one phrase (0 when evolve is off), and the one
  // now playing (-1 when stopped or off).
  int evolvePasses() const { return evolveTheme_.load() ? kEvolvePasses : 0; }
  int evolvePass() const { return evolvePass_.load(); }

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
  void triggerStep(int track, int step, float level);
  int evolveAmount() const;
  int evolved(int lane, int value, int track, int step) const;
  void fireStep(int step, uint32_t stepSamples);
  void crush(int16_t* out, size_t n);

  voices::DrumKit kit_;

  std::atomic<uint32_t> kitSeed_{0};
  std::atomic<bool> kitPending_{false};
  std::atomic<uint8_t> pattern_[kTracks] = {};
  std::atomic<int8_t> pitch_[kTracks][kSteps] = {};
  std::atomic<int8_t> decay_[kTracks][kSteps] = {};
  std::atomic<int8_t> shape_[kTracks][kSteps] = {};
  std::atomic<uint16_t> bpm_{120};
  std::atomic<uint8_t> swing_{kMinSwing};
  std::atomic<uint8_t> evolveTheme_{0};
  std::atomic<int8_t> evolvePass_{-1};
  std::atomic<bool> playRequested_{false};
  std::atomic<int8_t> currentStep_{-1};

  // Audio-side state only.
  bool running_ = false;
  int nextStep_ = 0;
  int pass_ = -1;  // pass of the pattern within the evolve phrase
  uint32_t samplesToNextStep_ = 0;
  // Rolls: extra hits still to come inside the current step, per track.
  uint8_t rollLeft_[kTracks] = {};
  uint8_t rollStep_[kTracks] = {};
  uint32_t rollInterval_[kTracks] = {};
  uint32_t rollCountdown_[kTracks] = {};
  float rollLevel_[kTracks] = {};
  // Crush: sample-and-hold state.
  uint32_t crushCount_ = 0;
  int16_t crushHeld_ = 0;
};
