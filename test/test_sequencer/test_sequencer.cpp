// The sequencer: step timing counted in samples, swing, the pattern and its
// automation, and the evolve phrase.
#include <unity.h>

#include <memory>
#include <vector>

#include "helpers.h"
#include "sequencer.h"

namespace {

constexpr uint32_t kSeed = 4242;
constexpr int kRate = voices::kSampleRate;

std::unique_ptr<Sequencer> makeSequencer(uint32_t seed = kSeed) {
  auto seq = std::make_unique<Sequencer>();
  seq->init(seed);
  return seq;
}

// A busy pattern with some automation, so every evolve feature has material.
void writePattern(Sequencer& seq) {
  seq.setPattern(0, 0b10010001);
  seq.setPattern(1, 0b01000100);
  seq.setPattern(2, 0b10101010);
  seq.setStepPitch(0, 4, 5);
  seq.setStepDecay(1, 2, -3);
  seq.setStepShape(2, 1, 4);
}

// Samples in a pair of 16ths, and in the first of the pair, as the device
// counts them.
uint32_t pairSamples(int bpm) { return (uint32_t)kRate * 30u / bpm; }
uint32_t firstSamples(int bpm, int swing) { return pairSamples(bpm) * swing / 100u; }

// Sample at which each of the first `count` steps starts.
std::vector<uint32_t> expectedStarts(int bpm, int swing, int count) {
  std::vector<uint32_t> starts;
  uint32_t at = 0;
  for (int i = 0; i < count; ++i) {
    starts.push_back(at);
    uint32_t first = firstSamples(bpm, swing);
    at += (i % 2 == 0) ? first : pairSamples(bpm) - first;
  }
  return starts;
}

// Plays one sample at a time and notes the sample at which each step starts.
std::vector<uint32_t> observedStarts(Sequencer& seq, int count) {
  std::vector<uint32_t> starts;
  int last = -1;
  int16_t sample;
  for (uint32_t i = 0; (int)starts.size() < count && i < 120u * kRate; ++i) {
    seq.render(&sample, 1);
    int step = seq.currentStep();
    if (step != last) {
      TEST_ASSERT_EQUAL((int)(starts.size() % Sequencer::kSteps), step);  // in order
      starts.push_back(i);
      last = step;
    }
  }
  return starts;
}

void assertStepTiming(int bpm, int swing) {
  auto seq = makeSequencer();
  seq->setTempo(bpm);
  seq->setSwing(swing);
  seq->setPlaying(true);
  const int count = 3 * Sequencer::kSteps + 1;
  std::vector<uint32_t> expected = expectedStarts(bpm, swing, count);
  std::vector<uint32_t> observed = observedStarts(*seq, count);
  TEST_ASSERT_EQUAL(count, (int)observed.size());
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected.data(), observed.data(), count);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_stopped_sequencer_is_silent() {
  auto seq = makeSequencer();
  writePattern(*seq);
  TEST_ASSERT_FALSE(seq->hasAudio());
  TEST_ASSERT_EQUAL(-1, seq->currentStep());
  TEST_ASSERT_EQUAL(0, peakOf(renderSamples(*seq, kRate)));
}

void test_steps_fall_on_exact_samples() {
  // 120 BPM straight: a 16th is 5512.5 samples, so they alternate 5512, 5513.
  auto seq = makeSequencer();
  seq->setPlaying(true);
  std::vector<uint32_t> starts = observedStarts(*seq, 5);
  const uint32_t expected[] = {0, 5512, 11025, 16537, 22050};
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, starts.data(), 5);
}

void test_step_timing_across_tempos_and_swing() {
  for (int bpm : {40, 97, 120, 170, 240}) {
    for (int swing : {50, 58, 66, 75}) assertStepTiming(bpm, swing);
  }
}

void test_timing_does_not_drift() {
  // Whatever the swing, a pair of steps lasts exactly one 8th note, so after
  // many passes the pattern is still exactly where the tempo says.
  auto seq = makeSequencer();
  seq->setTempo(133);
  seq->setSwing(61);
  seq->setPlaying(true);
  const int passes = 40;
  std::vector<uint32_t> starts = observedStarts(*seq, passes * Sequencer::kSteps + 1);
  TEST_ASSERT_EQUAL_UINT32(passes * 4u * pairSamples(133), starts.back());
}

