# XMAS_2026

Christmas 2026 persistence-of-vision display.

## Contents

- `PCB/` — KiCad project `XMAS_2026` (schematic and board)
- `Firmware/` — wand firmware. The current program is the RP2350 C firmware in `Firmware/rp2350/`

## Origin

Copied from [PoV_display_PoC](https://github.com/crikeyk/PoV_display_PoC) at `458e425`, then the KiCad project was renamed to `XMAS_2026`.

## Libraries

The handmade parts are in the private [kicad-library](https://github.com/crikeyk/kicad-library) repo: the `TLV74311PDBVR` symbol and the `BC-8200 AAA Battery clip` footprint. Clone it next to this project (`Documents/Code/kicad-library`) so `PCB/sym-lib-table` and `PCB/fp-lib-table` resolve `${KIPRJMOD}/../../kicad-library`.

The other symbols and footprints are the [JLCPCB KiCad library](https://github.com/CDFER/JLCPCB-Kicad-Library), installed through KiCad's Plugin and Content Manager. Use the 2026.08.08 package. The newer 2026.09.21 package is missing the generated parts.
