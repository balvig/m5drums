#include "sequencer.h"

void Sequencer::init(uint32_t seed) { kit_.init(seed); }

void Sequencer::triggerStep(int track, int step) {
  kit_.trigger((voices::Voice)track, pitch_[track][step].load(), decay_[track][step].load());
}

void Sequencer::render(int16_t* out, size_t n) {
  uint32_t auditions = auditions_.exchange(0);
  for (int t = 0; t < kTracks; ++t) {
    if (auditions & (1u << t)) triggerStep(t, auditionStep_[t].load());
  }

  bool wanted = playRequested_.load();
  if (wanted && !running_) {
    running_ = true;
    nextStep_ = 0;
    samplesToNextStep_ = 0;
  } else if (!wanted && running_) {
    running_ = false;
    currentStep_.store(-1);
  }

  while (n > 0) {
    if (running_ && samplesToNextStep_ == 0) {
      for (int t = 0; t < kTracks; ++t) {
        if (pattern_[t].load() & (1u << nextStep_)) triggerStep(t, nextStep_);
      }
      currentStep_.store(nextStep_);

      // Steps are 16th notes. A pair of them lasts one 8th note; swing splits
      // the pair unevenly. Tempo and swing are read here, so changes apply
      // from the next step.
      uint32_t pairSamples = voices::kSampleRate * 30u / bpm_.load();
      uint32_t firstSamples = pairSamples * swing_.load() / 100u;
      samplesToNextStep_ = (nextStep_ % 2 == 0) ? firstSamples : pairSamples - firstSamples;
      nextStep_ = (nextStep_ + 1) % kSteps;
    }

    // Render up to the next step boundary so triggers land on the exact sample.
    size_t chunk = n;
    if (running_ && samplesToNextStep_ < chunk) chunk = samplesToNextStep_;
    kit_.render(out, chunk);
    out += chunk;
    n -= chunk;
    if (running_) samplesToNextStep_ -= chunk;
  }
}
