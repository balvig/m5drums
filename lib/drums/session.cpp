#include "session.h"

#include <string.h>

namespace session {

void pack(const Sequencer& seq, uint8_t* out) {
  Packed s = {};
  s.version = kVersion;
  s.swing = seq.swing();
  s.bpm = seq.tempo();
  s.kitSeed = seq.kitSeed();
  s.evolveTheme = seq.evolveTheme();
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    s.pattern[t] = seq.pattern(t);
    for (int st = 0; st < Sequencer::kSteps; ++st) {
      s.pitch[t][st] = seq.stepPitch(t, st);
      s.decay[t][st] = seq.stepDecay(t, st);
      s.shape[t][st] = seq.stepShape(t, st);
    }
  }
  memcpy(out, &s, sizeof(s));
}

bool unpack(const uint8_t* bytes, size_t length, Sequencer& seq) {
  Packed s;
  if (length != sizeof(s)) return false;
  memcpy(&s, bytes, sizeof(s));
  if (s.version != kVersion) return false;

  seq.init(s.kitSeed);
  seq.setTempo(s.bpm);
  seq.setSwing(s.swing);
  seq.setEvolveTheme(s.evolveTheme);
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    seq.setPattern(t, s.pattern[t]);
    for (int st = 0; st < Sequencer::kSteps; ++st) {
      seq.setStepPitch(t, st, s.pitch[t][st]);
      seq.setStepDecay(t, st, s.decay[t][st]);
      seq.setStepShape(t, st, s.shape[t][st]);
    }
  }
  return true;
}

}  // namespace session
