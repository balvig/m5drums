#include "audio.h"

#include <M5Unified.h>

#include <math.h>

namespace audio {

namespace {

constexpr size_t kBufferSamples = 256;  // ~5.8 ms at 44.1 kHz
constexpr int kSpeakerChannel = 0;

// The speaker keeps a pointer to each buffer (no copy) and holds at most two
// per channel: one playing, one queued. Three buffers means the one being
// rendered is never one the speaker still owns.
int16_t buffers[3][kBufferSamples];
size_t bufferIndex = 0;

// Hands a rendered buffer to the speaker and moves on to the next one.
// Returns once the speaker has a free queue slot, which paces the audio task
// at exactly the output sample rate.
void sendBuffer() {
  M5.Speaker.playRaw(buffers[bufferIndex], kBufferSamples, voices::kSampleRate, false, 1,
                     kSpeakerChannel, false);
  bufferIndex = (bufferIndex + 1) % 3;
}

// The speaker driver idles its DAC at zero and only lifts the resting level
// when a loud sample arrives, which mangles the start of the first hit after
// silence. The primer lifts it beforehand without making a sound: a negative
// sample raises the driver's resting level by its own size, so the two cancel
// and the output stays at zero; easing the sample back to zero then lets the
// output glide up to the new level, too slowly to hear.
constexpr int kPrimerBuffers = 8;         // ~46 ms
constexpr float kPrimerLevel = 16384.0f;  // the peak of one voice

void sendPrimer() {
  constexpr float kPi = 3.14159265f;
  constexpr int kTotal = kPrimerBuffers * kBufferSamples;
  int n = 0;
  for (int b = 0; b < kPrimerBuffers; ++b) {
    int16_t* buf = buffers[bufferIndex];
    for (size_t i = 0; i < kBufferSamples; ++i, ++n) {
      float ease = 0.5f * (1.0f + cosf(kPi * n / kTotal));  // 1 -> 0
      buf[i] = (int16_t)(-kPrimerLevel * ease);
    }
    sendBuffer();
  }
}

void audioTask(void* arg) {
  Sequencer& seq = *static_cast<Sequencer*>(arg);
  bool idle = true;
  for (;;) {
    // Stopped and silent: send nothing. With its queue empty the speaker
    // driver ramps the DAC down and switches the output off, which removes
    // the idle hiss that a stream of silent buffers would keep alive.
    if (!seq.hasAudio()) {
      idle = true;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    // Waking up to play: get the output ready before the first hit.
    if (idle && seq.playing()) sendPrimer();
    idle = false;

    seq.render(buffers[bufferIndex], kBufferSamples);
    sendBuffer();
  }
}

}  // namespace

void begin(Sequencer& seq) {
  // Core 0 is otherwise idle (no WiFi/BT); the Arduino loop runs on core 1.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, &seq, 3, nullptr, 0);
}

}  // namespace audio
