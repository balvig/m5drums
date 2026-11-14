#include "audio.h"

#include <M5Unified.h>

namespace audio {

namespace {

constexpr size_t kBufferSamples = 256;  // ~5.8 ms at 44.1 kHz
constexpr int kSpeakerChannel = 0;

// The speaker keeps a pointer to each buffer (no copy) and holds at most two
// per channel: one playing, one queued. Three buffers means the one being
// rendered is never one the speaker still owns.
int16_t buffers[3][kBufferSamples];

void audioTask(void* arg) {
  Sequencer& seq = *static_cast<Sequencer*>(arg);
  size_t index = 0;
  for (;;) {
    int16_t* buf = buffers[index];
    seq.render(buf, kBufferSamples);

    // Returns once the speaker has a free queue slot, which paces this task
    // at exactly the output sample rate.
    M5.Speaker.playRaw(buf, kBufferSamples, voices::kSampleRate, false, 1,
                       kSpeakerChannel, false);
    index = (index + 1) % 3;
  }
}

}  // namespace

void begin(Sequencer& seq) {
  // Core 0 is otherwise idle (no WiFi/BT); the Arduino loop runs on core 1.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, &seq, 3, nullptr, 0);
}

}  // namespace audio
