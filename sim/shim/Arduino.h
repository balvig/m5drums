// Stand-in for the few Arduino things the screens use, for the simulator on
// a computer (see sim/sim_main.cpp). Never built for the device.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The simulator's clock: it only moves when the simulator moves it, so that
// the same steps always give the same picture.
namespace sim {
extern uint32_t clockMs;
uint32_t random32();  // the same numbers on every run
void restart();       // what ESP.restart() does here
}  // namespace sim

inline uint32_t millis() { return sim::clockMs; }
inline void delay(uint32_t ms) { sim::clockMs += ms; }
inline uint32_t esp_random() { return sim::random32(); }

struct SimEsp {
  void restart() { sim::restart(); }
};
extern SimEsp ESP;
