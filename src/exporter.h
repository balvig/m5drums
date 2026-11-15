// Renders the pattern to a 16-bit mono WAV file on the microSD card. The
// audio is computed offline from a copy of the sequencer, so it never passes
// through the 8-bit DAC and is not affected by what is playing live.
#pragma once
#include <stddef.h>

#include "sequencer.h"

namespace exporter {

enum Result { kOk, kNoCard, kWriteFailed };

// Writes one evolve phrase (or, with evolve off, a few repeats of the
// pattern), then lets the last hits ring out.
// On success, name receives the file name (without the leading slash).
// Blocks for a second or two; UI loop only.
Result exportWav(const Sequencer& live, char* name, size_t nameSize);

}  // namespace exporter
