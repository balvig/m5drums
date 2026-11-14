// Sequencer UI: settings row, step grid with views, cursor, playhead.
#include <M5Unified.h>

#include "audio.h"

namespace {

Sequencer seq;

// --- Rows: one settings ("meta") row, then one row per track ---
constexpr int kMetaRow = 0;
constexpr int kRows = 1 + Sequencer::kTracks;
constexpr int kLabelCol = -1;  // cursor position left of the first cell: the row name

enum MetaField { kFieldTempo = 0, kFieldSwing = 1, kMetaFields = 2 };

// What the grid shows and edits. Pressing A and C together cycles through them.
enum View { kViewTrig = 0, kViewPitch = 1, kViewDecay = 2, kViews = 3 };
const char* const kViewNames[kViews] = {"TRIG", "PITCH", "DECAY"};

// --- Layout (320x240) ---
constexpr int kLabelX = 2, kLabelW = 50;
constexpr int kMetaY = 2, kMetaH = 30;
constexpr int kPlayIconX = 290;
constexpr int kTagY = 35, kTagH = 14;
constexpr int kPlayheadY = 40, kPlayheadH = 6;
constexpr int kGridX = 56, kCellPitchX = 33, kCellW = 29;
constexpr int kGridY = 52, kCellPitchY = 50, kCellH = 44;
constexpr int kHintY = 226, kHintH = 14;

struct FieldBox {
  int x, w;
};
const FieldBox fieldBoxes[kMetaFields] = {{56, 96}, {156, 120}};

struct Track {
  const char* label;
  uint16_t color;
};
const Track tracks[Sequencer::kTracks] = {
    {"LOW", TFT_RED},
    {"MID", TFT_YELLOW},
    {"HI", TFT_CYAN},
};

const uint16_t kOffBeat = 0x18E3;  // dark grey
const uint16_t kOnBeat = 0x39E7;   // lighter grey: steps 1 and 5, the beats
const uint16_t kDimText = 0x7BEF;
const uint16_t kValueText = 0xC618;

// --- UI state: what is currently on screen ---
int cursorRow = 1, cursorCol = 0;
int view = kViewTrig;
bool editing = false;  // adjusting the value under the cursor
int shownStep = -1;
bool shownPlaying = false;
int shownHints = -1;

int colsInRow(int row) { return row == kMetaRow ? (int)kMetaFields : Sequencer::kSteps; }
int rowY(int row) { return row == kMetaRow ? kMetaY : kGridY + (row - 1) * kCellPitchY; }
int rowH(int row) { return row == kMetaRow ? kMetaH : kCellH; }
bool onCell() { return cursorRow != kMetaRow && cursorCol != kLabelCol; }

// Value of a step in the current (non-TRIG) view, and that view's +/- range.
int laneRange() { return view == kViewPitch ? voices::kPitchRange : voices::kDecayRange; }
int laneValue(int track, int step) {
  return view == kViewPitch ? seq.stepPitch(track, step) : seq.stepDecay(track, step);
}

// Everything below redraws one small region. No full-screen redraws after
// setup(): they cause audible glitches.

void drawCell(int track, int step) {
  int x = kGridX + step * kCellPitchX;
  int y = rowY(track + 1);
  bool on = seq.stepOn(track, step);
  bool cursor = cursorRow == track + 1 && cursorCol == step;
  uint16_t frame = cursor ? (editing ? TFT_ORANGE : TFT_WHITE) : TFT_BLACK;
  uint16_t shade = step % 4 == 0 ? kOnBeat : kOffBeat;
  M5.Display.fillRect(x, y, kCellW, kCellH, frame);

  if (view == kViewTrig) {
    M5.Display.fillRoundRect(x + 3, y + 3, kCellW - 6, kCellH - 6, 4,
                             on ? tracks[track].color : shade);
    return;
  }

  // Value views: a bar growing up or down from the centre line. Coloured for
  // steps that are on, grey for steps that are off.
  M5.Display.fillRoundRect(x + 3, y + 3, kCellW - 6, kCellH - 6, 4, shade);
  uint16_t barColor = on ? tracks[track].color : kDimText;
  int mid = y + kCellH / 2;
  int len = laneValue(track, step) * (kCellH / 2 - 5) / laneRange();
  int barX = x + 7, barW = kCellW - 14;
  if (len > 0) M5.Display.fillRect(barX, mid - len, barW, len, barColor);
  if (len < 0) M5.Display.fillRect(barX, mid, barW, -len, barColor);
  M5.Display.fillRect(barX - 2, mid - 1, barW + 4, 2, barColor);
}

void drawMetaField(int field) {
  const FieldBox& box = fieldBoxes[field];
  bool cursor = cursorRow == kMetaRow && cursorCol == field;
  bool active = cursor && editing;
  uint16_t bg = active ? TFT_ORANGE : TFT_BLACK;

  char text[16];
  if (field == kFieldTempo) {
    snprintf(text, sizeof(text), "BPM %d", seq.tempo());
  } else {
    snprintf(text, sizeof(text), "SWING %d%%", seq.swing());
  }

  M5.Display.fillRect(box.x, kMetaY, box.w, kMetaH, TFT_BLACK);
  M5.Display.fillRoundRect(box.x, kMetaY, box.w, kMetaH, 4, bg);
  if (cursor) M5.Display.drawRoundRect(box.x, kMetaY, box.w, kMetaH, 4, TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(active ? TFT_BLACK : (cursor ? TFT_WHITE : kValueText), bg);
  M5.Display.drawString(text, box.x + box.w / 2, kMetaY + kMetaH / 2);
}

void drawLabel(int row) {
  int y = rowY(row), h = rowH(row);
  bool selected = row == cursorRow;
  bool cursor = selected && cursorCol == kLabelCol;
  const char* text = row == kMetaRow ? "SET" : tracks[row - 1].label;
  uint16_t color = row == kMetaRow ? TFT_WHITE : tracks[row - 1].color;

  M5.Display.fillRect(kLabelX, y, kLabelW, h, TFT_BLACK);
  if (cursor) M5.Display.drawRoundRect(kLabelX, y + 2, kLabelW, h - 4, 4, TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(selected ? color : kDimText, TFT_BLACK);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, y + h / 2);
}

// Small tag under "SET": the current view, or the value being edited.
void drawViewTag() {
  char text[12];
  if (editing && onCell()) {
    snprintf(text, sizeof(text), "%c %+d", kViewNames[view][0],
             laneValue(cursorRow - 1, cursorCol));
  } else {
    snprintf(text, sizeof(text), "%s", kViewNames[view]);
  }
  M5.Display.fillRect(kLabelX, kTagY, kLabelW, kTagH, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(view == kViewTrig ? kDimText : TFT_ORANGE, TFT_BLACK);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, kTagY + kTagH / 2);
}

// Redraws whatever sits at one cursor position.
void drawPosition(int row, int col) {
  if (col == kLabelCol) {
    drawLabel(row);
  } else if (row == kMetaRow) {
    drawMetaField(col);
  } else {
    drawCell(row - 1, col);
  }
}

void drawPlayheadMark(int step, bool lit) {
  M5.Display.fillRect(kGridX + step * kCellPitchX + 3, kPlayheadY, kCellW - 6,
                      kPlayheadH, lit ? TFT_GREEN : kOffBeat);
}

// Play/stop icon at the right end of the settings row.
void drawPlayIcon(bool playing) {
  int y = kMetaY + 7;
  M5.Display.fillRect(kPlayIconX, y, 18, 17, TFT_BLACK);
  if (playing) {
    M5.Display.fillTriangle(kPlayIconX, y, kPlayIconX, y + 16, kPlayIconX + 16, y + 8, TFT_GREEN);
  } else {
    M5.Display.fillRect(kPlayIconX + 1, y + 1, 14, 14, kDimText);
  }
}

// Button hints, centred above the physical buttons. Redrawn only when what
// the buttons do changes.
void updateHints() {
  static const char* const kMiddle[] = {
      "DONE",               "NEXT ROW  hold:PLAY",  "EDIT  hold:PLAY",
      "ON/OFF  hold:PLAY",  "SET PITCH  hold:PLAY", "SET DECAY  hold:PLAY",
  };
  int hints = editing ? 0 : (cursorCol == kLabelCol ? 1 : (cursorRow == kMetaRow ? 2 : 3 + view));
  if (hints == shownHints) return;
  shownHints = hints;

  M5.Display.fillRect(0, kHintY, 320, kHintH, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(bottom_center);
  M5.Display.setTextColor(kDimText, TFT_BLACK);
  M5.Display.drawString(editing ? "-" : "< LEFT", 65, 238);
  M5.Display.drawString(kMiddle[hints], 160, 238);
  M5.Display.drawString(editing ? "+" : "RIGHT >", 255, 238);
}

void moveCursor(int row, int col) {
  int oldRow = cursorRow, oldCol = cursorCol;
  cursorRow = row;
  cursorCol = col;
  drawPosition(oldRow, oldCol);
  drawPosition(cursorRow, cursorCol);
  // Row names also show which row is selected.
  if (oldRow != cursorRow) {
    if (oldCol != kLabelCol) drawLabel(oldRow);
    if (cursorCol != kLabelCol) drawLabel(cursorRow);
  }
  updateHints();
}

// Moves sideways through: row name, then the row's cells, wrapping.
void moveCursorBy(int delta) {
  int positions = colsInRow(cursorRow) + 1;
  int pos = (cursorCol + 1 + delta + positions) % positions;
  moveCursor(cursorRow, pos - 1);
}

void setEditing(bool on) {
  editing = on;
  drawPosition(cursorRow, cursorCol);
  drawViewTag();
  updateHints();
}

// Changes the value being edited. Returns false if it was already at its limit.
bool adjustValue(int delta) {
  if (cursorRow == kMetaRow) {
    int before = cursorCol == kFieldTempo ? seq.tempo() : seq.swing();
    if (cursorCol == kFieldTempo) {
      seq.setTempo(before + delta);
    } else {
      seq.setSwing(before + delta);
    }
    int after = cursorCol == kFieldTempo ? seq.tempo() : seq.swing();
    if (after == before) return false;
    drawMetaField(cursorCol);
    return true;
  }

  int track = cursorRow - 1;
  int before = laneValue(track, cursorCol);
  if (view == kViewPitch) {
    seq.setStepPitch(track, cursorCol, before + delta);
  } else {
    seq.setStepDecay(track, cursorCol, before + delta);
  }
  if (laneValue(track, cursorCol) == before) return false;
  drawCell(track, cursorCol);
  drawViewTag();
  if (!seq.playing()) seq.auditionStep(track, cursorCol);  // hear the change
  return true;
}

void cycleView() {
  if (editing) setEditing(false);
  view = (view + 1) % kViews;
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    for (int s = 0; s < Sequencer::kSteps; ++s) drawCell(t, s);
  }
  drawViewTag();
  updateHints();
}

// What A (-1) or C (+1) does on its own. Returns false if nothing changed.
bool nudge(int delta) {
  if (editing) return adjustValue(delta);
  moveCursorBy(delta);
  return true;
}

// True on the initial press, then repeatedly while the button stays held.
bool pressedOrRepeating(m5::Button_Class& button, uint32_t& nextRepeatAt) {
  uint32_t now = millis();
  if (button.wasPressed()) {
    nextRepeatAt = now + 400;
    return true;
  }
  if (editing && button.isPressed() && (int32_t)(now - nextRepeatAt) >= 0) {
    nextRepeatAt = now + 50;
    return true;
  }
  return false;
}

void drawStaticScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  for (int row = 0; row < kRows; ++row) {
    drawLabel(row);
    for (int col = 0; col < colsInRow(row); ++col) drawPosition(row, col);
  }
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, false);
  drawPlayIcon(false);
  drawViewTag();
  updateHints();
}

