// Experimental: plays through a Bluetooth speaker or headphones (A2DP).
// While one is connected it takes over from the built-in speaker.
#pragma once
#include "sequencer.h"

namespace bluetooth {

enum State { kOff, kSearching, kConnecting, kConnected };

// Turns Bluetooth on and starts looking for a speaker. It connects to the
// first audio device it finds, so put the one you want in pairing mode.
// There is no way back to kOff short of restarting the device.
void start(Sequencer& seq);

State state();
inline bool connected() { return state() == kConnected; }
// Name of the device found, or "" before one is.
const char* deviceName();

// Asks for the audio stream to start now. Call once a device has connected.
// Left alone, the library only starts it on a timer that fires every 10 s,
// and until then nothing pulls audio, so pressing play appears to do nothing.
void startStream();

}  // namespace bluetooth
