#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame
# The simulator (docs/SIMULATOR.md), everything under build/sim/:
#   1. the generated headers (tables, font) and the input matrix's keymap
#   2. the host test: the sim core in plain cc (tests/sim_test.c)
#   3. the wasm: the same core, with zig's clang (wasm32-wasi reactor; ZIG="python3 -m ziglang" works too)
#   4. the page next to it (web/sim: Hortator's layout in ObsSynth's colours and fonts) and its version.json
#   tools/build_sim.sh              all four
#   tools/build_sim.sh --host-only  1 and 2 (no zig)
# Serve it: python3 -m http.server -d build/sim 8000, then http://localhost:8000/
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
CC=${CC:-cc}
ZIG=${ZIG:-zig}
OUT=build/sim
GEN=$OUT/gen
HOST_ONLY=0
[ "${1:-}" = --host-only ] && HOST_ONLY=1
mkdir -p "$GEN" "$OUT/host"

"$PY" tools/gen_tables.py "$GEN/felucca_tables.h" >/dev/null
"$PY" tools/gen_font.py "$GEN/felucca_font.h" >/dev/null
"$PY" tools/gen_wavetables.py "$GEN/synth_wavetables.h" >/dev/null
"$PY" tools/gen_samples.py "$GEN/synth_samples.h" >/dev/null
awk '/static const int8_t FM1_KEYMAP/,/^};/' firmware/hal/fm1_input.h > "$GEN/sim_keymap.h"
grep -q FM1_KEYMAP "$GEN/sim_keymap.h" || { echo "build_sim: FM1_KEYMAP not found in firmware/hal/fm1_input.h" >&2; exit 1; }

# the GLO page's version and build, as tools/build.py: VERSION.txt, the commit (* when this tree differs from it)
REV=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)
[ -n "$(git status --porcelain -- . 2>/dev/null)" ] && REV="$REV*"
VDEFS=(-DSYNTH_VERSION="\"$(cat VERSION.txt)\"" -DSYNTH_BUILD="\"$REV\"")
CFLAGS=(-O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -I"$GEN" -Ifirmware/src -Itests "${VDEFS[@]}")
$CC "${CFLAGS[@]}" -o "$OUT/host/sim_test" tests/sim_test.c -lm
"$OUT/host/sim_test"
[ "$HOST_ONLY" = 1 ] && exit 0

EXPORTS=(sim_init sim_render sim_frame sim_btn sim_key sim_enc sim_master sim_leds sim_key_leds sim_fb sim_audio sim_playing
         sim_flash sim_flash_reset sim_flash_dirty sim_flash_clean sim_glow sim_key_glow sim_midi sim_preset sim_preset_name)
$ZIG cc -target wasm32-wasi -mexec-model=reactor -O2 -Wall -Wno-unused-function "${VDEFS[@]}" "${EXPORTS[@]/#/-Wl,--export=}" \
    -I"$GEN" -Ifirmware/src -Itests -o "$OUT/fm1sim.wasm" tests/sim_core.c
cp web/sim/*.* "$OUT/" && mkdir -p "$OUT/img" && cp web/sim/img/* "$OUT/img/"
# the page's "what's new" (app.js): VERSION.txt, the commit, the changelog's Unreleased and this version's notes
COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo none)
CDATE=$(git log -1 --format=%cs 2>/dev/null || date +%F)
VERSION_PY=$(cat <<'PY'
import json, re, sys
from pathlib import Path
log, version, commit, date = Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
sections, cur = {}, None
for line in (log.read_text(encoding="utf-8").splitlines() if log.exists() else []):
    m = re.match(r"## (Unreleased|(\d+\.\d+\.\d+) - (\d{4}-\d{2}-\d{2}))\s*$", line)
    if m:
        cur = sections.setdefault(m[2] or "Unreleased", {"date": m[3], "notes": []})
    elif cur is not None and line.startswith("- "):
        cur["notes"].append(line[2:].strip())
    elif cur is not None and line.startswith("  ") and cur["notes"]:
        cur["notes"][-1] += " " + line.strip()
def note(text):                                   # a short title: the words before the first colon or bracket
    text = text.replace("`", "").replace("**", "")
    head = re.split(r"[:(]", text, maxsplit=1)[0].strip()
    title = head if 0 < len(head) <= 48 else " ".join(text.split()[:5])
    return {"title": title, "text": text}
rel = sections.get(version, {"date": None, "notes": []})
print(json.dumps({"version": version, "commit": commit, "date": date,
                  "since": [note(n) for n in sections.get("Unreleased", {"notes": []})["notes"]],
                  "release": {"version": version, "date": rel["date"], "notes": [note(n) for n in rel["notes"]]}},
                 indent=1, ensure_ascii=False))
PY
)
# one per page language: index.html (English) reads version.json, pt.html version.pt.json
"$PY" -c "$VERSION_PY" CHANGELOG.md "$(cat VERSION.txt)" "$COMMIT" "$CDATE" > "$OUT/version.json"
"$PY" -c "$VERSION_PY" CHANGELOG.pt-BR.md "$(cat VERSION.txt)" "$COMMIT" "$CDATE" > "$OUT/version.pt.json"
echo "build_sim: $OUT/fm1sim.wasm ($(wc -c < "$OUT/fm1sim.wasm") B) and the page in $OUT/"
