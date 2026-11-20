// The WAV export's header and length.
#include <unity.h>

#include <memory>

#include "helpers.h"
#include "wav.h"

namespace {

uint32_t get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t get16(const uint8_t* p) { return p[0] | p[1] << 8; }

}  // namespace

void setUp() {}
void tearDown() {}

void test_header_is_16_bit_mono_at_44100() {
  uint8_t h[wav::kHeaderSize];
  wav::buildHeader(h, 88200);
  TEST_ASSERT_EQUAL_MEMORY("RIFF", h, 4);
  TEST_ASSERT_EQUAL_UINT32(36 + 88200, get32(h + 4));  // the file's size, less these 8 bytes
  TEST_ASSERT_EQUAL_MEMORY("WAVEfmt ", h + 8, 8);
  TEST_ASSERT_EQUAL_UINT32(16, get32(h + 16));
  TEST_ASSERT_EQUAL_UINT16(1, get16(h + 20));  // PCM
  TEST_ASSERT_EQUAL_UINT16(1, get16(h + 22));  // mono
  TEST_ASSERT_EQUAL_UINT32(44100, get32(h + 24));
  TEST_ASSERT_EQUAL_UINT32(88200, get32(h + 28));  // bytes per second
  TEST_ASSERT_EQUAL_UINT16(2, get16(h + 32));      // bytes per frame
  TEST_ASSERT_EQUAL_UINT16(16, get16(h + 34));
  TEST_ASSERT_EQUAL_MEMORY("data", h + 36, 4);
  TEST_ASSERT_EQUAL_UINT32(88200, get32(h + 40));
}

void test_body_is_four_passes_without_evolve() {
  auto seq = std::make_unique<Sequencer>();
  seq->init(1);
  seq->setTempo(120);  // a pass is one second
  TEST_ASSERT_EQUAL_UINT32(4 * 44100, wav::bodySamples(*seq));
}

void test_body_is_one_phrase_with_evolve() {
  auto seq = std::make_unique<Sequencer>();
  seq->init(1);
  seq->setTempo(120);
  seq->setEvolveTheme(3);
  TEST_ASSERT_EQUAL_UINT32(16 * 44100, wav::bodySamples(*seq));
}

// The body must end exactly where the sequencer starts the pattern again, at
// any tempo and swing, so that the exported file loops cleanly.
void test_body_ends_on_the_loop_point() {
  for (int bpm : {40, 97, 133, 170, 240}) {
    for (int swing : {50, 61, 75}) {
      for (int theme : {0, 2}) {
        auto seq = std::make_unique<Sequencer>();
        seq->init(1);
        seq->setTempo(bpm);
        seq->setSwing(swing);
        seq->setEvolveTheme(theme);
        seq->setPlaying(true);
        uint32_t body = wav::bodySamples(*seq);
        // One sample at a time for the last stretch: the pattern's last step
        // is sounding on the body's last sample, and its first on the next.
        renderSamples(*seq, body - 1);
        int16_t sample;
        seq->render(&sample, 1);
        TEST_ASSERT_EQUAL(Sequencer::kSteps - 1, seq->currentStep());
        seq->render(&sample, 1);
        TEST_ASSERT_EQUAL(0, seq->currentStep());
      }
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_is_16_bit_mono_at_44100);
  RUN_TEST(test_body_is_four_passes_without_evolve);
  RUN_TEST(test_body_is_one_phrase_with_evolve);
  RUN_TEST(test_body_ends_on_the_loop_point);
  return UNITY_END();
}
