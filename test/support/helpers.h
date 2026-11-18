// Shared by the test suites.
#pragma once
#include <stdio.h>
#include <unity.h>

#include <vector>

#include "sequencer.h"

constexpr size_t kBlock = 256;  // the device's audio buffer size

// Renders `total` samples in device-sized buffers.
inline std::vector<int16_t> renderSamples(Sequencer& seq, size_t total) {
  std::vector<int16_t> out(total);
  for (size_t done = 0; done < total; done += kBlock) {
    seq.render(out.data() + done, total - done < kBlock ? total - done : kBlock);
  }
  return out;
}

// Index of the first sample where a and b differ, or -1 if they are the same.
inline long firstDifference(const std::vector<int16_t>& a, const std::vector<int16_t>& b) {
  if (a.size() != b.size()) return 0;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return (long)i;
  }
  return -1;
}

inline void assertSameAudio(const std::vector<int16_t>& a, const std::vector<int16_t>& b,
                            const char* what) {
  long at = firstDifference(a, b);
  if (at < 0) return;
  char message[120];
  snprintf(message, sizeof(message), "%s: audio differs from sample %ld", what, at);
  TEST_FAIL_MESSAGE(message);
}

inline int peakOf(const std::vector<int16_t>& samples) {
  int peak = 0;
  for (int16_t s : samples) {
    int a = s < 0 ? -s : s;
    if (a > peak) peak = a;
  }
  return peak;
}