void test_swing_delays_the_offbeat() {
  auto seq = makeSequencer();
  seq->setSwing(66);
  seq->setPlaying(true);
  std::vector<uint32_t> starts = observedStarts(*seq, 3);
  TEST_ASSERT_EQUAL_UINT32(7276, starts[1]);   // 66 % of 11025
  TEST_ASSERT_EQUAL_UINT32(11025, starts[2]);  // the on-beat does not move
}

void test_hit_sounds_on_its_steps_first_sample() {
  auto seq = makeSequencer();
  seq->setPattern(0, 1u << 4);  // one hit, on step 5
  seq->setPlaying(true);
  std::vector<int16_t> out = renderSamples(*seq, kRate);
  const uint32_t start = expectedStarts(120, 50, 5)[4];
  for (uint32_t i = 0; i < start; ++i) TEST_ASSERT_EQUAL(0, out[i]);
  int peak = 0;
  for (uint32_t i = start; i < start + 441; ++i) {  // within 10 ms
    int a = out[i] < 0 ? -out[i] : out[i];
    if (a > peak) peak = a;
  }
  TEST_ASSERT_GREATER_THAN(0, peak);
}

void test_empty_pattern_plays_silence() {
  auto seq = makeSequencer();
  seq->setPlaying(true);
  TEST_ASSERT_EQUAL(0, peakOf(renderSamples(*seq, 2 * kRate)));
  TEST_ASSERT_TRUE(seq->hasAudio());  // still running
}

void test_stop_and_restart_begins_from_step_one() {
  auto seq = makeSequencer();
  seq->setPlaying(true);
  renderSamples(*seq, 20000);
  TEST_ASSERT_EQUAL(3, seq->currentStep());
  seq->setPlaying(false);
  renderSamples(*seq, kBlock);
  TEST_ASSERT_EQUAL(-1, seq->currentStep());
  seq->setPlaying(true);
  renderSamples(*seq, kBlock);
  TEST_ASSERT_EQUAL(0, seq->currentStep());
}

void test_ring_out_ends_after_stopping() {
  auto seq = makeSequencer();
  writePattern(*seq);
  seq->setPlaying(true);
  renderSamples(*seq, kRate);
  seq->setPlaying(false);
  renderSamples(*seq, 30 * kRate);
  TEST_ASSERT_FALSE(seq->hasAudio());
}

void test_pattern_editing() {
  auto seq = makeSequencer();
  seq->toggleStep(1, 3);
  TEST_ASSERT_TRUE(seq->stepOn(1, 3));
  TEST_ASSERT_EQUAL_HEX8(0b00001000, seq->pattern(1));
  TEST_ASSERT_EQUAL_HEX8(0, seq->pattern(0));
  seq->toggleStep(1, 3);
  TEST_ASSERT_FALSE(seq->stepOn(1, 3));

  writePattern(*seq);
  seq->clearPattern();
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    TEST_ASSERT_EQUAL_HEX8(0, seq->pattern(t));
    for (int s = 0; s < Sequencer::kSteps; ++s) {
      TEST_ASSERT_EQUAL(0, seq->stepPitch(t, s));
      TEST_ASSERT_EQUAL(0, seq->stepDecay(t, s));
      TEST_ASSERT_EQUAL(0, seq->stepShape(t, s));
    }
  }
}

void test_values_are_kept_in_range() {
  auto seq = makeSequencer();
  seq->setTempo(10);
  TEST_ASSERT_EQUAL(Sequencer::kMinBpm, seq->tempo());
  seq->setTempo(999);
  TEST_ASSERT_EQUAL(Sequencer::kMaxBpm, seq->tempo());
  seq->setSwing(0);
  TEST_ASSERT_EQUAL(Sequencer::kMinSwing, seq->swing());
  seq->setSwing(100);
  TEST_ASSERT_EQUAL(Sequencer::kMaxSwing, seq->swing());
  seq->setEvolveTheme(99);
  TEST_ASSERT_EQUAL(Sequencer::kEvolveThemes, seq->evolveTheme());
  seq->setStepPitch(0, 0, 100);
  TEST_ASSERT_EQUAL(voices::kPitchRange, seq->stepPitch(0, 0));
  seq->setStepDecay(0, 0, -100);
  TEST_ASSERT_EQUAL(-voices::kDecayRange, seq->stepDecay(0, 0));
  seq->setStepShape(0, 0, 100);
  TEST_ASSERT_EQUAL(voices::kShapeRange, seq->stepShape(0, 0));
}

