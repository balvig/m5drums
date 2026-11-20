// What presses of the three buttons mean (lib/controls/gestures.h), driven
// with made-up button states and time.
#include <unity.h>

#include <string>

#include "gestures.h"

namespace {

// Stands in for the screens: notes what the gestures ask for, as a string
// like "< > down play enter back" ("<" and ">" are steps left and right).
struct Log : GestureTarget {
  std::string events;
  bool grid = true;        // the cursor is on the grid
  bool value = false;      // a value is being changed
  bool stepWorks = true;   // false: already at the limit, nothing changes

  bool onGrid() override { return grid; }
  bool changingValue() override { return value; }
  bool step(int direction) override {
    add(direction < 0 ? "<" : ">");
    return stepWorks;
  }
  void row(int direction) override { add(direction < 0 ? "up" : "down"); }
  void playStop() override { add("play"); }
  void enter() override { add("enter"); }
  void back() override { add("back"); }

  void add(const char* what) {
    if (!events.empty()) events += " ";
    events += what;
  }
  int count(const char* what) const {
    int n = 0;
    for (size_t at = events.find(what); at != std::string::npos; at = events.find(what, at + 1)) ++n;
    return n;
  }
};

Log* target;
Gestures* gestures;
bool left, middle, right;
uint32_t now;

// Lets time pass with the buttons as they are, a main-loop pass every 2 ms.
void wait(uint32_t ms) {
  for (uint32_t passed = 0; passed < ms; passed += 2, now += 2) {  // `now` may wrap round
    gestures->update(left, middle, right, now, *target);
  }
  gestures->update(left, middle, right, now, *target);
}

void tap(bool& button, uint32_t ms = 60) {
  button = true;
  wait(ms);
  button = false;
  wait(20);
}

void assertEvents(const char* expected) { TEST_ASSERT_EQUAL_STRING(expected, target->events.c_str()); }

}  // namespace

void setUp() {
  target = new Log;
  gestures = new Gestures;
  left = middle = right = false;
  now = 5000;
  wait(10);
}
void tearDown() {
  delete target;
  delete gestures;
}

// ---- On the grid ----

void test_nothing_pressed_does_nothing() {
  wait(3000);
  assertEvents("");
}

void test_tap_left_and_right_step_the_cursor() {
  tap(left);
  tap(right);
  tap(right);
  assertEvents("< > >");
}

void test_on_the_grid_a_step_happens_on_release() {
  left = true;
  wait(30);
  assertEvents("");
  left = false;
  wait(2);
  assertEvents("<");
}

void test_on_the_grid_a_held_button_never_repeats() {
  right = true;
  wait(3000);
  assertEvents("");  // still down: nothing yet
  right = false;
  wait(20);
  assertEvents(">");
}

void test_hold_left_tap_right_goes_down_a_row() {
  left = true;
  wait(150);
  tap(right);
  assertEvents("down");
  left = false;
  wait(50);
  assertEvents("down");  // letting go of the held one adds nothing
}

void test_hold_right_tap_left_goes_up_a_row() {
  right = true;
  wait(150);
  tap(left);
  right = false;
  wait(50);
  assertEvents("up");
}

void test_several_rows_in_one_hold() {
  left = true;
  wait(100);
  tap(right);
  tap(right);
  tap(right);
  left = false;
  wait(50);
  assertEvents("down down down");
}

void test_row_move_when_the_held_button_lets_go_first() {
  left = true;
  wait(100);
  right = true;
  wait(60);
  left = false;  // the pair ends here: still a row move, and only one
  wait(60);
  right = false;
  wait(60);
  assertEvents("down");
}

void test_both_held_plays_without_moving_the_cursor() {
  left = true;
  wait(20);
  right = true;
  wait(Gestures::kPlayHoldMs - 10);
  assertEvents("");
  wait(20);
  assertEvents("play");
  wait(2000);  // kept down: still just the once
  left = right = false;
  wait(100);
  assertEvents("play");
}

void test_both_pressed_at_the_same_instant_plays() {
  left = right = true;
  wait(Gestures::kPlayHoldMs + 20);
  left = right = false;
  wait(100);
  assertEvents("play");
}

void test_play_then_stop() {
  for (int i = 0; i < 2; ++i) {
    right = true;
    wait(10);
    left = true;
    wait(600);
    left = right = false;
    wait(100);
  }
  assertEvents("play play");
}

void test_after_playing_a_tap_steps_again() {
  left = right = true;
  wait(600);
  left = right = false;
  wait(100);
  tap(left);
  assertEvents("play <");
}

// ---- The middle button ----

void test_middle_tap_is_enter_on_release() {
  middle = true;
  wait(100);
  assertEvents("");
  middle = false;
  wait(10);
  assertEvents("enter");
}

void test_middle_held_is_back_while_still_down() {
  middle = true;
  wait(Gestures::kBackHoldMs - 10);
  assertEvents("");
  wait(20);
  assertEvents("back");
  wait(2000);
  middle = false;
  wait(100);
  assertEvents("back");  // once, and no enter on letting go
}

