# XMAS_2026

Christmas 2026 persistence-of-vision display.

## Contents

- `PCB/` — KiCad project `XMAS_2026` (schematic and board)
- `Firmware/` — wand firmware. The current program is the RP2350 C firmware in `Firmware/rp2350/`

## Origin

Copied from [PoV_display_PoC](https://github.com/crikeyk/PoV_display_PoC) at `458e425`, then the KiCad project was renamed to `XMAS_2026`.

## Libraries

Custom symbols and footprints are in the private [kicad-library](https://github.com/crikeyk/kicad-library) repo. Clone it next to this project (`Documents/Code/kicad-library`) so `PCB/sym-lib-table` and `PCB/fp-lib-table` resolve `${KIPRJMOD}/../../kicad-library`.
