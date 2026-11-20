// What presses of the three buttons mean. Plain C++ with no hardware in it:
// it is given which buttons are down and the time, and calls back with what
// to do, so it is tested on a computer (test/test_gestures).
//
//   left / right  tapped: one step that way (while the other is held: a row)
//                 held left, then right tapped: down a row
//                 held right, then left tapped: up a row
//                 both held for a moment: play / stop
//   middle        tapped: enter
//                 held on its own for a moment: back
//
// On the grid, left and right step when released and never while held,
// because a held button is the shift for the other one. In the menu and
// while a value is being changed they step on press (after a few hundredths
// of a second, so that a pair pressed together is seen first) and repeat
// while held.
#pragma once
#include <stdint.h>

// What the gestures act on: the screens.
class GestureTarget {
 public:
  // The cursor is on the grid: no menu, help page or value being changed.
  virtual bool onGrid() = 0;
  virtual bool changingValue() = 0;
  // One step left (-1) or right (+1): the cursor, the menu's marker, or the
  // value being changed. False if nothing changed.
  virtual bool step(int direction) = 0;
  // Up (-1) or down (+1) a row.
  virtual void row(int direction) = 0;
  virtual void playStop() = 0;
  virtual void enter() = 0;
  virtual void back() = 0;

 protected:
  ~GestureTarget() = default;
};

class Gestures {
 public:
  static constexpr uint32_t kBackHoldMs = 400;      // middle held alone this long: back
  static constexpr uint32_t kPlayHoldMs = 450;      // left and right held together this long: play / stop
  static constexpr uint32_t kPairGraceMs = 45;      // how long a press waits for the other button to join it
  static constexpr uint32_t kFirstRepeatMs = 400;   // a held button starts repeating after this
  static constexpr uint32_t kValueRepeatMs = 50;    // then this often when it changes a value
  static constexpr uint32_t kCursorRepeatMs = 120;  // and this often when it moves the menu's marker

  // Call on every pass of the main loop with which buttons are down now.
  void update(bool leftDown, bool middleDown, bool rightDown, uint32_t now, GestureTarget& target) {
    bool onGrid = target.onGrid();

    if (middleDown && !middleWas_) {
      backDone_ = false;
      middlePressedAt_ = now;
    }

    left_.down = leftDown;
    right_.down = rightDown;
    for (Side* b : {&left_, &right_}) {
      Side* other = b == &left_ ? &right_ : &left_;

      if (b->down && !b->wasDown) {
        b->moves = 0;
        b->pressedAt = now;
        b->repeats = !onGrid;
        if (other->down) {
          // Landing on the other outer button: the pair is a row move if let
          // go quickly, play / stop if kept down. Nothing shows until it is
          // known which.
          becomeHolder(*other, target);
          b->mode = kRowPress;
        } else {
          b->mode = kPending;
        }
        continue;
      }

      switch (b->mode) {
        case kPending:
          if (!b->repeats) {
            // On the grid a held button is a shift for the other one, so it
            // never moves the cursor while down: the step happens on release.
            if (!b->down) {
              target.step(b->direction);
              b->mode = kIdle;
            }
          } else if (!b->down || now - b->pressedAt >= kPairGraceMs) {
            // In the menu and while changing a value: act once the other
            // button has had its chance to join, then repeat while held.
            if (target.step(b->direction)) ++b->moves;
            b->mode = b->down ? kMoving : kIdle;
            b->repeatAt = b->pressedAt + kFirstRepeatMs;
          }
          break;

        case kMoving:
          if (!b->down) {
            b->mode = kIdle;
          } else if ((int32_t)(now - b->repeatAt) >= 0) {
            b->repeatAt = now + (target.changingValue() ? kValueRepeatMs : kCursorRepeatMs);
            if (target.step(b->direction)) ++b->moves;
          }
          break;

        case kRowPress:
          if (b->down && other->down) {
            if (now - b->pressedAt >= kPlayHoldMs) {
              // Both kept down: play / stop.
              b->mode = kHolding;
              target.playStop();
            }
          } else {
            // One of the pair let go first: a row move. Holding left and
            // pressing right goes down; the reverse goes up.
            if (onGrid) target.row(b->direction);
            b->mode = b->down ? kHolding : kIdle;
          }
          break;

        default:
          if (!b->down) b->mode = kIdle;
          break;
      }
    }
    left_.wasDown = leftDown;
    right_.wasDown = rightDown;

    // Middle: enter on a tap; back once held on its own a moment.
    if (middleDown && !backDone_ && now - middlePressedAt_ >= kBackHoldMs) {
      backDone_ = true;
      target.back();
    }
    if (!middleDown && middleWas_ && !backDone_) target.enter();
    middleWas_ = middleDown;
  }

 private:
  enum Mode {
    kIdle,
    kPending,   // just pressed: waiting a moment to see if the other button joins it
    kMoving,    // stepping sideways, repeating
    kHolding,   // the held half of a pair; does nothing more on its own
    kRowPress,  // the second half of a pair: a row move if let go, play / stop if held
  };

  // One of the two outer buttons, and what its current press has become.
  struct Side {
    explicit Side(int direction) : direction(direction) {}
    int direction;  // -1 left, +1 right
    Mode mode = kIdle;
    int moves = 0;         // steps this press has made, to take back if needed
    bool repeats = false;  // this press began in the menu or while changing a value
    uint32_t pressedAt = 0;
    uint32_t repeatAt = 0;
    bool down = false, wasDown = false;
  };

  // Takes back this press's steps; the press does nothing more on its own.
  static void becomeHolder(Side& side, GestureTarget& target) {
    if (side.mode == kMoving) {
      for (; side.moves > 0; --side.moves) target.step(-side.direction);
    }
    side.mode = kHolding;
  }

  Side left_{-1}, right_{+1};
  bool middleWas_ = false;
  bool backDone_ = false;  // this hold of middle has already done "back"
  uint32_t middlePressedAt_ = 0;
};
