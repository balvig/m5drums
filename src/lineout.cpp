#include "lineout.h"

#include <M5Module_Audio.h>
#include <M5Unified.h>

#include <atomic>

namespace lineout {

namespace {

constexpr uint8_t kModuleAddress = 0x33;  // the module's own controller
constexpr uint32_t kPollMs = 250;
// Output amplifier level, 0-100 in 33 steps of 1.5 dB: 91 is 0 dB, 100 is
// +4.5 dB. Kept a little below 0 dB, with the digital signal sent hot instead
// (see write()): the amplifier then turns down the codec's own hiss along
// with the signal, rather than amplifying a quiet signal and the hiss together.
constexpr int kVolume = 82;  // -4.5 dB
constexpr uint8_t kCodecAddress = 0x10;  // the ES8388 itself
constexpr uint32_t kI2cHz = 400000;
constexpr size_t kMaxFrames = 256;

// The built-in speaker's DAC needs I2S port 0, so the module takes port 1.
M5ModuleAudio module(I2S_NUM_1);
bool fitted = false;
std::atomic<bool> pluggedIn{false};
uint32_t polledAt = 0;
int16_t stereo[kMaxFrames * 2];

}  // namespace

bool begin() {
  if (!M5.In_I2C.scanID(kModuleAddress)) return false;
  // Sets up I2S on the core's standard audio pins, and the codec, at 44.1 kHz.
  if (!module.begin(M5.In_I2C)) {
    Serial.println("lineout: module found but could not be set up");
    return false;
  }
  // As in M5Stack's own playback example.
  module.setHPMode(AUDIO_HPMODE_NATIONAL);
  module.setSpeakerOutput(DAC_OUTPUT_OUT1);
  module.setBitsSample(ES_MODULE_ADC_DAC, BIT_LENGTH_16BITS);
  module.setSampleRate(SAMPLE_RATE_44K);
  module.setSpeakerVolume(kVolume);
  // Only playback is used. Close both microphone paths and power down the
  // codec's input side (ADC and mic bias) so they cannot add noise, and set
  // the DAC's digital volume to full (the library leaves it 2.5 dB down).
  module.setMICStatus(AUDIO_MIC_CLOSE);
  module.setHPMICStatus(AUDIO_MIC_CLOSE);
  M5.In_I2C.writeRegister8(kCodecAddress, 0x03, 0xFF, kI2cHz);  // ADC power: all off
  M5.In_I2C.writeRegister8(kCodecAddress, 0x1A, 0x00, kI2cHz);  // left DAC volume: 0 dB
  M5.In_I2C.writeRegister8(kCodecAddress, 0x1B, 0x00, kI2cHz);  // right DAC volume: 0 dB
  fitted = true;
  pluggedIn.store(module.getHPInsertStatus() == 1);
  Serial.printf("lineout: module ready, firmware 0x%02X, jack %s\n", module.getFirmwareVersion(),
                pluggedIn.load() ? "in use" : "empty");
  return true;
}

void poll() {
  if (!fitted || millis() - polledAt < kPollMs) return;
  polledAt = millis();
  bool now = module.getHPInsertStatus() == 1;
  if (now != pluggedIn.exchange(now)) Serial.printf("lineout: jack %s\n", now ? "in use" : "empty");
}

bool active() { return fitted && pluggedIn.load(); }

void write(const int16_t* mono, size_t n) {
  if (n > kMaxFrames) n = kMaxFrames;
  // The mix leaves a lot of headroom for the built-in speaker. Here it is
  // doubled (+6 dB), limited at full scale, to sit well above the codec's noise.
  for (size_t i = 0; i < n; ++i) {
    int32_t v = mono[i] * 2;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    stereo[2 * i] = (int16_t)v;
    stereo[2 * i + 1] = (int16_t)v;
  }
  module.play((const uint8_t*)stereo, n * 2 * sizeof(int16_t));
}

}  // namespace lineout
