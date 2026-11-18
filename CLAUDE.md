# M5Stack Gray drum machine

A small, self-contained step-sequencer drum machine running on an M5Stack Gray,
operated only with its three buttons and the built-in display.

## Hardware (verified against docs.m5stack.com/en/core/Gray)

- M5Stack Gray, SKU K002: classic ESP32-D0WDQ6, dual core 240 MHz, 520 KB SRAM, 16 MB flash, no PSRAM
- Display: 2" 320x240 IPS LCD (ILI9342C)
- Buttons: A = GPIO39, B = GPIO38, C = GPIO37
- Speaker: built-in 1 W, driven from GPIO25 (ESP32 DAC1, 8-bit)
- GPIO26 (DAC2) is free on the bottom header / Grove port B
- IMU: MPU6886 (I2C 0x68) + BMM150 (I2C 0x10) on GPIO21/22
- microSD (TF) slot, 16 GB max
- No native USB (serial bridge only), so no USB audio
- Red power button: set at boot to the IP5306's "one short press on/off"
  mode (register 0x01 bit 5; the chip's default is single press on, double
  press off). The button is also wired to the ESP32's reset line, so it
  cannot be read by the firmware. Do not use the long-press mode (bit 7): it
  was tried, and the device then would not turn on from battery with either
  a press or a hold, only from USB.
- Power chip: IP5306 over I2C. Battery level is only reported in quarters
  (25/50/75/100); `M5.Power.getBatteryLevel()` and `isCharging()` both work.

The SD card chip-select is GPIO4 on the display's SPI bus (from M5Unified's
pin table; use `M5.getPin(m5::pin_name_t::sd_spi_cs)`).

## Toolchain

- PlatformIO, Arduino framework
- `platform = espressif32@6.13.0` (Arduino core 2.0.17), `board = m5stack-grey`
  (note the spelling: grey). Keep the platform pinned: unpinned, PlatformIO
  reused an old 5.1.1 (2022) already on this machine, and with its Bluetooth
  stack A2DP audio was only sent at about 30 % of real time.
- PlatformIO is installed with `uv tool install platformio`; the 6.x platform
  also needs `intelhex` in that environment
  (`uv pip install --python ~/.local/share/uv/tools/platformio/bin/python intelhex`).
- Library: `m5stack/M5Unified` (pulls in M5GFX). Do not use the older M5Stack library.
- Bluetooth audio: `pschatzmann/ESP32-A2DP` pinned to v1.7.4 (no extra
  dependencies). It takes the firmware to about 1.4 MB, hence
  `board_build.partitions = default_16MB.csv`.
- Build: `pio run`. Flash: `pio run -t upload`. Logs: `pio device monitor`.
- Tests: `pio test -e native`. They build voices.cpp, sequencer.cpp and
  storage.cpp for this computer (no device needed) and take a few seconds.
  Suites are in `test/test_*`; `test/support` has shared helpers and an
  in-memory stand-in for the ESP32's `Preferences.h`. Run them after any
  change to those three files. Anything that touches hardware (buttons,
  display, audio outputs, Bluetooth, SD export) is only checked on the device.

## Design

- 8 steps (16th notes, so half a bar) x 3 parts: A, B and C
- Sounds are synthesised in code (virtual analog), not sample playback:
  - the aim is analog drum machine character, not FM or metallic timbres
  - instruments in voices.cpp, each a different recipe. In the draw: PEW
    (sine with a falling pitch), NOISE (filtered noise over an optional
    tone), BLIP (square or triangle with a small bend), KICK (low sine, fast
    drop, click, drive), CLAP (band-passed noise in a few bursts), TOM (round
    sine with a gentle sag and warmth), SNARE (tuned body plus snappy noise),
    RIM (very short high tick). Implemented but left out of the draw: METAL
    (FM) and COWBELL (two detuned squares), both judged too metallic, and
    PLUCK (Karplus-Strong string) and ACID (resonant saw), held back until
    there is a plan for pitched sounds.
  - a kit is three different instruments from the draw, each with random
    settings, generated from a seed
  - analog feel: each hit's pitch wanders about 0.5 % and its level a few
    percent at random, and the mix is soft-clipped rather than hard-limited
  - no backwards compatibility for saved sessions: when the session layout
    or what a seed produces changes, bump kVersion in storage.cpp and old
    saves are simply ignored (the device starts fresh)
  - the worst-case kit renders 1 s of audio in about 190 ms on the device
  - sines come from a lookup table, not sinf()
- Screen shows a top row (MENU button, battery icon, play state), then
  a 3-row x 8-column grid, a cursor, and a moving playhead