void test_middle_works_while_an_outer_button_is_held() {
  left = true;
  wait(100);
  tap(middle);
  assertEvents("enter");
}

// ---- In the menu (not on the grid) ----

void test_in_the_menu_a_press_steps_after_the_grace_time() {
  target->grid = false;
  right = true;
  wait(Gestures::kPairGraceMs - 10);
  assertEvents("");
  wait(20);
  assertEvents(">");
  right = false;
  wait(50);
  assertEvents(">");  // nothing more on release
}

void test_in_the_menu_a_very_short_tap_still_steps() {
  target->grid = false;
  tap(left, 10);
  assertEvents("<");
}

void test_in_the_menu_a_held_button_repeats() {
  target->grid = false;
  right = true;
  wait(Gestures::kFirstRepeatMs - 10);
  TEST_ASSERT_EQUAL(1, target->count(">"));
  wait(20);
  TEST_ASSERT_EQUAL(2, target->count(">"));
  wait(5 * Gestures::kCursorRepeatMs);
  TEST_ASSERT_INT_WITHIN(1, 7, target->count(">"));
  right = false;
  int before = target->count(">");
  wait(1000);
  TEST_ASSERT_EQUAL(before, target->count(">"));
}

void test_changing_a_value_repeats_faster() {
  target->grid = false;
  target->value = true;
  left = true;
  wait(Gestures::kFirstRepeatMs + 20 * Gestures::kValueRepeatMs);
  TEST_ASSERT_INT_WITHIN(2, 22, target->count("<"));
}

void test_in_the_menu_a_pair_pressed_together_plays_and_moves_nothing() {
  target->grid = false;
  left = true;
  wait(20);  // inside the grace time
  right = true;
  wait(600);
  left = right = false;
  wait(100);
  assertEvents("play");
}

void test_in_the_menu_a_step_already_made_is_taken_back_for_a_pair() {
  target->grid = false;
  left = true;
  wait(100);  // past the grace time: it has stepped
  assertEvents("<");
  right = true;
  wait(600);
  left = right = false;
  wait(100);
  assertEvents("< > play");  // stepped back, then play
}

void test_a_step_that_changed_nothing_is_not_taken_back() {
  target->grid = false;
  target->stepWorks = false;  // the value is at its limit
  left = true;
  wait(100);
  right = true;
  wait(600);
  left = right = false;
  wait(100);
  assertEvents("< play");
}

void test_in_the_menu_a_quick_pair_does_not_move_a_row() {
  target->grid = false;
  left = true;
  wait(20);
  tap(right);
  left = false;
  wait(100);
  assertEvents("");
}

void test_the_clock_wrapping_round_changes_nothing() {
  now = 0xFFFFFFFFu - 300;  // millis() wraps after about 50 days
  target->grid = false;
  right = true;
  wait(Gestures::kFirstRepeatMs + 3 * Gestures::kCursorRepeatMs + 20);
  right = false;
  wait(20);
  TEST_ASSERT_INT_WITHIN(1, 5, target->count(">"));
  middle = true;
  wait(Gestures::kBackHoldMs + 20);
  middle = false;
  wait(20);
  TEST_ASSERT_EQUAL(1, target->count("back"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_pressed_does_nothing);
  RUN_TEST(test_tap_left_and_right_step_the_cursor);
  RUN_TEST(test_on_the_grid_a_step_happens_on_release);
  RUN_TEST(test_on_the_grid_a_held_button_never_repeats);
  RUN_TEST(test_hold_left_tap_right_goes_down_a_row);
  RUN_TEST(test_hold_right_tap_left_goes_up_a_row);
  RUN_TEST(test_several_rows_in_one_hold);
  RUN_TEST(test_row_move_when_the_held_button_lets_go_first);
  RUN_TEST(test_both_held_plays_without_moving_the_cursor);
  RUN_TEST(test_both_pressed_at_the_same_instant_plays);
  RUN_TEST(test_play_then_stop);
  RUN_TEST(test_after_playing_a_tap_steps_again);
  RUN_TEST(test_middle_tap_is_enter_on_release);
  RUN_TEST(test_middle_held_is_back_while_still_down);
  RUN_TEST(test_middle_works_while_an_outer_button_is_held);
  RUN_TEST(test_in_the_menu_a_press_steps_after_the_grace_time);
  RUN_TEST(test_in_the_menu_a_very_short_tap_still_steps);
  RUN_TEST(test_in_the_menu_a_held_button_repeats);
  RUN_TEST(test_changing_a_value_repeats_faster);
  RUN_TEST(test_in_the_menu_a_pair_pressed_together_plays_and_moves_nothing);
  RUN_TEST(test_in_the_menu_a_step_already_made_is_taken_back_for_a_pair);
  RUN_TEST(test_a_step_that_changed_nothing_is_not_taken_back);
  RUN_TEST(test_in_the_menu_a_quick_pair_does_not_move_a_row);
  RUN_TEST(test_the_clock_wrapping_round_changes_nothing);
  return UNITY_END();
}
