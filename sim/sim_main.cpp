// The screens, run on a computer instead of the device: the real src/ui.cpp,
// the real sequencer and the real display library, with a made-up battery,
// Bluetooth and SD card behind them. For trying out layouts without flashing.
//
//   pio run -e sim                        build (and every picture again)
//   .pio/build/sim/program                a window, with sound; keys below
//   .pio/build/sim/program shots [dir]    every picture in kScenes, as PNGs
//
// Keys in the window: left, down, right are the three buttons, and can be
// held and combined as on the device. B battery, T Bluetooth (the next
// state), E what the next export does, S saves a picture.
//
// What it cannot show: the real panel's colours and brightness, how the
// buttons feel, how the device sounds (the computer plays the clean mix, not
// the built-in speaker), and anything the real Bluetooth or card code does.
#include <M5Unified.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "bluetooth.h"
#include "exporter.h"
#include "storage.h"
#include "ui.h"

sim::Device M5;
SimEsp ESP;

namespace sim {

uint32_t clockMs = 100000;

// ---- The made-up world behind the screens ----

Sequencer seq;
bool restored = true;  // false: start as the device does with nothing saved

bluetooth::State bluetoothState = bluetooth::kOff;
uint32_t bluetoothSince = 0;
exporter::Result exportResult = exporter::kOk;
bool formatWorks = true;     // what formatting the made-up card comes to
bool stopAtFormat = false;   // take the scene's picture while the card is being formatted
void pictureAndLeave();
int exports = 6;  // files already on the made-up card

uint32_t randomState = 0x2545F491u;
uint32_t random32() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

// The device restarts to turn Bluetooth off. Here the screen is left as it
// was at that moment.
void restart() {}

constexpr int kFrameMs = 20;
constexpr int kFrameSamples = voices::kSampleRate * kFrameMs / 1000;
int16_t frameAudio[kFrameSamples];

// One frame: time passes, the sequencer plays that much (there is no audio
// task here, so this is what moves the playhead), and the screens catch up.
void frame() {
  clockMs += kFrameMs;
  // A speaker is found after 2 s and connected 1.5 s later.
  if (bluetoothState == bluetooth::kSearching && clockMs - bluetoothSince > 2000) {
    bluetoothState = bluetooth::kConnecting;
  } else if (bluetoothState == bluetooth::kConnecting && clockMs - bluetoothSince > 3500) {
    bluetoothState = bluetooth::kConnected;
  }
  seq.render(frameAudio, kFrameSamples);
  ui::update();
}

void frames(int count) {
  for (int i = 0; i < count; ++i) frame();
}

}  // namespace sim

// ---- What the screens ask of the rest of the firmware ----

namespace storage {
bool load(Sequencer&) { return false; }
void save(const Sequencer&) {}
}  // namespace storage

namespace bluetooth {
void start(Sequencer&) {
  sim::bluetoothState = kSearching;
  sim::bluetoothSince = sim::clockMs;
}
State state() { return sim::bluetoothState; }
const char* deviceName() { return sim::bluetoothState >= kConnecting ? "JBL Flip 4" : ""; }
void startStream() {}
}  // namespace bluetooth

namespace exporter {
Result exportWav(const Sequencer&, char* name, size_t nameSize) {
  if (sim::exportResult == kOk) snprintf(name, nameSize, "drums%03d.wav", ++sim::exports);
  return sim::exportResult;
}
bool formatCard() {
  // On the device nothing moves while this runs. The picture of that moment
  // has to be taken from in here.
  if (sim::stopAtFormat) sim::pictureAndLeave();
  if (sim::formatWorks) sim::exportResult = kOk;
  return sim::formatWorks;
}
}  // namespace exporter

