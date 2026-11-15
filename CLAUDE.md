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

## Design

- 8 steps (16th notes, so half a bar) x 3 parts: A, B and C
- Sounds are synthesised in code (virtual analog), not sample playback:
  - one voice engine (oscillator with pitch sweep + FM + filtered noise),
    used in different proportions by four instruments: PEW (sine with a
    falling pitch), NOISE (filtered noise over an optional tone), METAL (FM)
    and BLIP (square or triangle with a small bend)
  - a kit is three different instruments out of the four, each with random
    settings. The worst-case kit renders 1 s of audio in about 140 ms.
  - sines come from a lookup table, not sinf()
- Screen shows a top row (MENU button, battery icon, play state), then
  a 3-row x 8-column grid, a cursor, and a moving playhead
- Controls:
  - A / C short press: cursor left / right within the row, wrapping
  - A / C long press: cursor up / down a row, wrapping
  - A + C together: play/stop
  - B short press: on a step, toggle it; on MENU, open the menu
  - B long press on a drum's row: open that drum's detail screen, where the
    three rows are that instrument's lanes. B long press again: back.
  - In the detail screen, B short press on a step edits that value
  - Edit mode: A/C decrease/increase (hold to repeat), B short press leaves
- Automation: every step stores three offsets, shown as bars in the detail
  screen. Pitch (+/-18 semitones; on NOISE it is brightness), decay (+/-12,
  4 = double/half length), and a third "shape" lane (+/-8, 4 = double/half)
  that is the instrument's own: PEW sweep depth, NOISE tone body, METAL FM
  amount, BLIP bend depth.
- MENU (top left, the only cell of the top row): B opens a drop-down list.
  A/C move up/down, B selects, B long press closes.
  - TEMPO (40-240 BPM) and SWING (50-75 %, MPC style: 50 is straight): B
    enters edit mode, A/C change the value, B leaves
  - NEW (select twice): empty pattern with three new random sounds
    and a random tempo (80-170 BPM) and swing (50-66 %).
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
  - CONTROLS: a page listing what the buttons do. Nothing about the controls
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
