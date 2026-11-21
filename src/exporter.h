// Renders the pattern to a 16-bit mono WAV file on the microSD card. The
// audio is computed offline from a copy of the sequencer, so it never passes
// through the 8-bit DAC and is not affected by what is playing live.
#pragma once
#include <stddef.h>

#include "sequencer.h"

namespace exporter {

enum Result {
  kOk,
  kNoCard,       // nothing in the slot
  kUnreadable,   // a card is in the slot, but it has no file system this can read
  kWriteFailed,
};

// Writes one evolve phrase (or, with evolve off, a few repeats of the
// pattern), then lets the last hits ring out.
// On success, name receives the file name (without the leading slash).
// Blocks for a second or two; UI loop only.
Result exportWav(const Sequencer& live, char* name, size_t nameSize);

// Erases a card that cannot be read and gives it a file system (FAT) that
// can. Only a card with no file system the card library knows is touched: one
// that can be read after all is left as it is. Returns whether the card can
// be read now. Blocks for several seconds (about 8 for the card tried in
// sketchbox-m5, longer for a larger one); UI loop only.
bool formatCard();

}  // namespace exporter
