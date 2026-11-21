// The screens and what the buttons do on them. Draws through M5.Display and
// reads the buttons and battery through M5, so that the same code runs on
// the device and in the simulator (sim/sim_main.cpp).
//
// The sequencer is one screen: a MENU button top left, then a grid of three
// drums by eight steps, with a cursor that goes anywhere on it. Holding the
// middle button on a drum's row opens that drum: the three rows are then its
// lanes of per-step values. Holding it again goes back.
//
// MENU opens a page of settings, a list in the manner of sketchbox-m5: left
// and right go up and down it, the middle button picks a setting to change
// (or does what the row says), and holding it goes back.
//
// A bar along the bottom always says what left, middle and right do.
#include "ui.h"

#include <M5Unified.h>

#include "bluetooth.h"
#include "exporter.h"
#include "gestures.h"
#include "storage.h"

namespace {

Sequencer* seq = nullptr;

// --- Rows the cursor can be on: the MENU button, then three grid rows ---
// On the main screen the grid rows are the three drums. With a drum open
// they are that drum's three lanes.
constexpr int kMetaRow = 0;
constexpr int kGridRows = 3;
constexpr int kRows = 1 + kGridRows;

// The three per-step values. What the lanes are called, and what the third
// one does, depends on which instrument the drum is.
enum Lane { kLanePitch = 0, kLaneDecay = 1, kLaneShape = 2, kLanes = 3 };

enum Setting {
  kTempo = 0,
  kSwing = 1,
  kEvolve = 2,     // off, or one of the named evolve themes
  kBluetooth = 3,  // experimental: play through a Bluetooth speaker
  kExport = 4,
  kNew = 5,
  kHelp = 6,
  kSettings = 7,
};
const char* const kSettingNames[kSettings] = {"TEMPO", "SWING", "EVOLVE", "BLUETOOTH",
                                              "EXPORT", "NEW", "HELP"};

static_assert(Sequencer::kTracks == kGridRows && kLanes == kGridRows,
              "the grid has one row per drum and one per lane");

// --- Layout (320x240): a bar along the top and one along the bottom ---
constexpr int kTopLine = 20, kBottomLine = 220;
constexpr int kPhraseX = 70, kPhraseW = 160, kPhraseY = 7, kPhraseH = 6;
constexpr int kPlayIconX = 262, kPlayIconY = 4, kPlayIconSize = 12;
constexpr int kBatteryX = 284, kBatteryY = 4, kBatteryW = 30, kBatteryH = 12;
// The grid sits between the bars, with the same margin left and right. The
// strip above it names the drum that is open; with none open it is empty.
constexpr int kLabelX = 3, kLabelW = 50;
constexpr int kGridX = 57, kCellPitchX = 33, kCellW = 29;
constexpr int kGridW = Sequencer::kSteps * kCellPitchX - (kCellPitchX - kCellW);
constexpr int kStripY = 27, kStripH = 19;
constexpr int kGridY = 51, kCellPitchY = 49, kCellH = 44;
constexpr int kPlayheadY = kGridY + 2 * kCellPitchY + kCellH + 6, kPlayheadH = 6;
constexpr int kSettingY = 28, kSettingPitch = 27;

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

constexpr uint32_t kSaveIdleMs = 2000;  // save this long after the last change
constexpr uint32_t kBatteryPollMs = 5000;

// --- UI state: what is currently on screen ---
int cursorRow = 1, cursorCol = 0;
int savedGridCol = 0;     // the step to return to from the MENU button
int detailTrack = -1;     // the drum that is open, or -1 on the main screen
bool editing = false;     // a drum is open and the value under the cursor is being changed
bool menuOpen = false;    // the page of settings is showing instead of the grid
bool picked = false;      // menu: the chosen setting is being changed with left and right
int setting = 0;          // menu: the row that is chosen
bool helpOpen = false;    // menu: the help page is showing instead of the list
bool confirmNew = false;  // menu: NEW was pressed once; pressing it again erases
bool restarting = false;  // Bluetooth is being turned off, which restarts the device
char exportNote[20] = "";  // what the last export came to, shown on its row
// The card for an export cannot be read: the screen is asking whether to
// erase and format it. Left is no, right is yes, the middle button puts the
// question away. Presses are ignored for its first second, since a yes
// erases the card.
bool cardQuestion = false;
bool formatting = false;  // ... and the answer was yes: nothing moves until it is done
uint32_t cardQuestionAt = 0;
constexpr uint32_t kQuestionDeafMs = 1000;
bool dirty = false;        // changes not yet saved to flash
uint32_t dirtyAt = 0;
voices::Family families[Sequencer::kTracks];  // which instrument each drum is
int shownStep = -1;
int shownBattery = -1000;  // level * 2 + charging, as last drawn
int shownPhrase = -1000;   // passes * 32 + pass, as last drawn
int shownBluetooth = bluetooth::kOff;
int shownPlaying = -1;

bool inDetail() { return detailTrack >= 0; }
// A value is being changed with left and right.
bool adjusting() { return menuOpen ? picked : editing; }
int rowY(int gridRow) { return kGridY + gridRow * kCellPitchY; }

int laneRange(int lane) {
  return lane == kLanePitch ? voices::kPitchRange
                            : (lane == kLaneDecay ? voices::kDecayRange : voices::kShapeRange);
}
int laneValue(int lane, int track, int step) {
  if (lane == kLanePitch) return seq->stepPitch(track, step);
  if (lane == kLaneDecay) return seq->stepDecay(track, step);
  return seq->stepShape(track, step);
}
void setLaneValue(int lane, int track, int step, int value) {
  if (lane == kLanePitch) {
    seq->setStepPitch(track, step, value);
  } else if (lane == kLaneDecay) {
    seq->setStepDecay(track, step, value);
  } else {
    seq->setStepShape(track, step, value);
  }
}

// An RGB565 colour at about 40 % brightness.
uint16_t dimmed(uint16_t color) {
  uint16_t r = ((color >> 11) & 0x1F) * 2 / 5;
  uint16_t g = ((color >> 5) & 0x3F) * 2 / 5;
  uint16_t b = (color & 0x1F) * 2 / 5;
  return (r << 11) | (g << 5) | b;
}

// An RGB565 colour blended most of the way to white.
uint16_t lightened(uint16_t color) {
  uint16_t r = (color >> 11) & 0x1F, g = (color >> 5) & 0x3F, b = color & 0x1F;
  r += (0x1F - r) * 2 / 3;
  g += (0x3F - g) * 2 / 3;
  b += (0x1F - b) * 2 / 3;
  return (r << 11) | (g << 5) | b;
}

void markDirty() {
  dirty = true;
  dirtyAt = millis();
}

void smallText(uint16_t color, uint16_t background, textdatum_t datum) {
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color, background);
  M5.Display.setTextDatum(datum);
}

