// Session storage: what is saved comes back, and anything unusable is ignored.
// Runs against an in-memory stand-in for the ESP32's flash (support/Preferences.h).
#include <unity.h>

#include <memory>

#include "Preferences.h"
#include "helpers.h"
#include "storage.h"

namespace {

const char* const kSlot = "m5drums/session";

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

// A sequencer in a state no save would produce, to show a failed load leaves it alone.
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

void setUp() { fake_nvs::store.clear(); }
void tearDown() {}

void test_nothing_saved_loads_nothing() {
  auto seq = makeMarked();
  TEST_ASSERT_FALSE(storage::load(*seq));
  assertUntouched(*seq);
}

void test_session_survives_save_and_load() {
  auto saved = makeSession();
  storage::save(*saved);

  auto loaded = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(storage::load(*loaded));
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
  auto saved = makeSession();
  storage::save(*saved);
  auto loaded = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(storage::load(*loaded));

  auto fresh = makeSession();  // `saved`, before it has played anything
  fresh->setPlaying(true);
  loaded->setPlaying(true);
  const size_t seconds = 10;
  assertSameAudio(renderSamples(*fresh, seconds * voices::kSampleRate),
                  renderSamples(*loaded, seconds * voices::kSampleRate), "restored session");
}

void test_later_save_replaces_earlier() {
  auto seq = makeSession();
  storage::save(*seq);
  seq->setTempo(90);
  seq->toggleStep(0, 1);
  storage::save(*seq);

  auto loaded = std::make_unique<Sequencer>();
  TEST_ASSERT_TRUE(storage::load(*loaded));
  TEST_ASSERT_EQUAL(90, loaded->tempo());
  TEST_ASSERT_TRUE(loaded->stepOn(0, 1));
  TEST_ASSERT_EQUAL(1, (int)fake_nvs::store.size());
}

// A save from firmware with a different session version is dropped, not converted.
void test_other_version_is_ignored() {
  storage::save(*makeSession());
  TEST_ASSERT_EQUAL(1, (int)fake_nvs::store.count(kSlot));
  fake_nvs::store[kSlot][0] ^= 0xFF;  // the version is the first byte

  auto seq = makeMarked();
  TEST_ASSERT_FALSE(storage::load(*seq));
  assertUntouched(*seq);
}

void test_wrong_size_is_ignored() {
  storage::save(*makeSession());
  fake_nvs::store[kSlot].pop_back();
  auto shorter = makeMarked();
  TEST_ASSERT_FALSE(storage::load(*shorter));
  assertUntouched(*shorter);

  fake_nvs::store[kSlot].resize(fake_nvs::store[kSlot].size() + 9);
  auto longer = makeMarked();
  TEST_ASSERT_FALSE(storage::load(*longer));
  assertUntouched(*longer);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_saved_loads_nothing);
  RUN_TEST(test_session_survives_save_and_load);
  RUN_TEST(test_restored_session_sounds_the_same);
  RUN_TEST(test_later_save_replaces_earlier);
  RUN_TEST(test_other_version_is_ignored);
  RUN_TEST(test_wrong_size_is_ignored);
  return UNITY_END();
}
