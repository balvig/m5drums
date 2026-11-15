#include "exporter.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

namespace exporter {

namespace {

constexpr int kRepeats = 4;                               // times the 8-step pattern is played
constexpr uint32_t kMaxTailSamples = voices::kSampleRate * 4;
constexpr size_t kChunkSamples = 512;
constexpr size_t kHeaderSize = 44;

Sequencer offline;  // a private copy, rendered as fast as possible
int16_t chunk[kChunkSamples];

void put16(uint8_t* p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
void put32(uint8_t* p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}

// Canonical 44-byte header for 16-bit mono PCM.
void buildHeader(uint8_t* h, uint32_t dataBytes) {
  memcpy(h, "RIFF", 4);
  put32(h + 4, 36 + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16);                       // fmt chunk size
  put16(h + 20, 1);                        // PCM
  put16(h + 22, 1);                        // mono
  put32(h + 24, voices::kSampleRate);
  put32(h + 28, voices::kSampleRate * 2);  // bytes per second
  put16(h + 32, 2);                        // bytes per frame
  put16(h + 34, 16);                       // bits per sample
  memcpy(h + 36, "data", 4);
  put32(h + 40, dataBytes);
}

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
  uint8_t header[kHeaderSize] = {};
  if (file.write(header, kHeaderSize) != kHeaderSize) return kWriteFailed;  // filled in at the end

  offline.copyFrom(live);
  offline.setPlaying(true);

  // The pattern is 4 pairs of 16th notes; this matches the sequencer's own
  // timing sample for sample, so the body ends exactly on the loop point.
  uint32_t pairSamples = voices::kSampleRate * 30u / live.tempo();
  uint32_t bodySamples = kRepeats * (Sequencer::kSteps / 2) * pairSamples;
  if (!writeSamples(file, bodySamples)) return kWriteFailed;

  // Stop and let the last hits ring out.
  offline.setPlaying(false);
  uint32_t tailSamples = 0;
  while (tailSamples < kMaxTailSamples) {
    if (!writeSamples(file, kChunkSamples)) return kWriteFailed;
    tailSamples += kChunkSamples;
    if (!offline.hasAudio()) break;
  }

  buildHeader(header, (bodySamples + tailSamples) * sizeof(int16_t));
  if (!file.seek(0) || file.write(header, kHeaderSize) != kHeaderSize) return kWriteFailed;
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