// Everything below redraws one small region. Only opening and closing the
// menu redraws the whole area between the bars.

// ---- Top bar: the MENU button, the evolve phrase, play / stop, battery ----

// On the sequencer it is a button the cursor can go to (in a border, filled
// when the cursor is on it). With the menu open it is that page's name.
void drawMenuButton() {
  bool lit = menuOpen || cursorRow == kMetaRow;
  M5.Display.fillRect(0, 2, kPhraseX - 2, 16, TFT_BLACK);
  if (lit) {
    M5.Display.fillRoundRect(4, 3, 32, 14, 3, TFT_WHITE);
  } else {
    M5.Display.drawRoundRect(4, 3, 32, 14, 3, kDimText);
  }
  smallText(lit ? TFT_BLACK : kValueText, lit ? TFT_WHITE : TFT_BLACK, top_left);
  M5.Display.drawString("MENU", 8, 6);
}

// The power chip only reports the level in quarters, so the icon has four
// segments. Green while charging; hidden if the chip cannot be read.
void drawBattery(int level, bool charging) {
  M5.Display.fillRect(kBatteryX, kBatteryY, kBatteryW + 4, kBatteryH, TFT_BLACK);
  if (level < 0) return;
  uint16_t color = charging ? TFT_GREEN : (level <= 25 ? TFT_RED : kValueText);
  M5.Display.drawRect(kBatteryX, kBatteryY, kBatteryW, kBatteryH, color);
  M5.Display.fillRect(kBatteryX + kBatteryW, kBatteryY + 3, 3, kBatteryH - 6, color);  // terminal
  int segments = (level + 12) / 25;
  for (int i = 0; i < segments && i < 4; ++i) {
    M5.Display.fillRect(kBatteryX + 3 + i * 6, kBatteryY + 3, 5, kBatteryH - 6, color);
  }
}