- Controls (A, B, C are the left, middle and right buttons):
  - A / C tapped: cursor left / right within the row, wrapping. On the grid
    they act on release and do nothing while held, because a held button is
    the shift for the other. In the menu (up / down) and while editing a
    value (lower / raise) they act on press and repeat while held.
  - A held, then C pressed and let go: down a row. C held, then A pressed
    and let go: up a row. Wrapping (MENU, then the three drums or lanes).
    The row changes when either button is released.
  - A and C both held for 0.45 s: play/stop, with no cursor movement.
  - In the menu and while editing, a press of A or C waits 45 ms before
    acting, so that a pair pressed together (play/stop) is recognised first.
  - B tapped: "enter". On a step, toggle it; on MENU, open the menu; in a
    drum's detail screen, start or finish editing the value; in the menu,
    select.
  - B held on its own for 0.4 s (fires while still held): on a drum's row,
    open that drum's detail screen, where the three rows are that
    instrument's lanes; otherwise go back (out of the detail screen, menu or
    help)
  - Other arrangements tried along the way: one button as both enter and
    shift (on A, on B, and on C); A as a row-shift for B and C.
- Automation: every step stores three offsets, shown as bars in the detail
  screen. Pitch (+/-18 semitones; on NOISE it is brightness), decay (+/-12,
  4 = double/half length), and a third "shape" lane (+/-8, 4 = double/half)
  that is the instrument's own: PEW sweep depth, NOISE tone body, METAL FM
  amount, BLIP bend depth, KICK drive, CLAP spread between bursts, PLUCK
  brightness, COWBELL detune, ACID filter envelope depth, TOM pitch sag,
  SNARE snap (noise level), RIM click (noise level).
- MENU (top left, the only cell of the top row): B opens a drop-down list.
  A/C move up/down, B selects, B long press closes.
  - TEMPO (40-240 BPM) and SWING (50-75 %, MPC style: 50 is straight): B
    enters edit mode, A/C change the value, B leaves
  - NEW (select twice): empty pattern with three new random sounds
    and a random tempo (80-170 BPM) and swing (50-66 %). Evolve goes to off.
  - EVOLVE (off, or a named theme): over a fixed 8-bar phrase the pattern
    moves away from what is set and is home again when the phrase restarts.
    The first pass is always exactly as set and stored values are never
    changed. Themes are a table in sequencer.cpp: a 16-pass curve, how far
    each lane moves, whether hits move together or each in its own
    direction, volume spread, and chances of dropped hits, extra ghost hits
    and rolls, plus a sample-rate crush on the output. Themes: DRIFT, BUILD,
    WAVE, RISE, OPEN, FILL, GLITCH, MELT. There is no depth or seed to set;
    every random-looking choice is a hash of the kit seed, pass and step,
    so a phrase is identical every time and in exports. A row of 16
    segments in the top row shows the position in the phrase.
  - BLUETOOTH: turns on A2DP and connects to the first audio
    device found; while connected, the Bluetooth stack pulls audio from the
    sequencer and the built-in speaker goes quiet. Selecting it again saves
    and restarts, which is the only way to turn it off.
  - HELP: a page listing what the buttons do. Nothing about the controls
    is shown on screen otherwise.
  - EXPORT: renders one evolve phrase (4 repeats of the pattern if
    evolve is off) plus the ring-out, offline,
    to /drumsNNN.wav on the microSD card (16-bit mono, 44.1 kHz).
- The session (pattern, automation, tempo, swing, and the seed the sounds are
  generated from) is saved to NVS 2 s after the last change and restored at
  boot. Changing the random ranges in voices.cpp changes what a saved seed
  sounds like.

## Constraints

- Audio is rendered as 16-bit mono into small buffers in its own FreeRTOS task.
  Nothing in the audio path may block, allocate, or touch the display.
- Sequencer timing is counted in samples, not millis(), so it cannot drift
  against the audio.
- Redraw only the grid cells that changed. Full-screen redraws cause audible glitches.
- Keep the voice code independent of the output so the same buffers can go to
  the speaker and to a WAV file.

## Milestones

Build and flash after each one. Do not start the next until the current one
works on the device.

1. Hello: project builds, screen shows text, each button plays a beep
2. Voices: A/B/C trigger the three voices directly
3. Sequencer: grid UI, cursor, toggle steps, play/stop, playhead
4. Tempo and per-track volume controls
5. Recording: write the output to a 16-bit WAV file on the microSD card
6. Extras: IMU control (tilt for tempo, shake for a fill), pitch per step,
   pattern save/load

## Known limits

- The built-in speaker has almost no low end and the 8-bit DAC path hisses.
  That is expected. Clean output comes from the WAV recording in milestone 5.