namespace sim {

bool savePicture(const std::string& path) {
  size_t length = 0;
  void* png = M5.Display.createPng(&length, 0, 0, 320, 240);
  if (!png) return false;
  FILE* file = fopen(path.c_str(), "wb");
  bool ok = file && fwrite(png, 1, length, file) == length;
  if (file) fclose(file);
  free(png);
  return ok;
}

// ---- Button presses, timed as a person's are ----

void tap(Button& button) {
  button.down = true;
  frames(4);
  button.down = false;
  frames(3);
}

void hold(Button& button, int ms) {
  button.down = true;
  frames(ms / kFrameMs);
  button.down = false;
  frames(3);
}

// Holding one outer button and tapping the other: a row down or up.
void rowPress(Button& held, Button& tapped) {
  held.down = true;
  frames(5);
  tap(tapped);
  held.down = false;
  frames(3);
}

void press(char key) {
  switch (key) {
    case 'a': tap(M5.BtnA); break;
    case 'b': tap(M5.BtnB); break;
    case 'c': tap(M5.BtnC); break;
    case 'A': hold(M5.BtnA, 1000); break;  // held: repeats in the menu and on a value
    case 'C': hold(M5.BtnC, 1000); break;
    case 'h': hold(M5.BtnB, 600); break;  // open drum / back
    case 'd': rowPress(M5.BtnA, M5.BtnC); break;
    case 'u': rowPress(M5.BtnC, M5.BtnA); break;
    case 'p':  // both outer buttons held: play / stop
      M5.BtnA.down = true;
      frames(2);
      M5.BtnC.down = true;
      frames(30);
      M5.BtnA.down = M5.BtnC.down = false;
      frames(3);
      break;
    case 'w': frames(1000 / kFrameMs); break;
    case '.': frames(100 / kFrameMs); break;
    default:
      fprintf(stderr, "no such key in a scene: %c\n", key);
      exit(2);
  }
}

// ---- Pictures: a starting state, then button presses ----

// What every scene starts from: a saved session with a pattern in it.
void basePattern() {
  seq.init(20261121);
  seq.setTempo(120);
  seq.setPattern(0, 0b00010001);
  seq.setPattern(1, 0b01000100);
  seq.setPattern(2, 0b10101010);
  seq.setStepPitch(0, 4, 7);
  seq.setStepPitch(0, 0, -4);
  seq.setStepDecay(0, 4, -6);
  seq.setStepShape(0, 0, 5);
  seq.setStepPitch(1, 2, 12);
  seq.setStepDecay(1, 6, 9);
  seq.setStepPitch(2, 3, -9);
  seq.setStepDecay(2, 7, 6);
}

struct Scene {
  const char* name;
  void (*arrange)();    // changes to the made-up world, or nullptr
  const char* presses;  // a, b, c: tap left, middle, right; A, C: hold left, right for 1 s;
                        // h: hold middle (open drum / back); d, u: a row down, up;
                        // p: left and right held (play / stop); w: wait 1 s; .: wait 0.1 s
};

// Every scene starts on the first step of drum A. To the menu from there: up
// a row to the MENU button, then enter. Its rows: TEMPO, SWING, EVOLVE,
// BLUETOOTH, EXPORT, NEW, HELP.
#define MENU "ub"
#define EXPORT MENU "ccccb"

const Scene kScenes[] = {
    {"main", nullptr, ""},
    {"main-nothing-saved", [] { restored = false; }, ""},
    {"main-cursor-moved", nullptr, "ccc"},
    {"main-step-added", nullptr, "cb"},
    {"main-step-removed", nullptr, "b"},
    {"main-on-drum-b", nullptr, "d"},
    {"main-on-drum-c", nullptr, "dd"},
    {"main-on-menu-button", nullptr, "u"},
    {"main-playing", nullptr, "pw"},
    {"main-playing-further", nullptr, "pw.."},
    {"main-stopped-again", nullptr, "pwp"},
    {"main-evolve-playing", [] { seq.setEvolveTheme(1); }, "pwwwww"},
    {"main-battery-full", [] { M5.Power.level = 100; }, ""},
    {"main-battery-low", [] { M5.Power.level = 25; }, ""},
    {"main-battery-empty", [] { M5.Power.level = 0; }, ""},
    {"main-charging", [] { M5.Power.level = 50; M5.Power.charging = true; }, ""},
    {"main-no-battery-reading", [] { M5.Power.level = -1; }, ""},
    {"main-after-new", nullptr, MENU "cccccbb"},
    {"drum", nullptr, "h"},
    {"drum-b", nullptr, "dh"},
    {"drum-c", nullptr, "ddh"},
    {"drum-on-decay", nullptr, "hd"},
    {"drum-on-shape", nullptr, "hdd"},
    {"drum-cursor-moved", nullptr, "hcccc"},
    {"drum-editing", nullptr, "hb"},
    {"drum-editing-raised", nullptr, "hccccbC"},
    {"drum-editing-lowered", nullptr, "hccccbA"},
    {"drum-edited", nullptr, "hccccbCb"},
    {"drum-playing", nullptr, "hpw"},
    {"drum-playing-further", nullptr, "hpw.."},
    {"drum-on-menu-button", nullptr, "hu"},
    {"drum-back-out", nullptr, "dhh"},
    {"menu", nullptr, MENU},
    {"menu-on-swing", nullptr, MENU "c"},
    {"menu-tempo-picked", nullptr, MENU "b"},
    {"menu-tempo-raised", nullptr, MENU "bC"},
    {"menu-tempo-done", nullptr, MENU "bCb"},
    {"menu-swing-picked", nullptr, MENU "cbccc"},
    {"menu-evolve-picked", nullptr, MENU "ccbccc"},
    {"menu-bluetooth-searching", nullptr, MENU "cccb"},
    {"menu-bluetooth-connecting", nullptr, MENU "cccbww."},
    {"menu-bluetooth-connected", nullptr, MENU "cccbwwww"},
    {"menu-bluetooth-restarting", nullptr, MENU "cccbwwwwb"},
    {"menu-export-saved", nullptr, EXPORT},
    {"menu-export-no-card", [] { exportResult = exporter::kNoCard; }, EXPORT},
    {"menu-export-failed", [] { exportResult = exporter::kWriteFailed; }, EXPORT},
    {"menu-export-moved-away", nullptr, EXPORT "c"},
    // A card that cannot be read: the export asks whether to format it.
    {"menu-card-question", [] { exportResult = exporter::kUnreadable; }, EXPORT},
    {"menu-card-question-too-soon", [] { exportResult = exporter::kUnreadable; }, EXPORT "c"},
    {"menu-card-question-no", [] { exportResult = exporter::kUnreadable; }, EXPORT "wa"},
    {"menu-card-question-put-away", [] { exportResult = exporter::kUnreadable; }, EXPORT "wb"},
    {"menu-card-formatting", [] { exportResult = exporter::kUnreadable; stopAtFormat = true; }, EXPORT "wc"},
    {"menu-card-formatted-and-exported", [] { exportResult = exporter::kUnreadable; }, EXPORT "wc"},
    {"menu-card-could-not-format", [] { exportResult = exporter::kUnreadable; formatWorks = false; }, EXPORT "wc"},
    {"menu-new-are-you-sure", nullptr, MENU "cccccb"},
    {"menu-new-moved-away", nullptr, MENU "cccccba"},
    {"menu-help", nullptr, MENU "ab"},
    {"menu-help-closed", nullptr, MENU "abb"},
    {"menu-closed", nullptr, MENU "cch"},
    {"menu-closed-from-a-picked-value", nullptr, MENU "bhh"},
    {"menu-playing", [] { seq.setEvolveTheme(3); }, "p" MENU "ww"},
    {"menu-closed-while-playing", nullptr, "p" MENU "wh.."},
    {"menu-over-a-drum", nullptr, "hub"},
    {"menu-closed-over-a-drum", nullptr, "hubh"},
};

std::string picturePath;

void pictureAndLeave() {
  bool ok = savePicture(picturePath);
  printf("%s %s\n", ok ? "saved" : "FAILED", picturePath.c_str());
  fflush(stdout);
  _exit(ok ? 0 : 1);
}

int shoot(const Scene& scene, const std::string& folder) {
  picturePath = folder + "/" + scene.name + ".png";
  basePattern();
  if (scene.arrange) scene.arrange();
  ui::begin(seq, restored);
  frames(10);
  for (const char* key = scene.presses; *key; ++key) press(*key);
  // SIM_FRAMES=n: n more frames (of 20 ms) before the picture, to catch something moving.
  frames(getenv("SIM_FRAMES") ? atoi(getenv("SIM_FRAMES")) : 0);
  std::string path = folder + "/" + scene.name + ".png";
  bool ok = savePicture(path);
  printf("%s %s\n", ok ? "saved" : "FAILED", path.c_str());
  return ok ? 0 : 1;
}

// ---- The window ----

int live(bool* running) {
  basePattern();
  ui::begin(seq, restored);

  // Sound, and the pace: a frame is made whenever less than three are
  // waiting to be played, so the screen keeps time with what is heard.
  SDL_AudioDeviceID speaker = 0;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
    SDL_AudioSpec want = {};
    want.freq = voices::kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    speaker = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (speaker) SDL_PauseAudioDevice(speaker, 0);
  }
  if (!speaker) printf("no sound: %s\n", SDL_GetError());