void test_automation_changes_the_sound() {
  auto plain = makeSequencer();
  auto pitched = makeSequencer();
  for (Sequencer* s : {plain.get(), pitched.get()}) {
    s->setPattern(0, 1);
    s->setPlaying(true);
  }
  pitched->setStepPitch(0, 0, 7);
  TEST_ASSERT_TRUE(firstDifference(renderSamples(*plain, kRate), renderSamples(*pitched, kRate)) >=
                   0);
}

void test_new_kit_changes_the_sound() {
  auto a = makeSequencer();
  auto b = makeSequencer();
  for (Sequencer* s : {a.get(), b.get()}) {
    writePattern(*s);
    s->setPlaying(true);
  }
  b->newKit(kSeed + 1);
  TEST_ASSERT_EQUAL_UINT32(kSeed + 1, b->kitSeed());
  TEST_ASSERT_TRUE(firstDifference(renderSamples(*a, kRate), renderSamples(*b, kRate)) >= 0);
}

// --- Evolve ---

constexpr size_t kPassSamples = 44100;  // 8 steps at 120 BPM
constexpr size_t kPhraseSamples = kPassSamples * Sequencer::kEvolvePasses;

void test_theme_names() {
  TEST_ASSERT_EQUAL_STRING("OFF", Sequencer::evolveThemeName(0));
  TEST_ASSERT_EQUAL_STRING("DRIFT", Sequencer::evolveThemeName(1));
  TEST_ASSERT_EQUAL_STRING("MELT", Sequencer::evolveThemeName(Sequencer::kEvolveThemes));
  TEST_ASSERT_EQUAL_STRING("OFF", Sequencer::evolveThemeName(Sequencer::kEvolveThemes + 1));
}

void test_evolve_first_pass_is_exactly_as_set() {
  auto plain = makeSequencer();
  writePattern(*plain);
  plain->setPlaying(true);
  std::vector<int16_t> asSet = renderSamples(*plain, kPassSamples);

  for (int theme = 1; theme <= Sequencer::kEvolveThemes; ++theme) {
    auto seq = makeSequencer();
    writePattern(*seq);
    seq->setEvolveTheme(theme);
    seq->setPlaying(true);
    assertSameAudio(asSet, renderSamples(*seq, kPassSamples), Sequencer::evolveThemeName(theme));
  }
}

void test_every_theme_changes_the_phrase() {
  auto plain = makeSequencer();
  writePattern(*plain);
  plain->setPlaying(true);
  std::vector<int16_t> asSet = renderSamples(*plain, kPhraseSamples);

  for (int theme = 1; theme <= Sequencer::kEvolveThemes; ++theme) {
    auto seq = makeSequencer();
    writePattern(*seq);
    seq->setEvolveTheme(theme);
    seq->setPlaying(true);
    long at = firstDifference(asSet, renderSamples(*seq, kPhraseSamples));
    TEST_ASSERT_TRUE_MESSAGE(at >= (long)kPassSamples, Sequencer::evolveThemeName(theme));
  }
}

void test_evolve_phrase_is_the_same_every_time() {
  for (int theme = 1; theme <= Sequencer::kEvolveThemes; ++theme) {
    auto a = makeSequencer();
    auto b = makeSequencer();
    for (Sequencer* s : {a.get(), b.get()}) {
      writePattern(*s);
      s->setEvolveTheme(theme);
      s->setPlaying(true);
    }
    assertSameAudio(renderSamples(*a, kPhraseSamples), renderSamples(*b, kPhraseSamples),
                    Sequencer::evolveThemeName(theme));
  }
}