// Reads the battery and redraws the icon if anything changed.
void updateBattery() {
  int level = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();
  int state = level * 2 + (charging ? 1 : 0);
  if (state == shownBattery) return;
  shownBattery = state;
  drawBattery(level, charging);
}

// Evolve phrase: one segment per pass of the pattern, the current one lit.
// Empty when evolve is off. Redrawn only on change.
void updatePhrase() {
  int passes = seq->evolvePasses(), pass = seq->evolvePass();
  int state = passes * 32 + pass;
  if (state == shownPhrase) return;
  shownPhrase = state;
  M5.Display.fillRect(kPhraseX, kPhraseY, kPhraseW, kPhraseH, TFT_BLACK);
  for (int i = 0; i < passes; ++i) {
    int x0 = kPhraseX + kPhraseW * i / passes, x1 = kPhraseX + kPhraseW * (i + 1) / passes;
    M5.Display.fillRect(x0, kPhraseY, x1 - x0 - 2, kPhraseH, i == pass ? TFT_WHITE : kOnBeat);
  }
}

void drawPlayIcon(bool playing) {
  int x = kPlayIconX, y = kPlayIconY, s = kPlayIconSize;
  M5.Display.fillRect(x, y, s + 1, s + 1, TFT_BLACK);
  if (playing) {
    M5.Display.fillTriangle(x, y, x, y + s, x + s, y + s / 2, TFT_GREEN);
  } else {
    M5.Display.fillRect(x + 1, y + 1, s - 2, s - 2, kDimText);
  }
}

// ---- Bottom bar: what left, middle and right do ----

// The middle label names what the cursor is on. It is plain on the main
// screen, in a border one level in (a drum is open, or the menu), and on
// orange while left and right are changing a value.
void drawBottom(bool all) {
  char left[8], middle[20], right[8];
  int inside = (menuOpen || inDetail()) ? 1 : 0;
  snprintf(left, sizeof(left), menuOpen ? "UP" : "<");
  snprintf(right, sizeof(right), menuOpen ? "DOWN" : ">");
  if (adjusting()) {
    snprintf(left, sizeof(left), "-");
    snprintf(right, sizeof(right), "+");
    inside = 2;
  }
  if (cardQuestion) {
    // The question about the card: no and yes, and nothing while formatting.
    snprintf(left, sizeof(left), formatting ? "" : "NO");
    snprintf(right, sizeof(right), formatting ? "" : "YES");
    middle[0] = 0;
    inside = 0;
  } else if (helpOpen) {
    left[0] = right[0] = 0;
    snprintf(middle, sizeof(middle), "HELP");
  } else if (menuOpen) {
    snprintf(middle, sizeof(middle), "%s", kSettingNames[setting]);
  } else if (cursorRow == kMetaRow) {
    left[0] = right[0] = 0;  // the button is the only thing in its row
    snprintf(middle, sizeof(middle), "MENU");
  } else if (inDetail()) {
    // The lane's name in full; the value itself is shown on the step.
    snprintf(middle, sizeof(middle), "%s", voices::laneFullName(families[detailTrack], cursorRow - 1));
  } else {
    snprintf(middle, sizeof(middle), "%s %s", tracks[cursorRow - 1].label,
             voices::familyName(families[cursorRow - 1]));
  }

  static char last[48] = "";
  char now[sizeof(last)];
  snprintf(now, sizeof(now), "%s|%s|%s|%d", left, middle, right, inside);
  if (!all && strcmp(now, last) == 0) return;
  strcpy(last, now);

  M5.Display.fillRect(0, kBottomLine, 320, 240 - kBottomLine, TFT_BLACK);
  M5.Display.drawFastHLine(0, kBottomLine, 320, kOnBeat);
  smallText(kDimText, TFT_BLACK, top_center);
  M5.Display.drawString(left, 65, 227);
  M5.Display.drawString(right, 255, 227);
  if (inside == 1) M5.Display.drawRoundRect(108, 223, 104, 15, 4, kDimText);
  if (inside == 2) M5.Display.fillRoundRect(108, 223, 104, 15, 4, TFT_ORANGE);
  smallText(inside == 2 ? TFT_BLACK : TFT_WHITE, inside == 2 ? TFT_ORANGE : TFT_BLACK, top_center);
  M5.Display.drawString(middle, 160, 227);
}

