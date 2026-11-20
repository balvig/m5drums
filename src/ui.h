// The screens and the three buttons. The same code runs on the device and,
// with a stand-in for M5Unified, in the simulator on a computer
// (sim/sim_main.cpp), so it must not touch any hardware except through M5,
// storage, bluetooth and exporter.
//
// Only what changed is redrawn: full-screen redraws cause audible glitches
// on the device.
#pragma once
#include "sequencer.h"

namespace ui {

// Draws the screen. `restored` false means nothing was saved: the sequencer
// then starts as NEW does, with random sounds, tempo and swing.
// Call once, after M5.begin() and seq.init() (or storage::load()).
void begin(Sequencer& seq, bool restored);

// Call on every pass of the main loop, after M5.update(): acts on the
// buttons, redraws what has changed, and saves the session once it has been
// left alone for a moment.
void update();

}  // namespace ui
