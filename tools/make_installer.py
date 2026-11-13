#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE; ObsSynth FM-1: 2026 Orian Beltrame
"""The web installer for a built package (Hortator's web/make_site.py, the installer part): web/installer/index_pkg.html
with fm1pkg.js, fm1ota.js and counter.js (off) inlined and the package's metadata, the package beside it.

  tools/make_installer.py [PACKAGE.fwsc] [OUT]     (default: build/synth-UNTESTED.fwsc, build/installer)

Serve OUT on localhost (python3 -m http.server -d build/installer 8001) and open it in Chrome or Edge: WebMIDI with
SysEx, the FM-1 on a USB data cable straight into the computer.
"""
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
WEB = SRC / "web" / "installer"
LIB = ("fm1pkg.js", "fm1ota.js", "counter.js")


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def main(pkg, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca-family package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # the pinned loader: only fm1pkg_make.py packages, no vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it")
    label = SRC / "build" / "version.txt"                # tools/build.py: version+commit of the build
    version = label.read_text().strip() if label.exists() else (SRC / "VERSION.txt").read_text().strip()
    version += " (untested)" if "UNTESTED" in pkg.name else ""
    html = (WEB / "index_pkg.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once")
    lib = "\n".join(strip_module((WEB / f).read_text(encoding="utf-8")) for f in LIB)
    meta = json.dumps({"version": version, "product": product, "pkg": pkg.name})
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(html.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    shutil.copy(pkg, out / pkg.name)
    print(f"installer: {out / 'index.html'} -> {pkg.name} ({len(raw)} B, identity {product}, version {version})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else SRC / "build" / "synth-UNTESTED.fwsc",
         sys.argv[2] if len(sys.argv) > 2 else SRC / "build" / "installer")
