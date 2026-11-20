// The parts of a WAV export that are plain arithmetic: the file header and
// how long the pattern's audio is. Tested on a computer; src/exporter.cpp
// does the rendering and the writing to the card.
#pragma once
#include <stdint.h>
#include <string.h>

#include "sequencer.h"

namespace wav {

constexpr size_t kHeaderSize = 44;
constexpr int kRepeats = 4;  // passes of the pattern written when evolve is off

inline void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
inline void put32(uint8_t* p, uint32_t v) {
  put16(p, (uint16_t)v);
  put16(p + 2, (uint16_t)(v >> 16));
}

// Canonical 44-byte header for 16-bit mono PCM at the kit's sample rate.
inline void buildHeader(uint8_t* h, uint32_t dataBytes) {
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

// Samples from the first hit to the loop point of what gets exported: one
// whole evolve phrase, or kRepeats passes of the pattern with evolve off.
// The pattern is 4 pairs of 16th notes; this matches the sequencer's own
// timing sample for sample, so the body ends exactly on the loop point.
inline uint32_t bodySamples(const Sequencer& seq) {
  uint32_t pairSamples = voices::kSampleRate * 30u / seq.tempo();
  int passes = seq.evolvePasses() > 0 ? seq.evolvePasses() : kRepeats;
  return passes * (Sequencer::kSteps / 2) * pairSamples;
}

}  // namespace wav