  const uint8_t* keys = SDL_GetKeyboardState(nullptr);
  static const SDL_Scancode kWatched[] = {SDL_SCANCODE_B, SDL_SCANCODE_T, SDL_SCANCODE_E, SDL_SCANCODE_S};
  bool was[sizeof(kWatched) / sizeof(kWatched[0])] = {};
  int pictures = 0;
  while (*running) {
    // The three buttons follow their keys, held for as long as the key is.
    M5.BtnA.down = keys[SDL_SCANCODE_LEFT];
    M5.BtnB.down = keys[SDL_SCANCODE_DOWN];
    M5.BtnC.down = keys[SDL_SCANCODE_RIGHT];
    for (size_t i = 0; i < sizeof(was); ++i) {
      bool down = keys[kWatched[i]];
      bool pressed = down && !was[i];
      was[i] = down;
      if (!pressed) continue;
      switch (kWatched[i]) {
        case SDL_SCANCODE_B:  // 75, 50, 25, 0, 100 and charging, 100
          if (M5.Power.charging) {
            M5.Power.charging = false;
          } else if (M5.Power.level > 0) {
            M5.Power.level -= 25;
          } else {
            M5.Power.level = 100;
            M5.Power.charging = true;
          }
          clockMs += 5000;  // the battery is only looked at every 5 s
          break;
        case SDL_SCANCODE_T:
          bluetoothState = (bluetooth::State)((bluetoothState + 1) % 4);
          bluetoothSince = clockMs + 3600000;  // stays there until T is pressed again
          break;
        case SDL_SCANCODE_E:
          exportResult = (exporter::Result)((exportResult + 1) % 4);
          printf("the next export: %s\n", exportResult == exporter::kOk ? "works"
                                           : exportResult == exporter::kNoCard ? "no card"
                                           : exportResult == exporter::kUnreadable ? "the card cannot be read" : "write fails");
          break;
        case SDL_SCANCODE_S: {
          char name[40];
          snprintf(name, sizeof(name), "sim-picture-%d.png", ++pictures);
          printf("%s %s\n", savePicture(name) ? "saved" : "FAILED", name);
          break;
        }
        default: break;
      }
    }
    if (speaker) {
      if (SDL_GetQueuedAudioSize(speaker) < 3 * sizeof(frameAudio)) {
        frame();
        SDL_QueueAudio(speaker, frameAudio, sizeof(frameAudio));
      } else {
        SDL_Delay(2);
      }
    } else {
      frame();
      SDL_Delay(kFrameMs);
    }
  }
  return 0;
}

