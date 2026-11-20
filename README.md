# m5drums

A three-button step-sequencer drum machine for M5Stack cores, with synthesised sounds.

## Minimum Hardware

- M5Stack Gray or Core Basic (v2.7), on USB
- Optional: Module Audio (M144) for a headphone jack (Core Basic only)
- Optional: microSD card, FAT formatted, for WAV export

## Setup

```sh
uv tool install platformio
uv pip install --python ~/.local/share/uv/tools/platformio/bin/python intelhex
```

## Commands

```sh
pio run               # build
pio run -t upload     # build and flash
pio device monitor    # serial log
pio test -e native    # run the tests on this computer (no device needed)
```

Controls are listed under MENU > HELP on the device.
