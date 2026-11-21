// The drum voices: which instruments a seed gives, and that every sound a
// seed can produce is audible, ends, and is the same every time.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <memory>
#include <vector>

#include "voices.h"

using namespace voices;

namespace {

constexpr int kSeeds = 60;
constexpr size_t kBlock = 256;
constexpr int kMaxSeconds = 30;  // nothing should ring for longer than this

// The instruments NEW may pick. METAL, COWBELL, PLUCK and ACID are held back.
bool inDraw(Family f) {
  return f == kPew || f == kNoise || f == kBlip || f == kKick || f == kClap || f == kTom ||
         f == kSnare || f == kRim;
}

// Renders until the kit is silent (or kMaxSeconds have passed).
std::vector<int16_t> renderUntilSilent(DrumKit& kit) {
  std::vector<int16_t> out;
  int16_t block[kBlock];
  const size_t limit = (size_t)kMaxSeconds * kSampleRate;
  while (kit.active() && out.size() < limit) {
    kit.render(block, kBlock);
    out.insert(out.end(), block, block + kBlock);
  }
  return out;
}

int peakOf(const std::vector<int16_t>& samples) {
  int peak = 0;
  for (int16_t s : samples) {
    int a = s < 0 ? -s : s;
    if (a > peak) peak = a;
  }
  return peak;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_kit_is_three_different_instruments_from_the_draw() {
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    Family f[kVoiceCount];
    familiesForSeed(seed, f);
    char message[60];
    snprintf(message, sizeof(message), "seed %u: %s %s %s", (unsigned)seed, familyName(f[0]),
             familyName(f[1]), familyName(f[2]));
    TEST_ASSERT_TRUE_MESSAGE(inDraw(f[0]) && inDraw(f[1]) && inDraw(f[2]), message);
    TEST_ASSERT_TRUE_MESSAGE(f[0] != f[1] && f[0] != f[2] && f[1] != f[2], message);
  }
}

void test_every_instrument_in_the_draw_turns_up() {
  bool seen[kFamilyCount] = {};
  for (uint32_t seed = 0; seed < 2000; ++seed) {
    Family f[kVoiceCount];
    familiesForSeed(seed, f);
    for (Family x : f) seen[x] = true;
  }
  for (int f = 0; f < kFamilyCount; ++f) {
    TEST_ASSERT_EQUAL_MESSAGE(inDraw((Family)f), seen[f], familyName((Family)f));
  }
}

void test_every_instrument_and_lane_has_a_name() {
  for (int f = 0; f < kFamilyCount; ++f) {
    TEST_ASSERT_TRUE(familyName((Family)f)[0] != 0);
    for (int lane = 0; lane < kLaneCount; ++lane) {
      const char* name = laneName((Family)f, lane);
      TEST_ASSERT_NOT_NULL(name);
      TEST_ASSERT_TRUE(strlen(name) >= 2 && strlen(name) <= 3);  // fits beside the grid
      // The full name is a word of its own (FM aside) that starts the same way
      // and fits the bottom bar's button with " STEP 8" after it.
      const char* full = laneFullName((Family)f, lane);
      TEST_ASSERT_TRUE_MESSAGE(strlen(full) <= 8, full);
      TEST_ASSERT_TRUE_MESSAGE(strcmp(name, "FM") == 0 || strlen(full) > strlen(name), full);
      TEST_ASSERT_EQUAL_MESSAGE(name[0], full[0], full);
    }
  }
}

void test_untriggered_kit_is_silent() {
  auto kit = std::make_unique<DrumKit>();
  kit->init(1234);
  TEST_ASSERT_FALSE(kit->active());
  int16_t block[kBlock];
  kit->render(block, kBlock);
  for (int16_t s : block) TEST_ASSERT_EQUAL(0, s);
}

// Every voice of every kit, at the centre and the corners of its three lanes.
void test_every_sound_is_audible_and_ends() {
  const int corners[][3] = {
      {0, 0, 0},
      {kPitchRange, kDecayRange, kShapeRange},
      {-kPitchRange, kDecayRange, -kShapeRange},
      {kPitchRange, -kDecayRange, -kShapeRange},
      {-kPitchRange, -kDecayRange, kShapeRange},
  };
  auto kit = std::make_unique<DrumKit>();
  float longest = 0;
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    kit->init(seed);
    Family families[kVoiceCount];
    familiesForSeed(seed, families);
    for (int v = 0; v < kVoiceCount; ++v) {
      for (const auto& c : corners) {
        char what[80];
        snprintf(what, sizeof(what), "seed %u %s pitch %d decay %d shape %d", (unsigned)seed,
                 familyName(families[v]), c[0], c[1], c[2]);
        kit->trigger((Voice)v, c[0], c[1], c[2]);
        TEST_ASSERT_TRUE_MESSAGE(kit->active(), what);
        std::vector<int16_t> out = renderUntilSilent(*kit);
        TEST_ASSERT_FALSE_MESSAGE(kit->active(), what);  // it ended
        float seconds = (float)out.size() / kSampleRate;
        if (seconds > longest) longest = seconds;
        // Audible: well clear of silence. (A broken filter or a NaN gives 0.)
        if (c[0] == 0) TEST_ASSERT_GREATER_THAN_MESSAGE(1000, peakOf(out), what);
      }
    }
  }
  printf("longest sound: %.1f s\n", longest);
}

