#include "storage.h"

#include <Preferences.h>

namespace storage {

namespace {

constexpr uint8_t kVersion = 4;
constexpr size_t kVersion1Size = 60;  // version 1 had no third lane
constexpr size_t kVersion2Size = 84;  // version 2 had no evolve settings
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
  int8_t shape[Sequencer::kTracks][Sequencer::kSteps];  // added in version 2
  uint8_t evolveTheme;  // added in version 3 (as a bar count; a theme since version 4)
  uint8_t unused[2];    // version 3 kept an evolve depth and seed here
  // New fields go at the end, with a new version and size check in load().
};

}  // namespace

bool load(Sequencer& seq) {
  Session s = {};
  Preferences prefs;
  // Opened read-write: a read-only open logs an error on first boot, when the
  // namespace does not exist yet.
  if (!prefs.begin(kNamespace, false)) return false;
  size_t size = prefs.getBytesLength(kKey);
  bool ok = size > 0 && size <= sizeof(s) && prefs.getBytes(kKey, &s, size) == size &&
            (((s.version == kVersion || s.version == 3) && size == sizeof(s)) ||
             (s.version == 2 && size == kVersion2Size) ||
             (s.version == 1 && size == kVersion1Size));
  prefs.end();
  if (!ok) return false;
  // Older saves: fields they lack read as whatever followed, so set them.
  if (s.version < 2) memset(s.shape, 0, sizeof(s.shape));
  if (s.version < 3) s.evolveTheme = 0;
  if (s.version == 3) s.evolveTheme = s.evolveTheme ? 1 : 0;  // was on: first theme

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

void save(const Sequencer& seq) {
  Session s = {};
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

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putBytes(kKey, &s, sizeof(s));
  prefs.end();
}

}  // namespace storage
