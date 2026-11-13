# Licensing

ObsSynth FM-1 is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text
in `LICENSE`). It is a work based on:

- **Felucca**, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments — <https://github.com/hugelton/Felucca>
  (the HAL, the boot, the update loader, the LCD driver, the canvas, the tools);
- **Hortator**, the drum-machine fork of Felucca, 2026 DEADACTIVE (the knobs' decoder after Felucca 1.1.5, the
  pixel fonts `assets/fonts/md_font.txt`, the web simulator's structure, the build).

Each file keeps its `SPDX-License-Identifier` and the copyright lines of the code it carries. If you distribute
this firmware, or firmware derived from it, you must give your recipients its complete corresponding source under
the same licence.

None of Felucca's assets (the panel photograph, the Hügelton Sample Pack), which are not under the GPL, are in this
tree or in the firmware.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| jl-uboot-tool by Andrey Grigoryev (kagaimiq): the boot-mode cipher and command framing in the recovery tool | MIT | `tools/rescue/fm1_uboot.py`; `LICENSES/MIT-jl-uboot-tool.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`, read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>; `LICENSES/Apache-2.0.txt` |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of
their respective owners. ObsSynth FM-1 is independent firmware that runs on FM-1 hardware; it is not affiliated
with, endorsed by or supported by those owners.

## Radio

ObsSynth FM-1 never enables the Bluetooth / Wi-Fi radio of the hardware.
