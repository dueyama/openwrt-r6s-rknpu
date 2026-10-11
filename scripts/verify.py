#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check source correspondence and synthetic tests without hardware or network."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--vendor-archive", type=Path, help="optional pinned Rockchip archive for delta reconstruction")
args = parser.parse_args()
manifest = json.loads((root / "provenance/r10-source-files.json").read_text())
for relative, item in manifest.items():
    data = (root / relative).read_bytes()
    if hashlib.sha256(data).hexdigest() != item["sha256"] or len(data) != item["bytes"]:
        raise SystemExit("Source mismatch: " + relative)
runtime = json.loads((root / "runtime/sources.json").read_text())
for item in runtime.values():
    for patch in item["patches_in_order"]:
        if hashlib.sha256((root / "runtime" / patch["file"]).read_bytes()).hexdigest() != patch["sha256"]:
            raise SystemExit("Runtime patch mismatch: " + patch["file"])
for path in root.rglob("*.py"):
    if ".git" not in path.parts and "build" not in path.parts:
        ast.parse(path.read_text(), filename=str(path.relative_to(root)))
if args.vendor_archive:
    digest = hashlib.sha256(args.vendor_archive.read_bytes()).hexdigest()
    upstream = json.loads((root / "provenance/upstreams.json").read_text())
    if digest != upstream["rockchip_driver"]["sha256"]:
        raise SystemExit("Vendor archive checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="rknpu-delta-") as name:
        folder = Path(name)
        with tarfile.open(args.vendor_archive) as tar:
            tar.extractall(folder, filter="data")
        original = folder / "drivers/rknpu"
        subprocess.run(["patch", "-p1", "--batch", "--fuzz=0", "-i",
                        str(root / "patches/0001-rknpu-0.9.8-linux-6.12-r6s-r10.patch")], cwd=original, check=True)
        expected = {str(p.relative_to(root / "driver")): p.read_bytes()
                    for p in (root / "driver").rglob("*") if p.is_file()}
        actual = {str(p.relative_to(original)): p.read_bytes()
                  for p in original.rglob("*") if p.is_file() and not p.name.endswith(".orig")}
        if expected != actual:
            raise SystemExit("Driver patch does not reproduce driver/")
subprocess.run([sys.executable, str(root / "tests/run.py")], check=True)
print("PASS: source hashes, runtime patch hashes, Python syntax and synthetic tests")
