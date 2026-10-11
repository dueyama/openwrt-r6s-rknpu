#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Prepare pinned GNU runtime sources in a new directory; never run a build."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tarfile

root = Path(__file__).resolve().parent
manifest = json.loads((root / "sources.json").read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("component", choices=manifest)
parser.add_argument("--downloads", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
spec = manifest[args.component]
downloads = args.downloads.resolve()
out = args.output.resolve()
if out.exists():
    parser.error("output must not exist")
for name, info in spec["downloads"].items():
    p = downloads / name
    if p.stat().st_size != info["bytes"] or hashlib.sha256(p.read_bytes()).hexdigest() != info["sha256"]:
        parser.error("source checksum mismatch: " + name)
for entry in spec["patches_in_order"]:
    if hashlib.sha256((root / entry["file"]).read_bytes()).hexdigest() != entry["sha256"]:
        parser.error("patch checksum mismatch: " + entry["file"])
out.mkdir(parents=True)
if args.component == "glibc":
    probe = out / "CaseSensitivityProbe"
    probe.write_text("probe")
    insensitive = (out / "casesensitivityprobe").exists()
    probe.unlink()
    if insensitive:
        out.rmdir()
        parser.error("glibc requires a case-sensitive filesystem (advisories and ADVISORIES are distinct)")

def extract(archive, target):
    with tarfile.open(archive) as tar:
        tar.extractall(target, filter="data")

def apply(patch, source, extra=()):
    subprocess.run(["patch", "-p1", "--batch", "--forward", "--fuzz=0", *extra,
                    "-i", str(patch.resolve())], cwd=source, check=True)

if args.component == "glibc":
    extract(downloads / "glibc_2.39.orig.tar.xz", out)
    source = out / "glibc-2.39"
    if not source.is_dir():
        candidates = list(out.glob("glibc*"))
        if len(candidates) != 1:
            raise RuntimeError("unexpected glibc archive layout")
        source = candidates[0]
    extract(downloads / "glibc_2.39-0ubuntu8.9.debian.tar.xz", source)
    patches = source / "debian/patches"
    for line in (patches / "series").read_text().splitlines():
        fields = shlex.split(line, comments=True)
        if fields:
            apply(patches / fields[0], source, fields[1:])
else:
    extract(downloads / "gcc-14_14.2.0.orig.tar.gz", out)
    candidates = list(out.rglob("gcc-14.2.0-dfsg.tar.xz"))
    if len(candidates) != 1:
        candidates = [p for p in out.rglob("*.tar.*") if hashlib.sha256(p.read_bytes()).hexdigest() == spec["inner_tar_sha256"]]
    if len(candidates) != 1 or hashlib.sha256(candidates[0].read_bytes()).hexdigest() != spec["inner_tar_sha256"]:
        raise RuntimeError("GCC inner archive checksum mismatch")
    extract(candidates[0], out / "inner")
    source = out / "inner/gcc-14.2.0"
    if not source.is_dir():
        raise RuntimeError("unexpected GCC archive layout")
for entry in spec["patches_in_order"]:
    apply(root / entry["file"], source)
for entry in spec["changed_files"]:
    if hashlib.sha256((source / entry["path"]).read_bytes()).hexdigest() != entry["sha256"]:
        raise RuntimeError("prepared source mismatch: " + entry["path"])
print("Prepared source verified:", source)