void loadStarterPattern() {
  for (int s : {0, 3}) seq.toggleStep(voices::kTomLow, s);
  seq.toggleStep(voices::kTomMid, 4);
  for (int s : {2, 6}) seq.toggleStep(voices::kTomHigh, s);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 0 (the default) leaves Serial off
  M5.begin(cfg);

  seq.init(esp_random());  // a different kit on every boot
  loadStarterPattern();
  drawStaticScreen();

  M5.Speaker.setVolume(160);
  audio::begin(seq);
  Serial.println("m5drums: ready");
}

void loop() {
  static uint32_t repeatA = 0, repeatC = 0;
  static bool chordHeld = false;
  static bool lastNudgeChanged = false;
  M5.update();

  // A + C together: next view. The first button of the pair has already acted
  // on its own by the time the second lands, so that action is taken back.
  bool aDown = M5.BtnA.wasPressed(), cDown = M5.BtnC.wasPressed();
  if (!chordHeld && ((aDown && M5.BtnC.isPressed()) || (cDown && M5.BtnA.isPressed()))) {
    chordHeld = true;
    if (!(aDown && cDown) && lastNudgeChanged) nudge(aDown ? -1 : +1);
    cycleView();
  }

  if (chordHeld) {
    if (!M5.BtnA.isPressed() && !M5.BtnC.isPressed()) chordHeld = false;
  } else {
    // A / C: cursor left / right, or decrease / increase while editing
    // (repeating while held).
    if (pressedOrRepeating(M5.BtnA, repeatA)) lastNudgeChanged = nudge(-1);
    if (pressedOrRepeating(M5.BtnC, repeatC)) lastNudgeChanged = nudge(+1);
  }

  // B short press acts on release, so that a hold does not also trigger it.
  if (M5.BtnB.wasClicked()) {
    if (editing) {
      setEditing(false);
    } else if (cursorCol == kLabelCol) {
      moveCursor((cursorRow + 1) % kRows, kLabelCol);  // next row
    } else if (cursorRow == kMetaRow || view != kViewTrig) {
      setEditing(true);
    } else {
      int track = cursorRow - 1;
      seq.toggleStep(track, cursorCol);
      drawCell(track, cursorCol);
      // Preview the sound when adding a step while stopped.
      if (!seq.playing() && seq.stepOn(track, cursorCol)) seq.auditionStep(track, cursorCol);
    }
  }

  // B hold: play/stop, from anywhere
  if (M5.BtnB.wasHold()) seq.setPlaying(!seq.playing());

  bool playing = seq.playing();
  if (playing != shownPlaying) {
    shownPlaying = playing;
    drawPlayIcon(playing);
  }

  int step = seq.currentStep();
  if (step != shownStep) {
    if (shownStep >= 0) drawPlayheadMark(shownStep, false);
    if (step >= 0) drawPlayheadMark(step, true);
    shownStep = step;
  }

  delay(2);
}
