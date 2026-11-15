#include "bluetooth.h"

#include <BluetoothA2DPSource.h>

#include <atomic>

#include "audio.h"

namespace bluetooth {

namespace {

constexpr size_t kChunkSamples = 256;

BluetoothA2DPSource source;
Sequencer* sequencer = nullptr;
std::atomic<int> currentState{kOff};
char foundName[24] = "";
int16_t mono[kChunkSamples];

// Called by the Bluetooth stack whenever it wants more audio: 44.1 kHz
// stereo. The frames arrive zeroed, so returning early plays silence.
int32_t fillFrames(Frame* frames, int32_t count) {
  if (currentState.load() != kConnected || !audio::claimRenderer()) return count;
  if (sequencer->hasAudio()) {
    int32_t done = 0;
    while (done < count) {
      int32_t n = count - done < (int32_t)kChunkSamples ? count - done : (int32_t)kChunkSamples;
      sequencer->render(mono, n);
      for (int32_t i = 0; i < n; ++i) {
        frames[done + i].channel1 = mono[i];
        frames[done + i].channel2 = mono[i];
      }
      done += n;
    }
  }
  audio::releaseRenderer();
  return count;
}

// Called for each audio device the search turns up. Takes the first one.
bool pickDevice(const char* name, esp_bd_addr_t, int) {
  snprintf(foundName, sizeof(foundName), "%s", name);
  return true;
}

void onConnectionState(esp_a2d_connection_state_t s, void*) {
  if (s == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    currentState.store(kConnected);
  } else if (s == ESP_A2D_CONNECTION_STATE_CONNECTING) {
    currentState.store(kConnecting);
  } else {
    currentState.store(kSearching);
  }
}

}  // namespace

void start(Sequencer& seq) {
  if (currentState.load() != kOff) return;
  sequencer = &seq;
  currentState.store(kSearching);
  source.set_auto_reconnect(false);  // always search, rather than wait for the last device
  source.set_ssid_callback(pickDevice);
  source.set_on_connection_state_changed(onConnectionState);
  source.set_volume(100);
  source.start(fillFrames);
}

void startStream() {
  // The library's own state machine picks up the reply and starts the stream.
  if (currentState.load() == kConnected) esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
}

State state() { return (State)currentState.load(); }

const char* deviceName() { return foundName; }

}  // namespace bluetooth
