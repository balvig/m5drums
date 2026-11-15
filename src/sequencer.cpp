#include "sequencer.h"

#include <math.h>

namespace {

// An evolve theme: what happens over the 16 passes of a phrase.
struct Theme {
  const char* name;
  // Amount for each pass, -8..8. Pass 0 is always 0: the phrase starts as
  // set. Everything below is given at full amount (8) and scales with it.
  int8_t curve[Sequencer::kEvolvePasses];
  // How far each lane (pitch, decay, shape) moves.
  int8_t reach[voices::kLaneCount];
  // True: every hit moves the same way. False: each hit has its own direction
  // and strength, fixed by the kit seed.
  bool together;
  int8_t level;     // how far hit volumes spread, in steps (4 = double/half)
  uint8_t mute;     // % chance that a hit is dropped
  uint8_t ghost;    // % chance that an empty step gets a quiet hit
  uint8_t roll;     // % chance that a hit becomes a roll of 2-4
  uint8_t crush;    // sample-rate reduction factor on the whole output (0 = none)
};

const Theme kThemes[Sequencer::kEvolveThemes] = {
    // Wanders up and down, then builds through the last bar with a few rolls.
    {"DRIFT", {0, 2, 3, 4, 3, 1, 0, -2, -3, -4, -3, -1, 0, 2, 6, 8},
     {6, 8, 8}, false, 3, 0, 10, 25, 0},
    // Moves steadily further out, filling in with extra hits and rolls.
    {"BUILD", {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8},
     {7, 8, 8}, false, 4, 0, 35, 50, 3},
    // Swells out to the middle of the phrase, thinning and shifting, and glides back.
    {"WAVE", {0, 1, 2, 4, 5, 6, 7, 8, 8, 7, 6, 5, 4, 2, 1, 0},
     {7, 10, 8}, false, 4, 15, 20, 0, 0},
    // A riser: everything climbs an octave, tightens, and breaks into rolls.
    {"RISE", {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8},
     {12, -6, 4}, true, 0, 0, 0, 60, 0},
    // Everything gets longer and fuller, like a filter opening, and busier.
    {"OPEN", {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8},
     {0, 8, 8}, true, 0, 0, 25, 0, 0},
    // Seven bars exactly as set, then the last bar goes off.
    {"FILL", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 8},
     {9, 10, 8}, false, 4, 20, 60, 60, 4},
    // Lurches in and out of chaos: dropped hits, stray hits, rolls, crushed audio.
    {"GLITCH", {0, 3, 0, 5, 2, 0, 6, 0, 4, 7, 0, 5, 8, 2, 8, 6},
     {10, 10, 8}, false, 4, 35, 35, 50, 8},
    // Everything sinks in pitch, smears out and degrades.
    {"MELT", {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 8},
     {-10, 8, -4}, true, 0, 0, 0, 0, 10},
};

// What a chance roll decides; keeps the decisions independent of each other.
enum Decision : uint32_t { kMute = 1, kGhost = 2, kRoll = 3, kRollCount = 4, kLevel = 5 };

constexpr int kLaneRange[voices::kLaneCount] = {voices::kPitchRange, voices::kDecayRange,
                                                voices::kShapeRange};

uint32_t mix(uint32_t h) {
  h ^= h >> 15;
  h *= 0xC2B2AE35u;
  h ^= h >> 13;
  return h;
}

