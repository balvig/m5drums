// Audio output: a FreeRTOS task that renders the sequencer into small buffers
// and hands them to the speaker.
#pragma once
#include "sequencer.h"

namespace audio {

// Starts the audio task. Call after M5.begin() and seq.init().
void begin(Sequencer& seq);

}  // namespace audio