// ---- The grid ----

// gridRow is 0..2: a drum on the main screen, a lane of the open drum.
void drawCell(int gridRow, int step) {
  int x = kGridX + step * kCellPitchX;
  int y = rowY(gridRow);
  int track = inDetail() ? detailTrack : gridRow;
  bool on = seq->stepOn(track, step);
  bool cursor = cursorRow == gridRow + 1 && cursorCol == step;
  uint16_t frame = cursor ? (editing ? TFT_ORANGE : TFT_WHITE) : TFT_BLACK;
  uint16_t shade = step % 4 == 0 ? kOnBeat : kOffBeat;
  uint16_t color = tracks[track].color;
  bool firing = on && step == shownStep;  // the playhead is on this hit right now
  M5.Display.fillRect(x, y, kCellW, kCellH, frame);

  if (!inDetail()) {
    M5.Display.fillRoundRect(x + 3, y + 3, kCellW - 6, kCellH - 6, 4,
                             on ? (firing ? lightened(color) : color) : shade);
    return;
  }

  // Lane: a bar growing up or down from the centre line. Steps with a hit get
  // a dimmed tint of the drum's colour and a full-colour bar: visibly marked,
  // but not the solid block that means "toggle me" on the main screen. Steps
  // without a hit stay grey. While the hit is firing the tint goes to full
  // colour and the bar to black.
  uint16_t tint = on ? (firing ? color : dimmed(color)) : shade;
  uint16_t barColor = on ? (firing ? TFT_BLACK : color) : kDimText;
  M5.Display.fillRoundRect(x + 3, y + 3, kCellW - 6, kCellH - 6, 4, tint);
  int mid = y + kCellH / 2;
  int value = laneValue(gridRow, track, step);
  int len = value * (kCellH / 2 - 5) / laneRange(gridRow);
  int barX = x + 7, barW = kCellW - 14;
  if (len > 0) M5.Display.fillRect(barX, mid - len, barW, len, barColor);
  if (len < 0) M5.Display.fillRect(barX, mid, barW, -len, barColor);
  M5.Display.fillRect(barX - 2, mid - 1, barW + 4, 2, barColor);

  // The step under the cursor shows its value as a number, in the half of
  // the cell its bar leaves free.
  if (cursor) {
    char text[6];
    snprintf(text, sizeof(text), value ? "%+d" : "0", value);
    smallText(editing ? TFT_ORANGE : TFT_WHITE, tint, middle_center);
    M5.Display.drawString(text, x + kCellW / 2, value > 0 ? mid + 10 : mid - 9);
  }
}

// Row name: a drum, or a lane of the open drum. The row the cursor is on is
// a filled block.
void drawLabel(int gridRow) {
  int y = rowY(gridRow);
  bool selected = cursorRow == gridRow + 1;
  const char* text = inDetail() ? voices::laneName(families[detailTrack], gridRow)
                                : tracks[gridRow].label;
  uint16_t color = tracks[inDetail() ? detailTrack : gridRow].color;
  uint16_t bg = selected ? color : TFT_BLACK;

  M5.Display.fillRect(kLabelX, y, kLabelW, kCellH, TFT_BLACK);
  if (selected) M5.Display.fillRoundRect(kLabelX, y + 3, kLabelW, kCellH - 6, 4, bg);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(selected ? TFT_BLACK : kDimText, bg);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, y + kCellH / 2);
}

