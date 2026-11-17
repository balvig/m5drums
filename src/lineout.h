// Output through M5Stack's Module Audio (M144): an ES8388 codec with a
// headphone jack, stacked on the core. Optional: without the module the
// built-in speaker is used, and with it, only while something is plugged in.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lineout {

// Looks for the module and sets it up. Returns false if it is not fitted.
// Call once, after M5.begin().
bool begin();

// Checks whether something is plugged into the jack. Call regularly from the
// UI loop; it talks to the module over I2C.
void poll();

// True when the module is fitted and something is plugged in: audio should
// go to the jack rather than the built-in speaker.
bool active();

// Sends n mono 16-bit samples at 44.1 kHz to the jack. Blocks until the
// codec has room, which paces the caller at the output sample rate.
void write(const int16_t* mono, size_t n);

}  // namespace lineout
