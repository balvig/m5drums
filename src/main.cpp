// Sequencer UI: settings row, view tabs, step grid, cursor, playhead.
#include <M5Unified.h>

#include "audio.h"
#include "storage.h"

namespace {

Sequencer seq;

// --- Rows: one settings ("meta") row, then one row per track ---
constexpr int kMetaRow = 0;
constexpr int kRows = 1 + Sequencer::kTracks;

// NEW sits in the row-name column; it starts an empty pattern with new sounds.
enum MetaField { kFieldNew = 0, kFieldTempo = 1, kFieldSwing = 2, kMetaFields = 3 };

// What the grid shows and edits. Pressing A and C together cycles through them.
enum View { kViewTrig = 0, kViewPitch = 1, kViewDecay = 2, kViews = 3 };
const char* const kViewNames[kViews] = {"TRIG", "PITCH", "DECAY"};

// --- Layout (320x240) ---
constexpr int kLabelX = 2, kLabelW = 50;
constexpr int kMetaY = 2, kMetaH = 30;
constexpr int kPlayIconX = 290;
constexpr int kTabY = 35, kTabH = 19, kTabPitchX = 88, kTabW = 84;
constexpr int kGridX = 56, kCellPitchX = 33, kCellW = 29;
constexpr int kGridY = 58, kCellPitchY = 47, kCellH = 42;
constexpr int kPlayheadY = 200, kPlayheadH = 6;
constexpr int kHintY = 226, kHintH = 14;

struct FieldBox {
  int x, w;
};
const FieldBox fieldBoxes[kMetaFields] = {{kLabelX, kLabelW}, {56, 96}, {156, 120}};

struct Track {
  const char* label;
  uint16_t color;
};
const Track tracks[Sequencer::kTracks] = {
    {"A", TFT_RED},
    {"B", TFT_YELLOW},
    {"C", TFT_CYAN},
};

const uint16_t kOffBeat = 0x18E3;  // dark grey
const uint16_t kOnBeat = 0x39E7;   // lighter grey: steps 1 and 5, the beats
const uint16_t kDimText = 0x7BEF;
const uint16_t kValueText = 0xC618;

constexpr uint32_t kRowHoldMs = 350;  // holding A / C this long changes row
constexpr uint32_t kSaveIdleMs = 2000;  // save this long after the last change

// --- UI state: what is currently on screen ---
int cursorRow = 1, cursorCol = 0;
int savedMetaCol = kFieldTempo, savedGridCol = 0;  // column to return to in each kind of row
int view = kViewTrig;
bool editing = false;  // adjusting the value under the cursor
bool confirmNew = false;  // NEW was pressed once; a second press erases
bool dirty = false;       // changes not yet saved to flash
uint32_t dirtyAt = 0;
int shownStep = -1;
bool shownPlaying = false;
int shownHints = -1;

int colsInRow(int row) { return row == kMetaRow ? (int)kMetaFields : Sequencer::kSteps; }
int rowY(int row) { return row == kMetaRow ? kMetaY : kGridY + (row - 1) * kCellPitchY; }
int rowH(int row) { return row == kMetaRow ? kMetaH : kCellH; }

// Value of a step in the current (non-TRIG) view, and that view's +/- range.
int laneRange() { return view == kViewPitch ? voices::kPitchRange : voices::kDecayRange; }
int laneValue(int track, int step) {
  return view == kViewPitch ? seq.stepPitch(track, step) : seq.stepDecay(track, step);
}

void markDirty() {
  dirty = true;
  dirtyAt = millis();
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
  bool active = cursor && (editing || (field == kFieldNew && confirmNew));
  uint16_t bg = active ? TFT_ORANGE : TFT_BLACK;

  char text[16];
  if (field == kFieldNew) {
    snprintf(text, sizeof(text), active ? "OK?" : "NEW");
  } else if (field == kFieldTempo) {
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

// Row name. The row the cursor is on is shown as a filled block.
void drawLabel(int row) {
  if (row == kMetaRow) return;  // that corner is the NEW button, not a name
  int y = rowY(row), h = rowH(row);
  bool selected = row == cursorRow;
  const char* text = tracks[row - 1].label;
  uint16_t bg = selected ? tracks[row - 1].color : TFT_BLACK;

  M5.Display.fillRect(kLabelX, y, kLabelW, h, TFT_BLACK);
  if (selected) M5.Display.fillRoundRect(kLabelX, y + 3, kLabelW, h - 6, 4, bg);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(selected ? TFT_BLACK : kDimText, bg);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, y + h / 2);
}

// View tabs above the grid; the active one is filled.
void drawTabs() {
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  for (int v = 0; v < kViews; ++v) {
    int x = kGridX + v * kTabPitchX;
    uint16_t bg = v == view ? TFT_ORANGE : TFT_BLACK;
    M5.Display.fillRect(x, kTabY, kTabW, kTabH, TFT_BLACK);
    if (v == view) M5.Display.fillRoundRect(x, kTabY, kTabW, kTabH, 4, bg);
    M5.Display.setTextColor(v == view ? TFT_BLACK : kDimText, bg);
    M5.Display.drawString(kViewNames[v], x + kTabW / 2, kTabY + kTabH / 2 + 1);
  }
}

// Left of the tabs: the value being edited, otherwise how to switch tabs.
void drawTabCorner() {
  M5.Display.fillRect(kLabelX, kTabY, kLabelW, kTabH, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  if (editing && cursorRow != kMetaRow) {
    char text[8];
    snprintf(text, sizeof(text), "%+d", laneValue(cursorRow - 1, cursorCol));
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Display.drawString(text, kLabelX + kLabelW / 2, kTabY + kTabH / 2 + 1);
  } else {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(kDimText, TFT_BLACK);
    M5.Display.drawString("L+R:", kLabelX + kLabelW / 2, kTabY + kTabH / 2);
  }
}

// Redraws whatever sits at one cursor position.
void drawPosition(int row, int col) {
  if (row == kMetaRow) {
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
      "DONE",
      "NEW PATTERN",
      "PRESS AGAIN: ERASE",
      "EDIT  hold:PLAY",
      "ON/OFF  hold:PLAY",
      "PITCH  hold:PLAY",
      "DECAY  hold:PLAY",
  };
  int hints;
  if (editing) {
    hints = 0;
  } else if (cursorRow != kMetaRow) {
    hints = 4 + view;
  } else if (cursorCol == kFieldNew) {
    hints = confirmNew ? 2 : 1;
  } else {
    hints = 3;
  }
  if (hints == shownHints) return;
  shownHints = hints;

  M5.Display.fillRect(0, kHintY, 320, kHintH, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(bottom_center);
  M5.Display.setTextColor(kDimText, TFT_BLACK);
  M5.Display.drawString(editing ? "-" : "<  hold:UP", 65, 238);
  M5.Display.drawString(kMiddle[hints], 160, 238);
  M5.Display.drawString(editing ? "+" : "hold:DOWN  >", 255, 238);
}

void moveCursor(int row, int col) {
  int oldRow = cursorRow, oldCol = cursorCol;
  cursorRow = row;
  cursorCol = col;
  confirmNew = false;  // moving away cancels a pending NEW
  drawPosition(oldRow, oldCol);
  drawPosition(cursorRow, cursorCol);
  if (oldRow != cursorRow) {
    drawLabel(oldRow);
    drawLabel(cursorRow);
  }
  updateHints();
}

// Left / right within the row, wrapping.
void moveCol(int delta) {
  int cols = colsInRow(cursorRow);
  moveCursor(cursorRow, (cursorCol + delta + cols) % cols);
}

// Up / down a row, wrapping. Track rows share a column; the settings row
// remembers its own.
void moveRow(int delta) {
  (cursorRow == kMetaRow ? savedMetaCol : savedGridCol) = cursorCol;
  int row = (cursorRow + delta + kRows) % kRows;
  moveCursor(row, row == kMetaRow ? savedMetaCol : savedGridCol);
}

void setEditing(bool on) {
  editing = on;
  drawPosition(cursorRow, cursorCol);
  drawTabCorner();
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
    markDirty();
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
  drawTabCorner();
  markDirty();
  if (!seq.playing()) seq.auditionStep(track, cursorCol);  // hear the change
  return true;
}

void drawAllCells() {
  for (int t = 0; t < Sequencer::kTracks; ++t) {
    for (int s = 0; s < Sequencer::kSteps; ++s) drawCell(t, s);
  }
}

// Empty pattern, flat automation, three new random sounds. Tempo and swing stay.
void newPattern() {
  confirmNew = false;
  seq.clearPattern();
  seq.newKit(esp_random());
  drawAllCells();
  drawMetaField(kFieldNew);
  updateHints();
  markDirty();
}

void cycleView() {
  if (editing) setEditing(false);
  view = (view + 1) % kViews;
  drawAllCells();
  drawTabs();
  updateHints();
}

// True on the initial press, then repeatedly while the button stays held.
bool pressedOrRepeating(m5::Button_Class& button, uint32_t& nextRepeatAt) {
  uint32_t now = millis();
  if (button.wasPressed()) {
    nextRepeatAt = now + 400;
    return true;
  }
  if (button.isPressed() && (int32_t)(now - nextRepeatAt) >= 0) {
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
  drawTabs();
  drawTabCorner();
  updateHints();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 0 (the default) leaves Serial off
  M5.begin(cfg);
  M5.BtnA.setHoldThresh(kRowHoldMs);
  M5.BtnC.setHoldThresh(kRowHoldMs);

  // Pick up where the last session left off. With nothing saved, start with
  // an empty pattern and random sounds.
  if (!storage::load(seq)) seq.init(esp_random());
  drawStaticScreen();

  M5.Speaker.setVolume(160);
  audio::begin(seq);
  Serial.println("m5drums: ready");
}

void loop() {
  static uint32_t repeatA = 0, repeatC = 0;
  static bool chordHeld = false;
  static bool lastAdjustChanged = false;
  M5.update();

  // A + C together: next view.
  bool aDown = M5.BtnA.wasPressed(), cDown = M5.BtnC.wasPressed();
  if (!chordHeld && ((aDown && M5.BtnC.isPressed()) || (cDown && M5.BtnA.isPressed()))) {
    chordHeld = true;
    // While editing, the first button of the pair already changed the value
    // when it went down; take that back.
    if (editing && !(aDown && cDown) && lastAdjustChanged) adjustValue(aDown ? -1 : +1);
    cycleView();
  }

  if (chordHeld) {
    // Swallow everything A and C do until both are up again.
    if (!M5.BtnA.isPressed() && !M5.BtnC.isPressed()) chordHeld = false;
  } else if (editing) {
    // A / C: decrease / increase, repeating while held
    if (pressedOrRepeating(M5.BtnA, repeatA)) lastAdjustChanged = adjustValue(-1);
    if (pressedOrRepeating(M5.BtnC, repeatC)) lastAdjustChanged = adjustValue(+1);
  } else {
    // A / C short press: left / right. Acts on release so it can be told
    // apart from a hold, which moves up / down a row.
    if (M5.BtnA.wasClicked()) moveCol(-1);
    if (M5.BtnC.wasClicked()) moveCol(+1);
    if (M5.BtnA.wasHold()) moveRow(-1);
    if (M5.BtnC.wasHold()) moveRow(+1);
  }

  // B short press acts on release, so that a hold does not also trigger it.
  if (M5.BtnB.wasClicked()) {
    if (editing) {
      setEditing(false);
    } else if (cursorRow == kMetaRow && cursorCol == kFieldNew) {
      // Erasing takes two presses.
      if (confirmNew) {
        newPattern();
      } else {
        confirmNew = true;
        drawMetaField(kFieldNew);
        updateHints();
      }
    } else if (cursorRow == kMetaRow || view != kViewTrig) {
      setEditing(true);
    } else {
      int track = cursorRow - 1;
      seq.toggleStep(track, cursorCol);
      drawCell(track, cursorCol);
      markDirty();
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

  // Save once things have been left alone for a moment.
  if (dirty && millis() - dirtyAt > kSaveIdleMs) {
    dirty = false;
    storage::save(seq);
  }

  delay(2);
}