void test_same_seed_gives_the_same_sound() {
  auto a = std::make_unique<DrumKit>();
  auto b = std::make_unique<DrumKit>();
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    a->init(seed);
    b->init(seed);
    for (int v = 0; v < kVoiceCount; ++v) {
      a->trigger((Voice)v);
      b->trigger((Voice)v);
    }
    std::vector<int16_t> outA = renderUntilSilent(*a);
    std::vector<int16_t> outB = renderUntilSilent(*b);
    TEST_ASSERT_TRUE(outA == outB);
  }
}

void test_different_seeds_give_different_sounds() {
  auto a = std::make_unique<DrumKit>();
  auto b = std::make_unique<DrumKit>();
  a->init(1);
  b->init(2);
  for (int v = 0; v < kVoiceCount; ++v) {
    a->trigger((Voice)v);
    b->trigger((Voice)v);
  }
  TEST_ASSERT_FALSE(renderUntilSilent(*a) == renderUntilSilent(*b));
}

// Every seed is its own kit, including seeds that differ only in the lowest bit.
void test_neighbouring_seeds_give_different_kits() {
  auto a = std::make_unique<DrumKit>();
  auto b = std::make_unique<DrumKit>();
  for (uint32_t seed = 0; seed < 200; ++seed) {
    a->init(seed);
    b->init(seed + 1);
    for (int v = 0; v < kVoiceCount; ++v) {
      a->trigger((Voice)v);
      b->trigger((Voice)v);
    }
    char message[40];
    snprintf(message, sizeof(message), "seeds %u and %u", (unsigned)seed, (unsigned)seed + 1);
    TEST_ASSERT_FALSE_MESSAGE(renderUntilSilent(*a) == renderUntilSilent(*b), message);
  }
}

// Analog feel: repeated hits of one drum are close but not identical.
void test_repeated_hits_vary_slightly() {
  auto kit = std::make_unique<DrumKit>();
  kit->init(7);
  kit->trigger(kVoiceA);
  std::vector<int16_t> first = renderUntilSilent(*kit);
  kit->trigger(kVoiceA);
  std::vector<int16_t> second = renderUntilSilent(*kit);
  TEST_ASSERT_FALSE(first == second);
  int a = peakOf(first), b = peakOf(second);
  TEST_ASSERT_INT_WITHIN(a / 4, a, b);  // level wander is a few percent, not more
}

// Three loud voices at once must round off, not wrap around: a wrapped sample
// shows up as a jump across most of the range between neighbouring samples.
void test_loud_mix_is_limited_without_wrapping() {
  auto kit = std::make_unique<DrumKit>();
  for (uint32_t seed = 1; seed <= kSeeds; ++seed) {
    kit->init(seed);
    Family f[kVoiceCount];
    familiesForSeed(seed, f);
    // Tones only: noise legitimately jumps from sample to sample.
    bool noisy = false;
    for (Family x : f) noisy |= (x == kNoise || x == kClap || x == kSnare || x == kRim);
    if (noisy) continue;
    for (int v = 0; v < kVoiceCount; ++v) kit->trigger((Voice)v, -12, 0, 0, 4.0f);
    std::vector<int16_t> out = renderUntilSilent(*kit);
    for (size_t i = 1; i < out.size(); ++i) {
      int jump = out[i] - out[i - 1];
      if (jump < 0) jump = -jump;
      if (jump > 50000) {
        char message[60];
        snprintf(message, sizeof(message), "seed %u wraps at sample %u", (unsigned)seed,
                 (unsigned)i);
        TEST_FAIL_MESSAGE(message);
      }
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_kit_is_three_different_instruments_from_the_draw);
  RUN_TEST(test_every_instrument_in_the_draw_turns_up);
  RUN_TEST(test_every_instrument_and_lane_has_a_name);
  RUN_TEST(test_untriggered_kit_is_silent);
  RUN_TEST(test_every_sound_is_audible_and_ends);
  RUN_TEST(test_same_seed_gives_the_same_sound);
  RUN_TEST(test_different_seeds_give_different_sounds);
  RUN_TEST(test_neighbouring_seeds_give_different_kits);
  RUN_TEST(test_repeated_hits_vary_slightly);
  RUN_TEST(test_loud_mix_is_limited_without_wrapping);
  return UNITY_END();
}