// Above the grid: which drum is open, and which instrument it is.
void drawStrip() {
  M5.Display.fillRect(kGridX, kStripY, kGridW, kStripH, TFT_BLACK);
  if (!inDetail()) return;
  char text[16];
  snprintf(text, sizeof(text), "%s: %s", tracks[detailTrack].label,
           voices::familyName(families[detailTrack]));
  uint16_t color = tracks[detailTrack].color;
  M5.Display.fillRoundRect(kGridX, kStripY, kGridW, kStripH, 4, color);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_BLACK, color);
  M5.Display.drawString(text, kGridX + kGridW / 2, kStripY + kStripH / 2 + 1);
}

void drawPlayheadMark(int step, bool lit) {
  M5.Display.fillRect(kGridX + step * kCellPitchX + 3, kPlayheadY, kCellW - 6, kPlayheadH,
                      lit ? TFT_GREEN : kOffBeat);
}

// Redraws the hits in one column, so they flash as the playhead passes.
void drawHitsInColumn(int step) {
  for (int r = 0; r < kGridRows; ++r) {
    if (seq->stepOn(inDetail() ? detailTrack : r, step)) drawCell(r, step);
  }
}

// Redraws everything that differs between the main screen and an open drum.
void drawGridArea() {
  drawStrip();
  for (int r = 0; r < kGridRows; ++r) {
    drawLabel(r);
    for (int s = 0; s < Sequencer::kSteps; ++s) drawCell(r, s);
  }
}

// Redraws whatever sits at one cursor position.
void drawPosition(int row, int col) {
  if (row == kMetaRow) {
    drawMenuButton();
  } else {
    drawCell(row - 1, col);
  }
}

void moveCursor(int row, int col) {
  int oldRow = cursorRow, oldCol = cursorCol;
  cursorRow = row;
  cursorCol = col;
  drawPosition(oldRow, oldCol);
  drawPosition(cursorRow, cursorCol);
  if (oldRow != cursorRow) {
    if (oldRow != kMetaRow) drawLabel(oldRow - 1);
    if (cursorRow != kMetaRow) drawLabel(cursorRow - 1);
  }
}

// Left / right within the row, wrapping. The MENU button is alone in its row.
void moveCol(int delta) {
  if (cursorRow == kMetaRow) return;
  moveCursor(cursorRow, (cursorCol + delta + Sequencer::kSteps) % Sequencer::kSteps);
}

// Up / down a row, wrapping: the MENU button, then the three grid rows. The
// grid rows share a step, which the MENU button remembers.
void moveRow(int delta) {
  if (cursorRow != kMetaRow) savedGridCol = cursorCol;
  int row = (cursorRow + delta + kRows) % kRows;
  moveCursor(row, row == kMetaRow ? 0 : savedGridCol);
}

// Opens a drum, with the cursor on the same step of its first lane.
void enterDetail(int track) {
  detailTrack = track;
  cursorRow = 1 + kLanePitch;
  drawGridArea();
}

// Back to the main screen, with the cursor on the drum that was open.
void exitDetail() {
  editing = false;
  bool wasOnMeta = cursorRow == kMetaRow;
  if (wasOnMeta) cursorCol = savedGridCol;
  cursorRow = 1 + detailTrack;
  detailTrack = -1;
  if (wasOnMeta) drawMenuButton();  // the cursor has left it
  drawGridArea();
}

// ---- The menu: one row for each setting ----

void settingValue(int item, char* text, size_t size) {
  switch (item) {
    case kTempo:
      snprintf(text, size, "%d", seq->tempo());
      break;
    case kSwing:
      snprintf(text, size, "%d%%", seq->swing());
      break;
    case kEvolve:
      snprintf(text, size, "%s", Sequencer::evolveThemeName(seq->evolveTheme()));
      break;
    case kBluetooth:
      if (restarting) {
        snprintf(text, size, "RESTARTING");
      } else if (bluetooth::state() == bluetooth::kOff) {
        snprintf(text, size, "OFF");
      } else if (bluetooth::state() == bluetooth::kSearching) {
        snprintf(text, size, "SEARCHING");
      } else if (bluetooth::state() == bluetooth::kConnecting) {
        snprintf(text, size, "CONNECTING");
      } else {
        snprintf(text, size, "%.12s", bluetooth::deviceName());
      }
      break;
    case kExport:
      snprintf(text, size, "%s", exportNote);
      break;
    case kNew:
      snprintf(text, size, confirmNew ? "ARE YOU SURE?" : "");
      break;
    default:
      text[0] = 0;
      break;
  }
}

