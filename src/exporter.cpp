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

// Is there a card in the slot? Asks whatever is there to reset itself, the
// first step of starting any card: a card answers "idle" within a few bytes.
// Only for a slot the card library has no card mounted in. (From sketchbox-m5.)
bool cardAnswers() {
  const int select = M5.getPin(m5::pin_name_t::sd_spi_cs);
  static const uint8_t reset[] = {0x40, 0, 0, 0, 0, 0x95};  // "go idle", with its checksum
  uint8_t answer = 0xFF;
  pinMode(select, OUTPUT);
  digitalWrite(select, HIGH);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int attempt = 0; attempt < 2 && answer != 0x01; ++attempt) {
    for (int i = 0; i < 10; ++i) SPI.transfer(0xFF);  // clocks, for a card that has just got power
    digitalWrite(select, LOW);
    for (uint8_t byte : reset) SPI.transfer(byte);
    answer = 0xFF;
    for (int i = 0; i < 16 && (answer & 0x80); ++i) answer = SPI.transfer(0xFF);
    digitalWrite(select, HIGH);
    SPI.transfer(0xFF);
  }
  SPI.endTransaction();
  return answer == 0x01;
}

}  // namespace

bool formatCard() {
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  uint32_t began = millis();
  // The last argument: format the card if it has no file system that mounts.
  bool ok = SD.begin(cs, SPI, 25000000, "/sd", 5, true);
  SD.end();
  Serial.printf("card: %s after %u ms\n", ok ? "formatted" : "still cannot be read",
                (unsigned)(millis() - began));
  return ok;
}

Result exportWav(const Sequencer& live, char* name, size_t nameSize) {
  // The card shares the display's SPI bus; M5Unified knows the chip-select pin.
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (!SD.begin(cs, SPI, 25000000)) {
    SD.end();
    // Nothing in the slot, or a card that cannot be read (not formatted, say)?
    return cardAnswers() ? kUnreadable : kNoCard;
  }

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
