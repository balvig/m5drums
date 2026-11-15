// Audio output: a FreeRTOS task that renders the sequencer into small buffers
// and hands them to the speaker.
#pragma once
#include "sequencer.h"

namespace audio {

// Starts the audio task. Call after M5.begin() and seq.init().
void begin(Sequencer& seq);

// The sequencer may only be rendered by one thing at a time: this task for
// the speaker, or the Bluetooth stack. Whoever wants to render claims it
// first; a false return means someone else is rendering, so play silence.
bool claimRenderer();
void releaseRenderer();

}  // namespace audio
