// The saved session: what is packed comes back, and anything else is refused.
#include <unity.h>

#include <memory>
#include <vector>

#include "helpers.h"
#include "session.h"

namespace {

std::unique_ptr<Sequencer> makeSession() {
  auto seq = std::make_unique<Sequencer>();
  seq->init(987654321u);
  seq->setTempo(137);
  seq->setSwing(63);
  seq->setEvolveTheme(5);
  seq->setPattern(0, 0b10010001);
  seq->setPattern(1, 0b01000100);
  seq->setPattern(2, 0b11111111);
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    for (int s = 0; s < Sequencer::kSteps; ++s) {
      seq->setStepPitch(t, s, t * 5 - s * 2);
      seq->setStepDecay(t, s, s - 6 + t);
      seq->setStepShape(t, s, 4 - s - t);
    }
  }
  seq->setStepPitch(0, 0, -voices::kPitchRange);
  seq->setStepPitch(2, 7, voices::kPitchRange);
  return seq;
}

std::vector<uint8_t> packed(const Sequencer& seq) {
  std::vector<uint8_t> bytes(session::kBytes);
  session::pack(seq, bytes.data());
  return bytes;
}

// A sequencer in a state no save would produce, to show a refusal leaves it alone.
std::unique_ptr<Sequencer> makeMarked() {
  auto seq = std::make_unique<Sequencer>();
  seq->init(1);
  seq->setTempo(77);
  seq->setPattern(1, 0b00110011);
  return seq;
}

void assertUntouched(const Sequencer& seq) {
  TEST_ASSERT_EQUAL_UINT32(1, seq.kitSeed());
  TEST_ASSERT_EQUAL(77, seq.tempo());
  TEST_ASSERT_EQUAL_HEX8(0b00110011, seq.pattern(1));
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_session_survives_pack_and_unpack() {
  auto saved = makeSession();
  std::vector<uint8_t> bytes = packed(*saved);

  auto loaded = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(session::unpack(bytes.data(), bytes.size(), *loaded));
  TEST_ASSERT_EQUAL_UINT32(saved->kitSeed(), loaded->kitSeed());
  TEST_ASSERT_EQUAL(137, loaded->tempo());
  TEST_ASSERT_EQUAL(63, loaded->swing());
  TEST_ASSERT_EQUAL(5, loaded->evolveTheme());
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    TEST_ASSERT_EQUAL_HEX8(saved->pattern(t), loaded->pattern(t));
    for (int s = 0; s < Sequencer::kSteps; ++s) {
      TEST_ASSERT_EQUAL(saved->stepPitch(t, s), loaded->stepPitch(t, s));
      TEST_ASSERT_EQUAL(saved->stepDecay(t, s), loaded->stepDecay(t, s));
      TEST_ASSERT_EQUAL(saved->stepShape(t, s), loaded->stepShape(t, s));
    }
  }
  TEST_ASSERT_FALSE(loaded->playing());  // a restored session starts stopped
}

void test_restored_session_sounds_the_same() {
  std::vector<uint8_t> bytes = packed(*makeSession());
  auto loaded = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(session::unpack(bytes.data(), bytes.size(), *loaded));

  auto fresh = makeSession();
  fresh->setPlaying(true);
  loaded->setPlaying(true);
  const size_t seconds = 10;
  assertSameAudio(renderSamples(*fresh, seconds * voices::kSampleRate),
                  renderSamples(*loaded, seconds * voices::kSampleRate), "restored session");
}

void test_packing_twice_gives_the_same_bytes() {
  auto seq = makeSession();
  TEST_ASSERT_TRUE(packed(*seq) == packed(*seq));
  auto other = makeSession();
  other->toggleStep(0, 1);
  TEST_ASSERT_FALSE(packed(*seq) == packed(*other));
}

void test_first_byte_is_the_version() {
  TEST_ASSERT_EQUAL_UINT8(session::kVersion, packed(*makeSession())[0]);
}

// A save from firmware with a different session version is dropped, not converted.
void test_other_version_is_refused() {
  std::vector<uint8_t> bytes = packed(*makeSession());
  for (int version : {0, session::kVersion - 1, session::kVersion + 1, 255}) {
    bytes[0] = (uint8_t)version;
    auto seq = makeMarked();
    TEST_ASSERT_FALSE(session::unpack(bytes.data(), bytes.size(), *seq));
    assertUntouched(*seq);
  }
}

void test_wrong_size_is_refused() {
  std::vector<uint8_t> bytes = packed(*makeSession());
  auto seq = makeMarked();
  TEST_ASSERT_FALSE(session::unpack(bytes.data(), bytes.size() - 1, *seq));
  TEST_ASSERT_FALSE(session::unpack(bytes.data(), 0, *seq));
  bytes.resize(bytes.size() + 9);
  TEST_ASSERT_FALSE(session::unpack(bytes.data(), bytes.size(), *seq));
  assertUntouched(*seq);
}

// Flash can hold anything. Whatever the bytes say, the values end up in range.
void test_out_of_range_values_are_clamped() {
  std::vector<uint8_t> bytes(session::kBytes, 0x7F);
  bytes[0] = session::kVersion;
  auto seq = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(session::unpack(bytes.data(), bytes.size(), *seq));
  TEST_ASSERT_EQUAL(Sequencer::kMaxBpm, seq->tempo());
  TEST_ASSERT_EQUAL(Sequencer::kMaxSwing, seq->swing());
  TEST_ASSERT_EQUAL(Sequencer::kEvolveThemes, seq->evolveTheme());
  TEST_ASSERT_EQUAL(voices::kPitchRange, seq->stepPitch(1, 3));
  TEST_ASSERT_EQUAL(voices::kDecayRange, seq->stepDecay(1, 3));
  TEST_ASSERT_EQUAL(voices::kShapeRange, seq->stepShape(1, 3));
  seq->setPlaying(true);
  renderSamples(*seq, voices::kSampleRate);  // and it plays without falling over
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_session_survives_pack_and_unpack);
  RUN_TEST(test_restored_session_sounds_the_same);
  RUN_TEST(test_packing_twice_gives_the_same_bytes);
  RUN_TEST(test_first_byte_is_the_version);
  RUN_TEST(test_other_version_is_refused);
  RUN_TEST(test_wrong_size_is_refused);
  RUN_TEST(test_out_of_range_values_are_clamped);
  return UNITY_END();
}
