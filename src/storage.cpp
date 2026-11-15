#include "storage.h"

#include <Preferences.h>

namespace storage {

namespace {

constexpr uint8_t kVersion = 2;
constexpr size_t kVersion1Size = 60;  // version 1 had no per-step level
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
  int8_t level[Sequencer::kTracks][Sequencer::kSteps];  // added in version 2, keep last
};

}  // namespace

bool load(Sequencer& seq) {
  Session s = {};
  Preferences prefs;
  // Opened read-write: a read-only open logs an error on first boot, when the
  // namespace does not exist yet.
  if (!prefs.begin(kNamespace, false)) return false;
  size_t size = prefs.getBytesLength(kKey);
  bool ok = (size == sizeof(s) || size == kVersion1Size) &&
            prefs.getBytes(kKey, &s, size) == size &&
            ((s.version == kVersion && size == sizeof(s)) ||
             (s.version == 1 && size == kVersion1Size));
  prefs.end();
  if (!ok) return false;
  if (s.version == 1) memset(s.level, 0, sizeof(s.level));  // older save: flat levels

  seq.init(s.kitSeed);
  seq.setTempo(s.bpm);
  seq.setSwing(s.swing);
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    seq.setPattern(t, s.pattern[t]);
    for (int st = 0; st < Sequencer::kSteps; ++st) {
      seq.setStepPitch(t, st, s.pitch[t][st]);
      seq.setStepDecay(t, st, s.decay[t][st]);
      seq.setStepLevel(t, st, s.level[t][st]);
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
      s.level[t][st] = seq.stepLevel(t, st);
    }
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBytes(kKey, &s, sizeof(s));
  prefs.end();
}

}  // namespace storage