// A fixed direction and strength, -1..1, for one lane (or the volume) of one hit.
float driftDirection(uint32_t seed, int track, int step, int lane) {
  uint32_t h = (seed + 1) * 0x9E3779B1u;
  h ^= (uint32_t)(track * 64 + step * 8 + lane + 1) * 0x85EBCA6Bu;
  return (mix(h) >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

// A fixed 0..99 for one decision about one step of one pass.
int chance(uint32_t seed, int pass, int track, int step, Decision decision) {
  uint32_t h = (seed + 1) * 0x9E3779B1u;
  h ^= (uint32_t)(((pass * 4 + track) * 8 + step) * 8 + decision) * 0x85EBCA6Bu;
  return mix(h) % 100;
}

}  // namespace

void Sequencer::init(uint32_t seed) {
  kitSeed_.store(seed);
  kit_.init(seed);
}

void Sequencer::copyFrom(const Sequencer& other) {
  init(other.kitSeed());
  setTempo(other.tempo());
  setSwing(other.swing());
  setEvolveTheme(other.evolveTheme());
  for (int t = 0; t < kTracks; ++t) {
    setPattern(t, other.pattern(t));
    for (int s = 0; s < kSteps; ++s) {
      setStepPitch(t, s, other.stepPitch(t, s));
      setStepDecay(t, s, other.stepDecay(t, s));
      setStepShape(t, s, other.stepShape(t, s));
    }
  }
  playRequested_.store(false);
  currentStep_.store(-1);
  evolvePass_.store(-1);
  running_ = false;
}

void Sequencer::clearPattern() {
  for (int t = 0; t < kTracks; ++t) {
    pattern_[t].store(0);
    for (int s = 0; s < kSteps; ++s) {
      pitch_[t][s].store(0);
      decay_[t][s].store(0);
      shape_[t][s].store(0);
    }
  }
}

const char* Sequencer::evolveThemeName(int theme) {
  return theme >= 1 && theme <= kEvolveThemes ? kThemes[theme - 1].name : "OFF";
}

// The theme's amount for the pass now playing, -8..8. 0 when evolve is off
// and on the first pass, which always plays as set.
int Sequencer::evolveAmount() const {
  int theme = evolveTheme_.load();
  if (theme == 0 || pass_ <= 0) return 0;
  return kThemes[theme - 1].curve[pass_ % kEvolvePasses];
}

// A step's stored value, moved by the theme for the current pass of the phrase.
int Sequencer::evolved(int lane, int value, int track, int step) const {
  int amount = evolveAmount();
  if (amount == 0) return value;
  const Theme& t = kThemes[evolveTheme_.load() - 1];
  if (t.reach[lane] == 0) return value;
  float direction = t.together ? 1.0f : driftDirection(kitSeed_.load(), track, step, lane);
  float drift = direction * t.reach[lane] * amount / 8.0f;
  return clamp(value + (int)lroundf(drift), -kLaneRange[lane], kLaneRange[lane]);
}

void Sequencer::triggerStep(int track, int step, float level) {
  kit_.trigger((voices::Voice)track, evolved(0, pitch_[track][step].load(), track, step),
               evolved(1, decay_[track][step].load(), track, step),
               evolved(2, shape_[track][step].load(), track, step), level);
}

// Plays one step of the pattern, as the evolve theme has reshaped it.
void Sequencer::fireStep(int step, uint32_t stepSamples) {
  int amount = evolveAmount();
  int strength = amount < 0 ? -amount : amount;  // 0..8
  const Theme* theme = amount ? &kThemes[evolveTheme_.load() - 1] : nullptr;
  uint32_t seed = kitSeed_.load();

  for (int t = 0; t < kTracks; ++t) {
    rollLeft_[t] = 0;
    bool on = pattern_[t].load() & (1u << step);
    float level = 1.0f;

    if (theme) {
      if (on) {
        if (chance(seed, pass_, t, step, kMute) < theme->mute * strength / 8) on = false;
      } else if (chance(seed, pass_, t, step, kGhost) < theme->ghost * strength / 8) {
        on = true;
        level = 0.5f;  // added hits sit under the written ones
      }
      if (on && theme->level) {
        float spread = driftDirection(seed, t, step, kLevel) * theme->level * strength / 8.0f;
        level *= powf(2.0f, spread / 4.0f);
      }
    }
    if (!on) continue;
    triggerStep(t, step, level);

    if (theme && chance(seed, pass_, t, step, kRoll) < theme->roll * strength / 8) {
      // 1-3 more hits, evenly spaced inside this step, each a little quieter.
      int extra = 1 + chance(seed, pass_, t, step, kRollCount) % 3;
      rollLeft_[t] = extra;
      rollStep_[t] = step;
      rollInterval_[t] = stepSamples / (extra + 1);
      rollCountdown_[t] = rollInterval_[t];
      rollLevel_[t] = level * 0.8f;
    }
  }
}

// Sample-rate reduction: holds each sample for several, for a gritty lo-fi edge.
void Sequencer::crush(int16_t* out, size_t n) {
  int amount = running_ ? evolveAmount() : 0;
  if (amount == 0) return;
  int strength = amount < 0 ? -amount : amount;
  int maxFactor = kThemes[evolveTheme_.load() - 1].crush;
  uint32_t factor = 1 + (maxFactor > 1 ? (maxFactor - 1) * strength / 8 : 0);
  if (factor < 2) return;
  for (size_t i = 0; i < n; ++i) {
    if (crushCount_++ % factor == 0) crushHeld_ = out[i];
    out[i] = crushHeld_;
  }
}

void Sequencer::render(int16_t* out, size_t n) {
  int16_t* const start = out;
  const size_t total = n;

  // The kit belongs to the audio side, so a new one is built here.
  if (kitPending_.exchange(false)) kit_.init(kitSeed_.load());

  bool wanted = playRequested_.load();
  if (wanted && !running_) {
    running_ = true;
    nextStep_ = 0;
    pass_ = -1;  // the first step 1 makes it pass 0
    samplesToNextStep_ = 0;
  } else if (!wanted && running_) {
    running_ = false;
    currentStep_.store(-1);
    evolvePass_.store(-1);
    for (int t = 0; t < kTracks; ++t) rollLeft_[t] = 0;
  }

  while (n > 0) {
    if (running_ && samplesToNextStep_ == 0) {
      if (nextStep_ == 0) {
        int passes = evolvePasses();
        pass_ = passes > 0 ? (pass_ + 1) % passes : 0;
        evolvePass_.store(passes > 0 ? pass_ : -1);
      }
      // Steps are 16th notes. A pair of them lasts one 8th note; swing splits
      // the pair unevenly. Tempo and swing are read here, so changes apply
      // from the next step.
      uint32_t pairSamples = voices::kSampleRate * 30u / bpm_.load();
      uint32_t firstSamples = pairSamples * swing_.load() / 100u;
      samplesToNextStep_ = (nextStep_ % 2 == 0) ? firstSamples : pairSamples - firstSamples;

      fireStep(nextStep_, samplesToNextStep_);
      currentStep_.store(nextStep_);
      nextStep_ = (nextStep_ + 1) % kSteps;
    }

    // Render up to the next step boundary or roll hit, so triggers land on
    // the exact sample.
    size_t chunk = n;
    if (running_ && samplesToNextStep_ < chunk) chunk = samplesToNextStep_;
    for (int t = 0; t < kTracks; ++t) {
      if (rollLeft_[t] && rollCountdown_[t] < chunk) chunk = rollCountdown_[t];
    }
    kit_.render(out, chunk);
    out += chunk;
    n -= chunk;
    for (int t = 0; t < kTracks; ++t) {
      if (!rollLeft_[t]) continue;
      rollCountdown_[t] -= chunk;
      if (rollCountdown_[t] == 0) {
        triggerStep(t, rollStep_[t], rollLevel_[t]);
        rollLevel_[t] *= 0.8f;
        rollCountdown_[t] = rollInterval_[t];
        --rollLeft_[t];
      }
    }
    if (running_) samplesToNextStep_ -= chunk;
  }

  crush(start, total);
}
