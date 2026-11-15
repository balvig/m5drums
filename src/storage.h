// Keeps the last session in the ESP32's flash (NVS) so it survives power off.
#pragma once
#include "sequencer.h"

namespace storage {

// Restores the saved session into seq, including seq.init() with the saved
// kit. Returns false, leaving seq untouched, if nothing usable is stored.
// Call before the audio task starts.
bool load(Sequencer& seq);

// Writes the session to flash. Takes a few milliseconds; UI loop only.
void save(const Sequencer& seq);

}  // namespace storage
