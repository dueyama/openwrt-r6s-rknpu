#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build optional private GNU libraries on a separate native AArch64 Linux host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("component", choices=["glibc", "libstdcxx"])
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--jobs", type=int, default=2)
args = parser.parse_args()
if platform.system() != "Linux" or platform.machine() not in ("aarch64", "arm64"):
    parser.error("use a separate native AArch64 Linux build host")
if args.jobs < 1:
    parser.error("jobs must be positive")
source, out = args.source.resolve(), args.output.resolve()
if out.exists():
    parser.error("output must not exist")
spec = json.loads((root / "sources.json").read_text())[args.component]
for entry in spec["changed_files"]:
    if hashlib.sha256((source / entry["path"]).read_bytes()).hexdigest() != entry["sha256"]:
        parser.error("source mismatch: " + entry["path"])
env = os.environ.copy()
flags = "-O2 -g -fstack-protector-strong -fstack-clash-protection -mbranch-protection=standard"
env.update(LC_ALL="C", CFLAGS=flags, CXXFLAGS=flags)
env.setdefault("CC", "gcc" if args.component == "glibc" else "gcc-14")
env.setdefault("CXX", "g++" if args.component == "glibc" else "g++-14")
if args.component == "libstdcxx":
    version = subprocess.check_output(shlex.split(env["CXX"]) + ["-dumpfullversion"], text=True).strip()
    if not version.startswith("14."):
        parser.error("libstdc++ source requires a GCC 14 compiler")
out.mkdir(parents=True)
build = out / "build"
build.mkdir()
prefix = "/usr/lib/r6s-rknn-runtime"
common = ["--prefix=" + prefix, "--libdir=" + prefix + "/lib"]
if args.component == "glibc":
    configure = [str(source / "configure"), *common, "--enable-kernel=3.2",
                 "--enable-stack-protector=strong", "--enable-bind-now", "--enable-fortify-source=2",
                 "--disable-werror", "--disable-build-nscd", "--disable-timezone-tools"]
else:
    compat = build / "gthread-support"
    compat.mkdir()
    (compat / "gthr-default.h").symlink_to(source / "libgcc/gthr-posix.h")
    env.update(CPPFLAGS="-I" + str(compat), LDFLAGS="-Wl,-z,relro,-z,now")
    configure = [str(source / "libstdc++-v3/configure"), *common,
                 "--build=aarch64-linux-gnu", "--host=aarch64-linux-gnu",
                 "--with-gxx-include-dir=" + prefix + "/include/c++/14", "--disable-multilib",
                 "--enable-shared", "--disable-static", "--disable-nls", "--disable-libstdcxx-pch",
                 "--enable-libstdcxx-time=yes"]
for label, command in [("configure", configure), ("make", ["make", "-j" + str(args.jobs)])]:
    with (out / (label + ".log")).open("xb") as log:
        subprocess.run(command, cwd=build, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
if args.component == "glibc":
    mapping = {"libc.so.6": "libc.so", "ld-linux-aarch64.so.1": "elf/ld.so",
               "libm.so.6": "math/libm.so", "libpthread.so.0": "nptl/libpthread.so", "libdl.so.2": "dlfcn/libdl.so"}
else:
    config = (build / "config.h").read_text()
    for macro in ["_GLIBCXX_HAS_GTHREADS", "HAVE_POSIX_MEMALIGN"]:
        if "#define " + macro + " 1" not in config:
            raise RuntimeError("required configure result missing: " + macro)
    public_config = (build / "include/aarch64-linux-gnu/bits/c++config.h").read_text()
    for macro in ["_GLIBCXX_HAS_GTHREADS", "_GLIBCXX_HAVE_POSIX_MEMALIGN"]:
        if "#define " + macro + " 1" not in public_config:
            raise RuntimeError("required public configure result missing: " + macro)
    libraries = list((build / "src/.libs").glob("libstdc++.so.6.*"))
    if len(libraries) != 1:
        raise RuntimeError("ambiguous libstdc++ output")
    mapping = {"libstdc++.so.6": str(libraries[0].relative_to(build))}
lib = out / "lib"
lib.mkdir()
result = {}
for name, relative in mapping.items():
    target = lib / name
    # The private loader is executed directly; preserve the built ELF mode.
    shutil.copy2(build / relative, target)
    subprocess.run(["strip", "--strip-unneeded", str(target)], check=True)
    info = subprocess.check_output(["readelf", "-h", "-d", "-V", str(target)], text=True)
    if "AArch64" not in info:
        raise RuntimeError("unexpected ELF machine")
    (out / (name + ".readelf")).write_text(info)
    result[name] = hashlib.sha256(target.read_bytes()).hexdigest()
(out / "SHA256.json").write_text(json.dumps(result, indent=2) + "\n")
print("Built local library outputs. No install or deployment was performed.")