void drawSetting(int item) {
  int y = kSettingY + item * kSettingPitch;
  bool marked = item == setting;
  // While one value is being changed the other rows step back.
  uint16_t color = marked ? TFT_WHITE : (picked ? kOnBeat : kDimText);
  // Orange: a value being changed, or an erase waiting to be confirmed.
  bool hot = marked && (picked || (item == kNew && confirmNew));
  char value[20];
  settingValue(item, value, sizeof(value));

  M5.Display.fillRect(0, y - 5, 320, kSettingPitch - 1, TFT_BLACK);
  if (marked) M5.Display.fillRect(0, y - 3, 3, 22, TFT_ORANGE);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.drawString(kSettingNames[item], 10, y);
  M5.Display.setTextDatum(top_right);
  M5.Display.setTextColor(hot ? TFT_ORANGE : color, TFT_BLACK);
  M5.Display.drawString(value, 312, y);
}

void clearContent() {
  M5.Display.fillRect(0, kTopLine + 1, 320, kBottomLine - kTopLine - 1, TFT_BLACK);
}

void drawSettings() {
  for (int i = 0; i < kSettings; ++i) drawSetting(i);
}

// What the buttons do, on a page of its own. L, M and R are the left,
// middle and right buttons.
void drawHelp() {
  struct Line {
    const char* keys;  // nullptr marks a heading
    const char* action;
  };
  static const Line kLines[] = {
      {"L / R", "left / right"},
      {"M", "step on / off"},
      {"hold L, R", "row down"},
      {"hold R, L", "row up"},
      {"hold L+R", "play / stop"},
      {"hold M", "drum / back"},
      {nullptr, "IN A DRUM"},
      {"M", "edit, done"},
      {"L / R", "lower / raise"},
  };
  clearContent();
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(top_left);
  int y = kTopLine + 8;
  for (const Line& line : kLines) {
    if (line.keys == nullptr) {
      M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
      M5.Display.drawString(line.action, 10, y);
    } else {
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.drawString(line.keys, 10, y);
      M5.Display.setTextColor(kValueText, TFT_BLACK);
      M5.Display.drawString(line.action, 142, y);
    }
    y += 21;
  }
}

// Leaving a row forgets its question or its result.
void clearSettingNotes() {
  confirmNew = false;
  exportNote[0] = 0;
}

void openMenu() {
  menuOpen = true;
  picked = false;
  setting = 0;
  clearSettingNotes();
  clearContent();
  drawSettings();
}

// Takes the menu away and draws the sequencer again.
void closeMenu() {
  menuOpen = picked = helpOpen = false;
  clearSettingNotes();
  clearContent();
  shownStep = -1;  // not followed while the menu was up; the loop lights the current step again
  drawMenuButton();
  drawGridArea();
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, false);
}

void closeHelp() {
  helpOpen = false;
  clearContent();
  drawSettings();
}

// Empty pattern, flat automation, three new random sounds, a random tempo
// (80-170 BPM) and swing (50-66 %), and evolve off.
void newPattern() {
  seq->clearPattern();
  seq->newKit(esp_random());
  voices::familiesForSeed(seq->kitSeed(), families);
  seq->setTempo(80 + esp_random() % 91);
  seq->setSwing(50 + esp_random() % 17);
  seq->setEvolveTheme(0);
  markDirty();
}

