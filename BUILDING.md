# Building ObsSynth for the FM-1

The build makes, in `build/`:

| File | What |
| --- | --- |
| `synth.bin` | the firmware app |
| `loader/ota.bin` | the update loader (pinned: `tools/frozen_base.txt`; the build stops if it differs) |
| `obssynth-fm1-VERSION.fwsc` | the release package, with `--release VERSION` (VERSION.txt's; what the `v*` tag workflow runs) |
| `synth-UNTESTED.fwsc` | a package for trying a working tree, with `SYNTH_PACKAGE=1` |

**Not tested on hardware yet.** The device build runs and passes its image checks (JieLi toolchain 20260730.1, AC79
SDK V1.2.1), but the firmware has never booted on an FM-1; the C also compiles for a host target and the simulator
runs it (`tests/run_tests.sh`). The app carries Felucca's USB driver and
M-UPGRADE update entry (`usb.c`, `ota.c`, the loader's own), so an installed build stays updatable over USB-MIDI
(`tools/fm1_install.py`, M-VAVE's updater). If it ever does not start, the way back is UBOOT (OCT- + OCT+ held 5 s,
or the boot-loop guard after two crashes in the first 30 s) and [docs/RECOVERY.md](docs/RECOVERY.md).

## Prerequisites

- Python 3.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server), Linux x86-64 only; on macOS or
  Windows each tool runs in a `linux/amd64` `debian:bookworm-slim` Docker container (on Windows: WSL2 runs it
  natively):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files (`cpu/wl82/tools/uboot.boot`,
  `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

## Build

```
./build.sh                       # the app and the loader, with the image checks
./build.sh --release 0.1.0       # + build/obssynth-fm1-0.1.0.fwsc (the version must be VERSION.txt's)
SYNTH_PACKAGE=1 ./build.sh       # + build/synth-UNTESTED.fwsc
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations. The checks: the entry stub at the app's XIP
address, no RAM code calling into flash, no reference to the chip ROM, the image within the app slot, RAM and pool
headroom, and every hardware register access inside `firmware/hal/`.

## Tests (no hardware, no toolchain)

```
tests/run_tests.sh
```

The simulator's core (`tests/sim_test.c`), the HAL's key debounce (`tests/input_test.c`) and, with node and a built
wasm, the page's glue (`tests/sim_glue.mjs`).

## Install (when it is time)

```
SYNTH_PACKAGE=1 ./build.sh                     # build/synth-UNTESTED.fwsc
tests/run_tests.sh                             # .. its update path: ota_test, ldr_test
python3 tools/make_installer.py                # build/installer/: Hortator's web installer with this package
python3 -m http.server -d build/installer 8001 # then http://localhost:8001/ in Chrome or Edge (WebMIDI + SysEx)
```

The FM-1 on a USB data cable straight into the computer, charged. The installer talks to the running firmware's
update entry (stock, Felucca, Hortator or this one), which stages the pinned loader; the loader writes the package.
`tools/fm1_install.py PACKAGE` does the same from a terminal (needs `mido` and `python-rtmidi`). Rehearse the way back
first: M-VAVE's updater (M-UPGRADE) with the stock firmware, as in Hortator's steps.

## Identity and releases

The package identity is **FM-1_917** (`tools/build.py` PRODUCT, `firmware/src/synth.c` FELUCCA_ID), ObsSynth's own:
it was free on The Firmware Hub when chosen. The update loader keeps Felucca's USB name (`ota-FM-1_900`): it is
Felucca's pinned loader. The app accepts both FM-1 flash chips (JEDEC 0x856014 Puya P25Q80, 0xB36014 UC25HQ80).

A release is a tag `vVERSION` with VERSION.txt at VERSION: `.github/workflows/release.yml` calls
[zvenson/fm1-build](https://github.com/zvenson/fm1-build) `@v1`, which installs the toolchain and the SDK on GitHub's
Ubuntu runner, runs `./build.sh --release VERSION` and `tests/run_tests.sh`, attests the package and attaches it to
the release.
