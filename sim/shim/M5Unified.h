// Stand-in for M5Unified in the simulator: the real display library drawing
// into a window, and buttons and a battery that the simulator sets.
#pragma once
#include <M5GFX.h>

#include "Arduino.h"

namespace sim {

struct Button {
  bool down = false;
  bool isPressed() const { return down; }
};

struct Power {
  int level = 75;
  bool charging = false;
  int getBatteryLevel() { return level; }
  bool isCharging() { return charging; }
};

struct Device {
  M5GFX Display;
  Button BtnA, BtnB, BtnC;
  sim::Power Power;
};

}  // namespace sim

extern sim::Device M5;