// The question about a card that cannot be read, over the whole page; or,
// once the answer is yes, that it is being formatted.
void drawCardQuestion() {
  clearContent();
  M5.Display.setTextDatum(top_left);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(formatting ? "CARD" : "CARD UNREADABLE", 16, 62);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5.Display.drawString(formatting ? "FORMATTING..." : "ERASE AND FORMAT?", 16, 106);
}

void exportPattern(bool afterFormat = false);

void closeCardQuestion() {
  cardQuestion = formatting = false;
  clearContent();
  drawSettings();
}

// Yes: erase and format the card, then carry on with the export that asked.
void formatCard() {
  formatting = true;
  drawCardQuestion();
  drawBottom(false);
  // The card and the display share their wires: nothing on the screen moves
  // until this returns.
  bool ok = exporter::formatCard();
  closeCardQuestion();
  if (ok) {
    exportPattern(true);
  } else {
    snprintf(exportNote, sizeof(exportNote), "COULD NOT FORMAT");
    drawSetting(kExport);
  }
}

void exportPattern(bool afterFormat) {
  snprintf(exportNote, sizeof(exportNote), "EXPORTING");
  drawSetting(kExport);
  char name[24];
  switch (exporter::exportWav(*seq, name, sizeof(name))) {
    case exporter::kOk:
      snprintf(exportNote, sizeof(exportNote), "%s", name);
      break;
    case exporter::kNoCard:
      snprintf(exportNote, sizeof(exportNote), "NO SD CARD");
      break;
    case exporter::kUnreadable:
      if (afterFormat) {  // it was formatted, and still: do not ask again
        snprintf(exportNote, sizeof(exportNote), "COULD NOT FORMAT");
        break;
      }
      exportNote[0] = 0;
      cardQuestion = true;
      cardQuestionAt = millis();
      drawCardQuestion();
      return;
    default:
      snprintf(exportNote, sizeof(exportNote), "WRITE FAILED");
      break;
  }
  drawSetting(kExport);
}

// Changes the value being changed. Returns false if it was already at its limit.
bool adjustValue(int delta) {
  if (!menuOpen) {
    int lane = cursorRow - 1;
    int before = laneValue(lane, detailTrack, cursorCol);
    setLaneValue(lane, detailTrack, cursorCol, before + delta);
    if (laneValue(lane, detailTrack, cursorCol) == before) return false;
    drawCell(lane, cursorCol);
    markDirty();
    return true;
  }
  int before, after;
  if (setting == kTempo) {
    before = seq->tempo();
    seq->setTempo(before + delta);
    after = seq->tempo();
  } else if (setting == kSwing) {
    before = seq->swing();
    seq->setSwing(before + delta);
    after = seq->swing();
  } else {
    before = seq->evolveTheme();
    seq->setEvolveTheme(before + delta);
    after = seq->evolveTheme();
  }
  if (after == before) return false;
  drawSetting(setting);
  markDirty();
  return true;
}

// Left (-1) or right (+1). Returns false if nothing changed.
bool nudge(int delta) {
  if (cardQuestion) {
    if (millis() - cardQuestionAt < kQuestionDeafMs) return false;
    if (delta > 0) {
      formatCard();
    } else {
      closeCardQuestion();
    }
    return true;
  }
  if (helpOpen) return false;
  if (adjusting()) return adjustValue(delta);
  if (menuOpen) {
    int old = setting;
    setting = (setting + delta + kSettings) % kSettings;
    clearSettingNotes();
    drawSetting(old);
    drawSetting(setting);
  } else {
    moveCol(delta);
  }
  return true;
}

