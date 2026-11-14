// Milestone 1: hello. Screen shows text, each button plays a beep.
#include <M5Unified.h>

static void showStatus(const char* text) {
  // Redraw only the status line, not the whole screen.
  M5.Display.fillRect(0, 120, M5.Display.width(), 40, TFT_BLACK);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.drawString(text, 20, 124);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // 0 (the default) leaves Serial off
  M5.begin(cfg);

  M5.Display.setTextSize(3);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("M5 Drums", 20, 30);
  M5.Display.setTextSize(2);
  M5.Display.drawString("Press A / B / C to beep", 20, 80);
  M5.Display.drawString("A", 60, 210);
  M5.Display.drawString("B", 154, 210);
  M5.Display.drawString("C", 248, 210);

  M5.Speaker.setVolume(128);
  Serial.println("m5drums: milestone 1 ready");
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) {
    M5.Speaker.tone(440, 120);
    showStatus("A: 440 Hz");
    Serial.println("A");
  }
  if (M5.BtnB.wasPressed()) {
    M5.Speaker.tone(660, 120);
    showStatus("B: 660 Hz");
    Serial.println("B");
  }
  if (M5.BtnC.wasPressed()) {
    M5.Speaker.tone(880, 120);
    showStatus("C: 880 Hz");
    Serial.println("C");
  }
  delay(5);
}
