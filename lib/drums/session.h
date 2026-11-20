// A session (pattern, automation, tempo, swing, evolve theme and the seed the
// sounds come from) as a block of bytes, for keeping in flash. Plain C++, so
// it is tested on a computer; src/storage.cpp does the reading and writing.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "sequencer.h"

namespace session {

// Bump this whenever the layout below or the meaning of a kit seed changes.
// Bytes with any other version are refused and the device starts fresh;
// older saves are deliberately not converted.
constexpr uint8_t kVersion = 8;

struct Packed {
  uint8_t version;
  uint8_t swing;
  uint16_t bpm;
  uint32_t kitSeed;
  uint8_t pattern[Sequencer::kTracks];
  int8_t pitch[Sequencer::kTracks][Sequencer::kSteps];
  int8_t decay[Sequencer::kTracks][Sequencer::kSteps];
  int8_t shape[Sequencer::kTracks][Sequencer::kSteps];
  uint8_t evolveTheme;
};

constexpr size_t kBytes = sizeof(Packed);

// Writes the session as kBytes bytes.
void pack(const Sequencer& seq, uint8_t* out);

// Restores a session into seq, including seq.init() with its kit. Returns
// false, leaving seq untouched, unless the bytes are a session of this
// version. Not for a sequencer the audio task is using.
bool unpack(const uint8_t* bytes, size_t length, Sequencer& seq);

}  // namespace session