// The middle button's short press: "enter" on whatever the cursor is on.
void pressEnter() {
  if (cardQuestion) {
    // Puts the question away, the card untouched.
    if (millis() - cardQuestionAt >= kQuestionDeafMs) closeCardQuestion();
  } else if (helpOpen) {
    closeHelp();
  } else if (!menuOpen) {
    if (editing) {
      editing = false;
      drawCell(cursorRow - 1, cursorCol);
    } else if (cursorRow == kMetaRow) {
      openMenu();
    } else if (inDetail()) {
      editing = true;
      drawCell(cursorRow - 1, cursorCol);
    } else {
      seq->toggleStep(cursorRow - 1, cursorCol);
      drawCell(cursorRow - 1, cursorCol);
      markDirty();
    }
  } else if (picked) {
    picked = false;  // done changing the value
    drawSettings();
  } else if (setting == kTempo || setting == kSwing || setting == kEvolve) {
    picked = true;
    drawSettings();
  } else if (setting == kNew) {
    // Erasing takes two presses.
    if (confirmNew) {
      newPattern();
      // Land on the first step of the first drum, ready to write.
      detailTrack = -1;
      editing = false;
      cursorRow = 1;
      cursorCol = 0;
      savedGridCol = 0;
      closeMenu();
    } else {
      confirmNew = true;
      drawSetting(kNew);
    }
  } else if (setting == kBluetooth) {
    if (bluetooth::state() == bluetooth::kOff) {
      bluetooth::start(*seq);
      drawSetting(kBluetooth);
    } else {
      // Bluetooth cannot be shut down cleanly, so turning it off restarts.
      restarting = true;
      drawSetting(kBluetooth);
      storage::save(*seq);
      delay(300);
      ESP.restart();
    }
  } else if (setting == kExport) {
    exportPattern();
  } else {
    helpOpen = true;
    drawHelp();
  }
}

// The middle button held on its own: open the drum under the cursor, or go
// back out of wherever we are.
void pressBack() {
  if (cardQuestion) {
    closeCardQuestion();
  } else if (helpOpen) {
    closeHelp();
  } else if (menuOpen && picked) {
    picked = false;
    drawSettings();
  } else if (menuOpen) {
    closeMenu();
  } else if (inDetail()) {
    exitDetail();
  } else if (cursorRow != kMetaRow) {
    enterDetail(cursorRow - 1);
  }
}

// What the button gestures (lib/controls/gestures.h) do on these screens.
struct Screens : GestureTarget {
  bool onGrid() override { return !menuOpen && !editing; }
  bool changingValue() override { return adjusting(); }
  bool step(int direction) override { return nudge(direction); }
  void row(int direction) override { moveRow(direction); }
  void playStop() override { seq->setPlaying(!seq->playing()); }
  void enter() override { pressEnter(); }
  void back() override { pressBack(); }
};

Screens screens;
Gestures gestures;

}  // namespace

namespace ui {

void begin(Sequencer& sequencer, bool restored) {
  seq = &sequencer;
  if (!restored) newPattern();
  voices::familiesForSeed(seq->kitSeed(), families);

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.drawFastHLine(0, kTopLine, 320, kOnBeat);
  drawMenuButton();
  drawGridArea();
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, false);
  updateBattery();
  drawBottom(true);
}

void update() {
  gestures.update(M5.BtnA.isPressed(), M5.BtnB.isPressed(), M5.BtnC.isPressed(), millis(), screens);

  updatePhrase();
  drawBottom(false);

  // Keep the menu's Bluetooth row current while it searches and connects.
  if (bluetooth::state() != shownBluetooth) {
    shownBluetooth = bluetooth::state();
    if (menuOpen && !helpOpen && !cardQuestion) drawSetting(kBluetooth);
  }

  int playing = seq->playing();
  if (playing != shownPlaying) {
    shownPlaying = playing;
    drawPlayIcon(playing);
  }

  // The menu covers the grid, so the playhead is not followed while it is open.
  int step = seq->currentStep();
  if (!menuOpen && step != shownStep) {
    int old = shownStep;
    shownStep = step;  // drawCell reads this to decide which hits are lit
    if (old >= 0) {
      drawPlayheadMark(old, false);
      drawHitsInColumn(old);
    }
    if (step >= 0) {
      drawPlayheadMark(step, true);
      drawHitsInColumn(step);
    }
  }

  static uint32_t batteryCheckedAt = 0;
  if (millis() - batteryCheckedAt > kBatteryPollMs) {
    batteryCheckedAt = millis();
    updateBattery();
  }

  // Save once things have been left alone for a moment.
  if (dirty && millis() - dirtyAt > kSaveIdleMs) {
    dirty = false;
    storage::save(*seq);
  }
}

}  // namespace ui
