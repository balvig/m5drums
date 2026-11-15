#include "storage.h"

#include <Preferences.h>

namespace storage {

namespace {

constexpr uint8_t kVersion = 1;
const char* const kNamespace = "m5drums";
const char* const kKey = "session";

struct Session {
  uint8_t version;
  uint8_t swing;
  uint16_t bpm;
  uint32_t kitSeed;
  uint8_t pattern[Sequencer::kTracks];
  int8_t pitch[Sequencer::kTracks][Sequencer::kSteps];
  int8_t decay[Sequencer::kTracks][Sequencer::kSteps];
};

}  // namespace

bool load(Sequencer& seq) {
  Session s;
  Preferences prefs;
  // Opened read-write: a read-only open logs an error on first boot, when the
  // namespace does not exist yet.
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.getBytesLength(kKey) == sizeof(s) &&
            prefs.getBytes(kKey, &s, sizeof(s)) == sizeof(s) && s.version == kVersion;
  prefs.end();
  if (!ok) return false;

  seq.init(s.kitSeed);
  seq.setTempo(s.bpm);
  seq.setSwing(s.swing);
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    seq.setPattern(t, s.pattern[t]);
    for (int st = 0; st < Sequencer::kSteps; ++st) {
      seq.setStepPitch(t, st, s.pitch[t][st]);
      seq.setStepDecay(t, st, s.decay[t][st]);
    }
  }
  return true;
}

void save(const Sequencer& seq) {
  Session s = {};
  s.version = kVersion;
  s.swing = seq.swing();
  s.bpm = seq.tempo();
  s.kitSeed = seq.kitSeed();
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    s.pattern[t] = seq.pattern(t);
    for (int st = 0; st < Sequencer::kSteps; ++st) {
      s.pitch[t][st] = seq.stepPitch(t, st);
      s.decay[t][st] = seq.stepDecay(t, st);
    }
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBytes(kKey, &s, sizeof(s));
  prefs.end();
}

}  // namespace storage
