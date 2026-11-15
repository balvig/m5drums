// Sequencer UI: settings row, step grid, per-drum detail screen, playhead.
#include <M5Unified.h>

#include "audio.h"
#include "bluetooth.h"
#include "exporter.h"
#include "storage.h"

namespace {

Sequencer seq;

// --- Rows: one settings ("meta") row, then three grid rows ---
// On the main screen the grid rows are the three drums. On a drum's detail
// screen they are that drum's three automation lanes.
constexpr int kMetaRow = 0;
constexpr int kGridRows = 3;
constexpr int kRows = 1 + kGridRows;

// The top row has a single cell: MENU, in the row-name column. It opens a
// drop-down list of settings and actions.
enum MetaField { kFieldMenu = 0, kMetaFields = 1 };

// The three per-step values. What the lanes are called, and what the third
// one does, depends on which instrument the drum is.
enum Lane { kLanePitch = 0, kLaneDecay = 1, kLaneShape = 2, kLanes = 3 };

enum MenuItem {
  kMenuNew = 0,
  kMenuTempo = 1,
  kMenuSwing = 2,
  kMenuEvolve = 3,  // off, or one of the named evolve themes
  kMenuBluetooth = 4,  // experimental: play through a Bluetooth speaker
  kMenuExport = 5,
  kMenuControls = 6,
  kMenuClose = 7,
  kMenuItems = 8,
};

static_assert(Sequencer::kTracks == kGridRows && kLanes == kGridRows,
              "the grid has one row per drum and one per lane");

// --- Layout (320x240) ---
constexpr int kLabelX = 2, kLabelW = 50;
constexpr int kMetaY = 2, kMetaH = 30;
constexpr int kPlayIconX = 290;
constexpr int kBatteryX = 240, kBatteryW = 30, kBatteryH = 14;
constexpr int kPhraseX = 60, kPhraseW = 168, kPhraseY = 13, kPhraseH = 8;
constexpr int kStripY = 35, kStripH = 19;
constexpr int kGridX = 56, kCellPitchX = 33, kCellW = 29;
constexpr int kGridY = 58, kCellPitchY = 47, kCellH = 42;
constexpr int kGridW = Sequencer::kSteps * kCellPitchX - (kCellPitchX - kCellW);
constexpr int kPlayheadY = 200, kPlayheadH = 6;
// The menu drops down from the MENU button, over the top-left of the grid.
constexpr int kMenuX = 2, kMenuY = 34, kMenuW = 190, kMenuItemH = 24;
constexpr int kMenuH = kMenuItems * kMenuItemH + 8;
// The controls page covers everything below the top row.
constexpr int kHelpY = 34, kHelpH = 240 - kHelpY;

struct FieldBox {
  int x, w;
};
const FieldBox fieldBoxes[kMetaFields] = {{kLabelX, kLabelW}};

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

constexpr uint32_t kRowHoldMs = 350;    // holding A / C this long changes row
constexpr uint32_t kSaveIdleMs = 2000;  // save this long after the last change
constexpr uint32_t kBatteryPollMs = 5000;

// --- UI state: what is currently on screen ---
int cursorRow = 1, cursorCol = 0;
int savedMetaCol = kFieldMenu, savedGridCol = 0;  // column to return to in each kind of row
int detailTrack = -1;     // drum whose detail screen is open, or -1 on the main screen
bool editing = false;     // adjusting the value under the cursor
bool menuOpen = false;
int menuIndex = 0;
bool menuConfirm = false;  // NEW PATTERN was chosen once; choosing it again erases
bool menuMessage = false;  // the menu box is showing a message instead of its items
bool helpOpen = false;     // the controls page is covering the grid
bool dirty = false;       // changes not yet saved to flash
uint32_t dirtyAt = 0;
voices::Family families[Sequencer::kTracks];  // which instrument each drum is
int shownStep = -1;
int shownBattery = -1000;  // level * 2 + charging, as last drawn
int shownPhrase = -1000;   // passes * 32 + pass, as last drawn
int shownBluetooth = bluetooth::kOff;
bool shownPlaying = false;

void drawMenuItem(int item);

bool inDetail() { return detailTrack >= 0; }
int colsInRow(int row) { return row == kMetaRow ? (int)kMetaFields : Sequencer::kSteps; }
int rowY(int row) { return row == kMetaRow ? kMetaY : kGridY + (row - 1) * kCellPitchY; }
int rowH(int row) { return row == kMetaRow ? kMetaH : kCellH; }

int laneRange(int lane) {
  return lane == kLanePitch ? voices::kPitchRange
                            : (lane == kLaneDecay ? voices::kDecayRange : voices::kShapeRange);
}
int laneValue(int lane, int track, int step) {
  if (lane == kLanePitch) return seq.stepPitch(track, step);
  if (lane == kLaneDecay) return seq.stepDecay(track, step);
  return seq.stepShape(track, step);
}
void setLaneValue(int lane, int track, int step, int value) {
  if (lane == kLanePitch) {
    seq.setStepPitch(track, step, value);
  } else if (lane == kLaneDecay) {
    seq.setStepDecay(track, step, value);
  } else {
    seq.setStepShape(track, step, value);
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

// Everything below redraws one small region. No full-screen redraws after
// setup(): they cause audible glitches.

// gridRow is 0..2: a drum on the main screen, a lane on a detail screen.
void drawCell(int gridRow, int step) {
  int x = kGridX + step * kCellPitchX;
  int y = rowY(gridRow + 1);
  int track = inDetail() ? detailTrack : gridRow;
  bool on = seq.stepOn(track, step);
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
  // without a hit stay grey.
  // While the hit is firing the tint goes to full colour and the bar to black.
  M5.Display.fillRoundRect(x + 3, y + 3, kCellW - 6, kCellH - 6, 4,
                           on ? (firing ? color : dimmed(color)) : shade);
  uint16_t barColor = on ? (firing ? TFT_BLACK : color) : kDimText;
  int mid = y + kCellH / 2;
  int len = laneValue(gridRow, track, step) * (kCellH / 2 - 5) / laneRange(gridRow);
  int barX = x + 7, barW = kCellW - 14;
  if (len > 0) M5.Display.fillRect(barX, mid - len, barW, len, barColor);
  if (len < 0) M5.Display.fillRect(barX, mid, barW, -len, barColor);
  M5.Display.fillRect(barX - 2, mid - 1, barW + 4, 2, barColor);
}

void drawMetaField(int field) {
  const FieldBox& box = fieldBoxes[field];
  bool cursor = cursorRow == kMetaRow && cursorCol == field;
  uint16_t bg = (cursor && menuOpen) ? TFT_ORANGE : TFT_BLACK;

  M5.Display.fillRect(box.x, kMetaY, box.w, kMetaH, TFT_BLACK);
  M5.Display.fillRoundRect(box.x, kMetaY, box.w, kMetaH, 4, bg);
  if (cursor) M5.Display.drawRoundRect(box.x, kMetaY, box.w, kMetaH, 4, TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(bg == TFT_ORANGE ? TFT_BLACK : (cursor ? TFT_WHITE : kValueText), bg);
  M5.Display.drawString("MENU", box.x + box.w / 2, kMetaY + kMetaH / 2);
}

// Row name: a drum, or a lane on a detail screen. The row the cursor is on is
// shown as a filled block.
void drawLabel(int row) {
  if (row == kMetaRow) return;  // that corner is the MENU button, not a name
  int y = rowY(row), h = rowH(row);
  bool selected = row == cursorRow;
  const char* text = inDetail() ? voices::laneName(families[detailTrack], row - 1)
                                : tracks[row - 1].label;
  uint16_t color = tracks[inDetail() ? detailTrack : row - 1].color;
  uint16_t bg = selected ? color : TFT_BLACK;

  M5.Display.fillRect(kLabelX, y, kLabelW, h, TFT_BLACK);
  if (selected) M5.Display.fillRoundRect(kLabelX, y + 3, kLabelW, h - 6, 4, bg);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(selected ? TFT_BLACK : kDimText, bg);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, y + h / 2);
}

// Strip above the grid: which drum a detail screen belongs to, and which
// instrument that drum is.
void drawStrip() {
  M5.Display.fillRect(kGridX, kStripY, kGridW, kStripH, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  int cx = kGridX + kGridW / 2, cy = kStripY + kStripH / 2;
  if (inDetail()) {
    char text[16];
    snprintf(text, sizeof(text), "%s: %s", tracks[detailTrack].label,
             voices::familyName(families[detailTrack]));
    uint16_t color = tracks[detailTrack].color;
    M5.Display.fillRoundRect(kGridX, kStripY, kGridW, kStripH, 4, color);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_BLACK, color);
    M5.Display.drawString(text, cx, cy + 1);
  }
}

// Left of the strip: the lane value being edited.
void drawStripCorner() {
  M5.Display.fillRect(kLabelX, kStripY, kLabelW, kStripH, TFT_BLACK);
  if (!(editing && inDetail() && cursorRow != kMetaRow)) return;
  char text[8];
  snprintf(text, sizeof(text), "%+d", laneValue(cursorRow - 1, detailTrack, cursorCol));
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5.Display.drawString(text, kLabelX + kLabelW / 2, kStripY + kStripH / 2 + 1);
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

// Battery icon in the top row, left of the play icon. The Gray's power chip
// only reports the level in quarters, so the icon has four segments. Green
// while charging; hidden if the chip cannot be read.
void drawBattery(int level, bool charging) {
  int y = kMetaY + (kMetaH - kBatteryH) / 2;
  M5.Display.fillRect(kBatteryX, y, kBatteryW + 4, kBatteryH, TFT_BLACK);
  if (level < 0) return;
  uint16_t color = charging ? TFT_GREEN : (level <= 25 ? TFT_RED : kValueText);
  M5.Display.drawRect(kBatteryX, y, kBatteryW, kBatteryH, color);
  M5.Display.fillRect(kBatteryX + kBatteryW, y + 4, 3, kBatteryH - 8, color);  // terminal
  int segments = (level + 12) / 25;
  for (int i = 0; i < segments && i < 4; ++i) {
    M5.Display.fillRect(kBatteryX + 3 + i * 6, y + 3, 5, kBatteryH - 6, color);
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

// Evolve phrase in the top row: one segment per pass of the pattern, the
// current one lit. Empty when evolve is off. Redrawn only on change.
void updatePhrase() {
  int passes = seq.evolvePasses(), pass = seq.evolvePass();
  int state = passes * 32 + pass;
  if (state == shownPhrase) return;
  shownPhrase = state;
  M5.Display.fillRect(kPhraseX, kPhraseY, kPhraseW, kPhraseH, TFT_BLACK);
  for (int i = 0; i < passes; ++i) {
    int x0 = kPhraseX + kPhraseW * i / passes, x1 = kPhraseX + kPhraseW * (i + 1) / passes;
    M5.Display.fillRect(x0, kPhraseY, x1 - x0 - 2, kPhraseH, i == pass ? TFT_WHITE : kOnBeat);
  }
}

// True if the open menu is covering this grid column.
bool underMenu(int step) {
  return menuOpen && kGridX + step * kCellPitchX < kMenuX + kMenuW;
}

// Redraws the hits in one column, so they flash as the playhead passes.
void drawHitsInColumn(int step) {
  if (underMenu(step)) return;
  for (int r = 0; r < kGridRows; ++r) {
    if (seq.stepOn(inDetail() ? detailTrack : r, step)) drawCell(r, step);
  }
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

void drawAllCells() {
  for (int r = 0; r < kGridRows; ++r) {
    for (int s = 0; s < Sequencer::kSteps; ++s) drawCell(r, s);
  }
}

// Redraws everything that differs between the main and detail screens.
void drawGridArea() {
  for (int row = 1; row < kRows; ++row) drawLabel(row);
  drawAllCells();
  drawStrip();
  drawStripCorner();
}

void moveCursor(int row, int col) {
  int oldRow = cursorRow, oldCol = cursorCol;
  cursorRow = row;
  cursorCol = col;
  drawPosition(oldRow, oldCol);
  drawPosition(cursorRow, cursorCol);
  if (oldRow != cursorRow) {
    drawLabel(oldRow);
    drawLabel(cursorRow);
  }
}

// Left / right within the row, wrapping.
void moveCol(int delta) {
  int cols = colsInRow(cursorRow);
  moveCursor(cursorRow, (cursorCol + delta + cols) % cols);
}

// Up / down a row, wrapping. Grid rows share a column; the settings row
// remembers its own.
void moveRow(int delta) {
  (cursorRow == kMetaRow ? savedMetaCol : savedGridCol) = cursorCol;
  int row = (cursorRow + delta + kRows) % kRows;
  moveCursor(row, row == kMetaRow ? savedMetaCol : savedGridCol);
}

void setEditing(bool on) {
  editing = on;
  drawPosition(cursorRow, cursorCol);
  drawStripCorner();
}

// Opens a drum's detail screen, with the cursor on the same step of its
// first lane.
void enterDetail(int track) {
  detailTrack = track;
  cursorRow = 1 + kLanePitch;
  drawGridArea();
}

// Back to the main screen, with the cursor on the drum that was open.
void exitDetail() {
  editing = false;
  bool wasOnMeta = cursorRow == kMetaRow;
  if (wasOnMeta) {
    savedMetaCol = cursorCol;
    cursorCol = savedGridCol;
  }
  cursorRow = 1 + detailTrack;
  detailTrack = -1;
  if (wasOnMeta) drawMetaField(savedMetaCol);  // the cursor has left it
  drawGridArea();
}

// Changes the value being edited. Returns false if it was already at its limit.
bool adjustValue(int delta) {
  if (menuOpen) {
    bool changed = false;
    switch (menuIndex) {
      case kMenuTempo: {
        int before = seq.tempo();
        seq.setTempo(before + delta);
        changed = seq.tempo() != before;
        break;
      }
      case kMenuSwing: {
        int before = seq.swing();
        seq.setSwing(before + delta);
        changed = seq.swing() != before;
        break;
      }
      case kMenuEvolve: {
        // Off, then the themes.
        int before = seq.evolveTheme();
        seq.setEvolveTheme(before + delta);
        changed = seq.evolveTheme() != before;
        break;
      }
    }
    if (!changed) return false;
    drawMenuItem(menuIndex);
    markDirty();
    return true;
  }

  int lane = cursorRow - 1;
  int before = laneValue(lane, detailTrack, cursorCol);
  setLaneValue(lane, detailTrack, cursorCol, before + delta);
  if (laneValue(lane, detailTrack, cursorCol) == before) return false;
  drawCell(lane, cursorCol);
  drawStripCorner();
  markDirty();
  return true;
}

// --- Menu ---

void drawMenuItem(int item) {
  int y = kMenuY + 4 + item * kMenuItemH;
  bool selected = item == menuIndex;
  // Orange: a value being changed, or an erase waiting to be confirmed.
  bool hot = selected && (editing || (item == kMenuNew && menuConfirm));
  uint16_t bg = selected ? (hot ? TFT_ORANGE : TFT_WHITE) : TFT_BLACK;

  char text[20];
  switch (item) {
    case kMenuTempo:
      snprintf(text, sizeof(text), "TEMPO  %d", seq.tempo());
      break;
    case kMenuSwing:
      snprintf(text, sizeof(text), "SWING  %d%%", seq.swing());
      break;
    case kMenuNew:
      snprintf(text, sizeof(text), hot ? "ARE YOU SURE?" : "NEW");
      break;
    case kMenuEvolve:
      snprintf(text, sizeof(text), "EVOLVE  %s", Sequencer::evolveThemeName(seq.evolveTheme()));
      break;
    case kMenuBluetooth:
      switch (bluetooth::state()) {
        case bluetooth::kOff:
          snprintf(text, sizeof(text), "BLUETOOTH  OFF");
          break;
        case bluetooth::kSearching:
          snprintf(text, sizeof(text), "BT  SEARCHING");
          break;
        case bluetooth::kConnecting:
          snprintf(text, sizeof(text), "BT  CONNECTING");
          break;
        default:
          snprintf(text, sizeof(text), "BT  %.10s", bluetooth::deviceName());
          break;
      }
      break;
    case kMenuExport:
      snprintf(text, sizeof(text), "EXPORT");
      break;
    case kMenuControls:
      snprintf(text, sizeof(text), "HELP");
      break;
    default:
      snprintf(text, sizeof(text), "CLOSE");
      break;
  }

  M5.Display.fillRect(kMenuX + 4, y, kMenuW - 8, kMenuItemH - 2, bg);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(selected ? TFT_BLACK : kValueText, bg);
  M5.Display.drawString(text, kMenuX + 12, y + (kMenuItemH - 2) / 2);
}

void drawMenuBox() {
  M5.Display.fillRect(kMenuX, kMenuY, kMenuW, kMenuH, TFT_BLACK);
  M5.Display.drawRect(kMenuX, kMenuY, kMenuW, kMenuH, TFT_WHITE);
}

// Replaces the menu items with a two-line message; any middle press closes it.
void showMenuMessage(const char* line1, const char* line2) {
  menuMessage = true;
  drawMenuBox();
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(line1, kMenuX + kMenuW / 2, kMenuY + kMenuH / 2 - 14);
  M5.Display.setTextColor(kValueText, TFT_BLACK);
  M5.Display.drawString(line2, kMenuX + kMenuW / 2, kMenuY + kMenuH / 2 + 14);
}

void openMenu() {
  menuOpen = true;
  menuIndex = 0;
  menuConfirm = false;
  menuMessage = false;
  drawMetaField(kFieldMenu);
  drawMenuBox();
  for (int i = 0; i < kMenuItems; ++i) drawMenuItem(i);
}

// Removes the menu and repaints the part of the screen it covered.
void closeMenu() {
  menuOpen = false;
  menuMessage = false;
  editing = false;
  M5.Display.fillRect(kMenuX, kMenuY, kMenuW, kMenuH, TFT_BLACK);
  drawMetaField(kFieldMenu);
  drawGridArea();
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, s == shownStep);
}

// --- Controls page ---

// L, M and R are the left, middle and right buttons.
void openHelp() {
  struct Line {
    const char* keys;  // nullptr marks a heading
    const char* action;
  };
  static const Line kLines[] = {
      {"L / R", "move"},
      {"hold L / R", "up / down"},
      {"L + R", "play / stop"},
      {"M", "step on / off"},
      {"hold M", "open drum"},
      {nullptr, "IN A DRUM"},
      {"M", "edit value"},
      {"hold M", "back"},
      {nullptr, "EDITING / MENU"},
      {"L / R", "change / move"},
      {"M", "done / select"},
  };

  menuOpen = false;
  editing = false;
  helpOpen = true;
  drawMetaField(kFieldMenu);
  M5.Display.fillRect(0, kHelpY, 320, kHelpH, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(top_left);
  int y = kHelpY + 6;
  for (const Line& line : kLines) {
    if (line.keys == nullptr) {
      M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
      M5.Display.drawString(line.action, 8, y);
    } else {
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.drawString(line.keys, 8, y);
      M5.Display.setTextColor(kValueText, TFT_BLACK);
      M5.Display.drawString(line.action, 148, y);
    }
    y += 18;
  }
}

// Removes the controls page and repaints what it covered.
void closeHelp() {
  helpOpen = false;
  M5.Display.fillRect(0, kHelpY, 320, kHelpH, TFT_BLACK);
  drawGridArea();
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, false);
  shownStep = -1;  // the loop lights the current step again
}

void menuMove(int delta) {
  if (menuMessage) return;
  int old = menuIndex;
  menuIndex = (menuIndex + delta + kMenuItems) % kMenuItems;
  menuConfirm = false;  // moving away cancels a pending erase
  drawMenuItem(old);
  drawMenuItem(menuIndex);
}

// Empty pattern, flat automation, three new random sounds, and a random
// tempo (80-170 BPM) and swing (50-66 %). The evolve theme stays as it is.
void newPattern() {
  seq.clearPattern();
  seq.newKit(esp_random());
  voices::familiesForSeed(seq.kitSeed(), families);
  seq.setTempo(80 + esp_random() % 91);
  seq.setSwing(50 + esp_random() % 17);
  markDirty();
}

void exportPattern() {
  showMenuMessage("EXPORTING", "please wait");
  char name[24];
  switch (exporter::exportWav(seq, name, sizeof(name))) {
    case exporter::kOk:
      showMenuMessage("SAVED", name);
      break;
    case exporter::kNoCard:
      showMenuMessage("NO SD CARD", "");
      break;
    default:
      showMenuMessage("WRITE FAILED", "");
      break;
  }
}

void menuSelect() {
  if (menuMessage) {
    closeMenu();
  } else if (menuIndex == kMenuTempo || menuIndex == kMenuSwing || menuIndex == kMenuEvolve) {
    // Start or finish changing the value with A / C.
    editing = !editing;
    drawMenuItem(menuIndex);
  } else if (menuIndex == kMenuNew) {
    // Erasing takes two presses.
    if (menuConfirm) {
      newPattern();
      // Land on the first step of the first drum, ready to write.
      detailTrack = -1;
      cursorRow = 1;
      cursorCol = 0;
      savedGridCol = 0;
      closeMenu();
    } else {
      menuConfirm = true;
      drawMenuItem(kMenuNew);
    }
  } else if (menuIndex == kMenuBluetooth) {
    if (bluetooth::state() == bluetooth::kOff) {
      bluetooth::start(seq);
      drawMenuItem(kMenuBluetooth);
    } else {
      // Bluetooth cannot be shut down cleanly, so turning it off restarts.
      showMenuMessage("RESTARTING", "");
      storage::save(seq);
      delay(300);
      ESP.restart();
    }
  } else if (menuIndex == kMenuExport) {
    exportPattern();
  } else if (menuIndex == kMenuControls) {
    openHelp();
  } else {
    closeMenu();
  }
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
  for (int col = 0; col < kMetaFields; ++col) drawMetaField(col);
  for (int s = 0; s < Sequencer::kSteps; ++s) drawPlayheadMark(s, false);
  drawPlayIcon(false);
  updateBattery();
  drawGridArea();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 0 (the default) leaves Serial off
  M5.begin(cfg);
  M5.BtnA.setHoldThresh(kRowHoldMs);
  M5.BtnC.setHoldThresh(kRowHoldMs);

  // Pick up where the last session left off. With nothing saved, start as
  // NEW PATTERN does.
  if (!storage::load(seq)) {
    seq.init(0);
    newPattern();
  }
  voices::familiesForSeed(seq.kitSeed(), families);

  drawStaticScreen();

  M5.Speaker.setVolume(160);
  audio::begin(seq);
  Serial.printf("m5drums: ready (battery %d%%, charging %d)\n", (int)M5.Power.getBatteryLevel(),
                (int)M5.Power.isCharging());
}

void loop() {
  static uint32_t repeatA = 0, repeatC = 0;
  static bool chordHeld = false;
  static bool lastAdjustChanged = false;
  M5.update();

  // A + C together: play / stop.
  bool aDown = M5.BtnA.wasPressed(), cDown = M5.BtnC.wasPressed();
  if (!chordHeld && ((aDown && M5.BtnC.isPressed()) || (cDown && M5.BtnA.isPressed()))) {
    chordHeld = true;
    // While editing, the first button of the pair already changed the value
    // when it went down; take that back.
    if (editing && !(aDown && cDown) && lastAdjustChanged) adjustValue(aDown ? -1 : +1);
    seq.setPlaying(!seq.playing());
  }

  if (chordHeld) {
    // Swallow everything A and C do until both are up again.
    if (!M5.BtnA.isPressed() && !M5.BtnC.isPressed()) chordHeld = false;
  } else if (helpOpen) {
    // Only the middle button does anything on the controls page.
  } else if (editing) {
    // A / C: decrease / increase, repeating while held
    if (pressedOrRepeating(M5.BtnA, repeatA)) lastAdjustChanged = adjustValue(-1);
    if (pressedOrRepeating(M5.BtnC, repeatC)) lastAdjustChanged = adjustValue(+1);
  } else if (menuOpen) {
    // A / C: up / down the menu
    if (M5.BtnA.wasClicked()) menuMove(-1);
    if (M5.BtnC.wasClicked()) menuMove(+1);
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
    if (helpOpen) {
      closeHelp();
    } else if (menuOpen) {
      menuSelect();
    } else if (editing) {
      setEditing(false);
    } else if (cursorRow == kMetaRow) {
      openMenu();
    } else if (inDetail()) {
      setEditing(true);
    } else {
      seq.toggleStep(cursorRow - 1, cursorCol);
      drawCell(cursorRow - 1, cursorCol);
      markDirty();
    }
  }

  // B hold: close the menu; on a drum, open its detail screen; in a detail
  // screen, go back.
  if (M5.BtnB.wasHold()) {
    if (helpOpen) {
      closeHelp();
    } else if (menuOpen) {
      closeMenu();
    } else if (inDetail()) {
      exitDetail();
    } else if (cursorRow != kMetaRow) {
      enterDetail(cursorRow - 1);
    }
  }

  updatePhrase();

  // Keep the menu's Bluetooth line current while it searches and connects.
  if (bluetooth::state() != shownBluetooth) {
    shownBluetooth = bluetooth::state();
    if (shownBluetooth == bluetooth::kConnected) bluetooth::startStream();
    if (menuOpen && !menuMessage) drawMenuItem(kMenuBluetooth);
  }

  bool playing = seq.playing();
  if (playing != shownPlaying) {
    shownPlaying = playing;
    drawPlayIcon(playing);
  }

  // The controls page covers the playhead, so it is not drawn while that is open.
  int step = seq.currentStep();
  if (!helpOpen && step != shownStep) {
    int old = shownStep;
    shownStep = step;  // drawCell reads this to decide which hits are lit
    if (old >= 0) {
      if (!underMenu(old)) drawPlayheadMark(old, false);
      drawHitsInColumn(old);
    }
    if (step >= 0) {
      if (!underMenu(step)) drawPlayheadMark(step, true);
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
    storage::save(seq);
  }

  delay(2);
}