std::string folder = "sim/shots";
const Scene* scene = nullptr;

int run(bool* running) {
  M5.Display.init();
  if (!scene) return live(running);
  int result = shoot(*scene, folder);
  fflush(stdout);
  // One picture per run: the screens keep their state in statics. Straight
  // out, without the usual tidying up: that runs while the window's own
  // thread is still going (it now and then crashed in sketchbox-m5, where
  // this comes from).
  _exit(result);
}

}  // namespace sim

int main(int argc, char** argv) {
  if (argc >= 2 && strcmp(argv[1], "shots") == 0) {
    // Every scene, each in a run of its own, so that each starts afresh.
    std::string folder = argc >= 3 ? argv[2] : sim::folder;
    mkdir(folder.c_str(), 0755);
    int failed = 0;
    for (const sim::Scene& scene : sim::kScenes) {
      std::string command = std::string("'") + argv[0] + "' shot " + scene.name + " '" + folder + "'";
      failed += system(command.c_str()) != 0;
    }
    return failed;
  }
  if (argc >= 3 && strcmp(argv[1], "shot") == 0) {
    for (const sim::Scene& scene : sim::kScenes) {
      if (strcmp(scene.name, argv[2]) == 0) sim::scene = &scene;
    }
    if (!sim::scene) {
      fprintf(stderr, "no scene called %s\n", argv[2]);
      return 2;
    }
    if (argc >= 4) sim::folder = argv[3];
    setenv("SDL_VIDEODRIVER", "dummy", 1);  // no window for a picture
  }
  return lgfx::Panel_sdl::main(sim::run, 0);
}
