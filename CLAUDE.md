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

Not verified, check before relying on it: the SD card chip-select pin, and
the exact M5Unified speaker API (read the library's own examples).

## Toolchain

- PlatformIO, Arduino framework
- `platform = espressif32`, `board = m5stack-grey` (note the spelling: grey)
- Library: `m5stack/M5Unified` (pulls in M5GFX). Do not use the older M5Stack library.
- Build: `pio run`. Flash: `pio run -t upload`. Logs: `pio device monitor`.

## Design

- 8 steps (16th notes, so half a bar) x 3 parts: A, B and C, all synth toms
- Sounds are synthesised in code (virtual analog), not sample playback:
  - all three are the same voice at different, completely random tunings: a
    sine with a downward pitch sweep ("pew") plus amplitude decay
- Screen shows a settings ("meta") row on top, then a 3-row x 8-column grid,
  a cursor, and a moving playhead
- Controls:
  - A / C short press: cursor left / right within the row, wrapping
  - A / C long press: cursor up / down a row (settings, A, B, C), wrapping
  - B short press: on a step, toggle it; on a setting, enter edit mode
  - Edit mode: A/C decrease/increase (hold to repeat), B short press leaves
  - B long press: play/stop
  - A + C together: next grid view (TRIG, PITCH, DECAY), shown as tabs above the grid
- Settings row: tempo (40-240 BPM) and swing (50-75 %, MPC style: 50 is straight)
- Automation: every step stores a pitch offset (+/-12 semitones) and a decay
  offset (+/-8 levels, 4 levels = double/half length). The PITCH and DECAY
  views show them as bars; B on a step enters edit mode for that value.
- NEW (top left, first cell of the settings row): B twice starts an empty
  pattern with three new random sounds. Tempo and swing are kept.
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
