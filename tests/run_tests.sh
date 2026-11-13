#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ObsSynth FM-1: 2026 Orian Beltrame
# The host tests (no hardware, no JieLi toolchain): the simulator's core (tools/build_sim.sh --host-only), the HAL's
# key debounce, and with node the page's glue on the built wasm (tools/build_sim.sh first).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/host
tools/build_sim.sh --host-only
$CC -O2 -Wall -Ifirmware/src -o build/host/input_test tests/input_test.c
build/host/input_test | tail -1
$CC -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/led_dim_test tests/led_dim_test.c   # (the dim LEDs)
build/host/led_dim_test | tail -1
$CC -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/enc_test tests/enc_test.c           # (the knobs)
build/host/enc_test | tail -1
if command -v node >/dev/null && [ -f build/sim/fm1sim.wasm ]; then
    node tests/sim_glue.mjs | tail -1
else
    echo "sim_glue: skipped (needs node and build/sim/fm1sim.wasm: tools/build_sim.sh)"
fi
# the update path on a built package (./build.sh --release VERSION or SYNTH_PACKAGE=1 ./build.sh; AC79_SDK for the
# "other app" package): the M-UPGRADE entry (ota.c) stages it, the pinned loader installs it over another app
PKG=${PKG:-$(ls build/obssynth-fm1-*.fwsc build/synth-UNTESTED.fwsc 2>/dev/null | head -1)}
if [ -n "$PKG" ] && [ -f "$PKG" ]; then
    export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
    $CC -O1 -Wall -Wno-unused-function -o build/host/ota_test tests/ota_test.c
    build/host/ota_test "$PKG" | tail -1
    head -c $(( $(wc -c < build/synth.bin) / 2 )) build/synth.bin > build/host/old_app.bin
    python3 tools/fm1pkg_make.py build/host/old_app.bin build/loader/ota.bin build/host/old.fwsc >/dev/null
    $CC -O1 -Wall -Wno-unused-function -o build/host/ldr_test tests/ldr_test.c
    build/host/ldr_test build/host/old.fwsc "$PKG" | tail -1
else
    echo "update path: skipped (no package: ./build.sh --release VERSION)"
fi
