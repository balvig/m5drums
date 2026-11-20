// The device: sets up the hardware and runs the main loop. The screens and
// the buttons are in ui.cpp; the sounds and the sequencer in lib/drums.
#include <M5Unified.h>

#include <sys/time.h>
#include <time.h>

#include "audio.h"
#include "bluetooth.h"
#include "lineout.h"
#include "storage.h"
#include "ui.h"

namespace {

Sequencer seq;

// The Gray has no clock, so exported files would be stamped 1 Jan 1980.
// Start the system clock at the moment this firmware was built instead: not
// the real time, but a plausible date, and later exports sort after earlier
// ones within a session.
void setClockFromBuildTime() {
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char month[4] = "";
  struct tm t = {};
  sscanf(__DATE__, "%3s %d %d", month, &t.tm_mday, &t.tm_year);
  sscanf(__TIME__, "%d:%d:%d", &t.tm_hour, &t.tm_min, &t.tm_sec);
  const char* found = strstr(kMonths, month);
  t.tm_mon = found ? (found - kMonths) / 3 : 0;
  t.tm_year -= 1900;
  struct timeval now = {mktime(&t), 0};
  settimeofday(&now, nullptr);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 0 (the default) leaves Serial off
  M5.begin(cfg);
  setClockFromBuildTime();

  // Red power button: one short press turns the device on, and off again.
  // These are settings in the IP5306 power chip (I2C 0x75, register 0x01):
  // bit 5 = on/off with one short press; bit 7 = long press instead of double
  // press, which must stay clear (with it set, the device would not turn on
  // from battery). The chip keeps the settings while the battery is connected.
  M5.In_I2C.bitOff(0x75, 0x01, 0x80, 100000);
  M5.In_I2C.bitOn(0x75, 0x01, 0x20, 100000);

  // Pick up where the last session left off. With nothing saved, start as
  // NEW PATTERN does.
  bool restored = storage::load(seq);
  if (!restored) seq.init(0);
  ui::begin(seq, restored);

  M5.Speaker.setVolume(160);
  // TEMPORARY: which chips are on the I2C bus, to tell the Gray from a Basic
  // and see whether the audio module is fitted.
  Serial.print("i2c:");
  for (int addr = 8; addr < 120; ++addr) {
    if (M5.In_I2C.scanID(addr)) Serial.printf(" 0x%02X", addr);
  }
  Serial.println();
  if (!lineout::begin()) Serial.println("lineout: no audio module");
  audio::begin(seq);
  Serial.printf("m5drums: ready (battery %d%%, charging %d, power reg 0x%02X)\n",
                (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging(),
                M5.In_I2C.readRegister8(0x75, 0x01, 100000));
}

void loop() {
  M5.update();
  ui::update();
  lineout::poll();

  // Once a Bluetooth speaker has connected, start sending to it straight away.
  static bluetooth::State bluetoothWas = bluetooth::kOff;
  if (bluetooth::state() != bluetoothWas) {
    bluetoothWas = bluetooth::state();
    if (bluetoothWas == bluetooth::kConnected) bluetooth::startStream();
  }

  delay(2);
}
