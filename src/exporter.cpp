#include "exporter.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "wav.h"

namespace exporter {

namespace {

constexpr uint32_t kMaxTailSamples = voices::kSampleRate * 4;
constexpr size_t kChunkSamples = 512;

Sequencer offline;  // a private copy, rendered as fast as possible
int16_t chunk[kChunkSamples];

// Renders and writes n samples. Returns false if the card stops accepting data.
bool writeSamples(File& file, uint32_t n) {
  while (n > 0) {
    size_t count = n < kChunkSamples ? n : kChunkSamples;
    offline.render(chunk, count);
    size_t bytes = count * sizeof(int16_t);
    // The ESP32 is little-endian, as WAV requires.
    if (file.write((const uint8_t*)chunk, bytes) != bytes) return false;
    n -= count;
  }
  return true;
}

Result writeFile(File& file, const Sequencer& live) {
  uint8_t header[wav::kHeaderSize] = {};
  if (file.write(header, wav::kHeaderSize) != wav::kHeaderSize) return kWriteFailed;  // filled in at the end

  offline.copyFrom(live);
  offline.setPlaying(true);

  // With evolve on, one whole phrase: the offline copy starts at the first
  // pass, as live playback does, and evolves identically.
  uint32_t bodySamples = wav::bodySamples(live);
  if (!writeSamples(file, bodySamples)) return kWriteFailed;

  // Stop and let the last hits ring out.
  offline.setPlaying(false);
  uint32_t tailSamples = 0;
  while (tailSamples < kMaxTailSamples) {
    if (!writeSamples(file, kChunkSamples)) return kWriteFailed;
    tailSamples += kChunkSamples;
    if (!offline.hasAudio()) break;
  }

  wav::buildHeader(header, (bodySamples + tailSamples) * sizeof(int16_t));
  if (!file.seek(0) || file.write(header, wav::kHeaderSize) != wav::kHeaderSize) return kWriteFailed;
  return kOk;
}

}  // namespace

Result exportWav(const Sequencer& live, char* name, size_t nameSize) {
  // The card shares the display's SPI bus; M5Unified knows the chip-select pin.
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (!SD.begin(cs, SPI, 25000000)) return kNoCard;

  // First unused drumsNNN.wav
  char path[24];
  int index = 1;
  for (; index <= 999; ++index) {
    snprintf(path, sizeof(path), "/drums%03d.wav", index);
    if (!SD.exists(path)) break;
  }
  Result result = kWriteFailed;
  if (index <= 999) {
    File file = SD.open(path, FILE_WRITE);
    if (file) {
      result = writeFile(file, live);
      file.close();
      if (result != kOk) SD.remove(path);
    }
  }
  SD.end();

  if (result == kOk) snprintf(name, nameSize, "%s", path + 1);
  return result;
}

}  // namespace exporter