void test_evolve_never_changes_what_is_stored() {
  auto seq = makeSequencer();
  writePattern(*seq);
  seq->setEvolveTheme(7);  // GLITCH
  seq->setPlaying(true);
  renderSamples(*seq, kPhraseSamples);
  TEST_ASSERT_EQUAL_HEX8(0b10010001, seq->pattern(0));
  TEST_ASSERT_EQUAL_HEX8(0b01000100, seq->pattern(1));
  TEST_ASSERT_EQUAL_HEX8(0b10101010, seq->pattern(2));
  TEST_ASSERT_EQUAL(5, seq->stepPitch(0, 4));
  TEST_ASSERT_EQUAL(-3, seq->stepDecay(1, 2));
  TEST_ASSERT_EQUAL(4, seq->stepShape(2, 1));
  TEST_ASSERT_EQUAL(0, seq->stepPitch(0, 0));
}

void test_evolve_pass_counts_through_the_phrase_and_wraps() {
  auto seq = makeSequencer();
  TEST_ASSERT_EQUAL(0, seq->evolvePasses());
  seq->setEvolveTheme(1);
  TEST_ASSERT_EQUAL(Sequencer::kEvolvePasses, seq->evolvePasses());
  TEST_ASSERT_EQUAL(-1, seq->evolvePass());  // stopped
  seq->setPlaying(true);
  for (int pass = 0; pass < Sequencer::kEvolvePasses + 2; ++pass) {
    renderSamples(*seq, kBlock);
    TEST_ASSERT_EQUAL(pass % Sequencer::kEvolvePasses, seq->evolvePass());
    renderSamples(*seq, kPassSamples - kBlock);
  }
  seq->setPlaying(false);
  renderSamples(*seq, kBlock);
  TEST_ASSERT_EQUAL(-1, seq->evolvePass());
}

void test_evolve_off_reports_no_pass() {
  auto seq = makeSequencer();
  seq->setPlaying(true);
  renderSamples(*seq, kBlock);
  TEST_ASSERT_EQUAL(-1, seq->evolvePass());
}

// The export renders through a copy. It must sound like a freshly started
// device, however long the live sequencer has been playing.
void test_copy_plays_like_a_fresh_start() {
  auto live = makeSequencer();
  writePattern(*live);
  live->setTempo(141);
  live->setSwing(62);
  live->setEvolveTheme(6);
  live->setPlaying(true);
  renderSamples(*live, 3 * kRate + 77);

  auto copy = std::make_unique<Sequencer>();
  copy->copyFrom(*live);
  TEST_ASSERT_FALSE(copy->playing());
  TEST_ASSERT_EQUAL(141, copy->tempo());
  TEST_ASSERT_EQUAL(62, copy->swing());
  TEST_ASSERT_EQUAL(6, copy->evolveTheme());

  auto fresh = makeSequencer();
  writePattern(*fresh);
  fresh->setTempo(141);
  fresh->setSwing(62);
  fresh->setEvolveTheme(6);

  copy->setPlaying(true);
  fresh->setPlaying(true);
  assertSameAudio(renderSamples(*fresh, 20 * kRate), renderSamples(*copy, 20 * kRate), "copy");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stopped_sequencer_is_silent);
  RUN_TEST(test_steps_fall_on_exact_samples);
  RUN_TEST(test_step_timing_across_tempos_and_swing);
  RUN_TEST(test_timing_does_not_drift);
  RUN_TEST(test_swing_delays_the_offbeat);
  RUN_TEST(test_hit_sounds_on_its_steps_first_sample);
  RUN_TEST(test_empty_pattern_plays_silence);
  RUN_TEST(test_stop_and_restart_begins_from_step_one);
  RUN_TEST(test_ring_out_ends_after_stopping);
  RUN_TEST(test_pattern_editing);
  RUN_TEST(test_values_are_kept_in_range);
  RUN_TEST(test_automation_changes_the_sound);
  RUN_TEST(test_new_kit_changes_the_sound);
  RUN_TEST(test_theme_names);
  RUN_TEST(test_evolve_first_pass_is_exactly_as_set);
  RUN_TEST(test_every_theme_changes_the_phrase);
  RUN_TEST(test_evolve_phrase_is_the_same_every_time);
  RUN_TEST(test_evolve_never_changes_what_is_stored);
  RUN_TEST(test_evolve_pass_counts_through_the_phrase_and_wraps);
  RUN_TEST(test_evolve_off_reports_no_pass);
  RUN_TEST(test_copy_plays_like_a_fresh_start);
  return UNITY_END();
}
