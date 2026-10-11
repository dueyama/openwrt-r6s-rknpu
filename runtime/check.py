#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run synthetic regression checks through the newly built private loader."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--glibc-output", required=True, type=Path)
parser.add_argument("--libstdcxx-output", required=True, type=Path)
parser.add_argument("--glibc-source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
glibc, cxx = args.glibc_output.resolve(), args.libstdcxx_output.resolve()
source, out = args.glibc_source.resolve(), args.output.resolve()
if out.exists():
    parser.error("output must not exist")
for folder in [glibc, cxx]:
    for name, digest in json.loads((folder / "SHA256.json").read_text()).items():
        if hashlib.sha256((folder / "lib" / name).read_bytes()).hexdigest() != digest:
            parser.error("built library hash mismatch: " + name)
loader = glibc / "lib/ld-linux-aarch64.so.1"
if not os.access(loader, os.X_OK):
    parser.error("the private loader must retain executable permission")
out.mkdir(parents=True)
c = out / "libc-check.c"
c.write_text(r'''#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <gnu/libc-version.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dl-path-normalize.h"
int main(void) {
    assert(strcmp(gnu_get_libc_version(), "2.39") == 0);
    const char *modes[] = {"w,ccs=", "w,ccs=,", "w,ccs=          ,"};
    for (unsigned i = 0; i < 3; ++i) {
        errno = 0;
        FILE *f = fopen("fopen-check.tmp", modes[i]);
        assert(f == NULL && errno == EINVAL);
    }
    /* Long resolver search entries; res_ninit performs no DNS query. */
    char search[600];
    memset(search, 'a', sizeof(search));
    for (unsigned i = 63; i < sizeof(search)-1; i += 64) search[i] = '.';
    search[sizeof(search)-1] = '\0';
    assert(setenv("LOCALDOMAIN", search, 1) == 0);
    struct __res_state state = {0};
    assert(res_ninit(&state) == 0);
    res_nclose(&state);
    char path[] = "/alpha//beta/../gamma/./";
    size_t n = _dl_normalize_path(path);
    assert(n == strlen(path) && strcmp(path, "/alpha/gamma") == 0);
    puts("PASS: private glibc version, fopen rejection, resolver initialization, path normalization");
    return 0;
}
''')
cpp = out / "cxx-check.cpp"
cpp.write_text(r'''#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <numeric>
#include <thread>
#include <vector>
#include <cstdio>
int main() {
    std::vector<int> values(1000, 7);
    long total = 0;
    std::thread worker([&]{ total = std::accumulate(values.begin(), values.end(), 0L); });
    worker.join();
    assert(total == 7000);
    void *p = ::operator new(128, std::align_val_t(64));
    assert(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
    ::operator delete(p, std::align_val_t(64));
    bool rejected = false;
    volatile std::size_t huge = std::numeric_limits<std::size_t>::max() - 31;
    try { p = ::operator new(huge, std::align_val_t(64)); }
    catch (const std::bad_alloc&) { rejected = true; }
    if (!rejected) ::operator delete(p, std::align_val_t(64));
    assert(rejected);
    std::puts("PASS: private libstdc++ vector, threads, aligned allocation and overflow rejection");
}
''')
subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(source / "elf"),
                str(c), "-lresolv", "-o", str(out / "libc-check")], check=True)
subprocess.run(["g++-14", "-O2", "-Wall", "-Wextra", "-Werror", "-std=c++17", "-pthread",
                str(cpp), "-o", str(out / "cxx-check")], check=True)
library_path = str(glibc / "lib") + ":" + str(cxx / "lib") + ":/usr/lib/aarch64-linux-gnu"
command = [str(loader), "--inhibit-cache", "--library-path", library_path]
env = {"PATH": "/usr/bin:/bin", "LANG": "C", "LC_ALL": "C"}
results = {}
for name in ["libc-check", "cxx-check"]:
    listing = subprocess.check_output(command + ["--list", str(out / name)], text=True, env=env)
    if str(glibc / "lib/libc.so.6") not in listing:
        raise RuntimeError("test did not select the private libc")
    if name == "cxx-check" and str(cxx / "lib/libstdc++.so.6") not in listing:
        raise RuntimeError("test did not select the private libstdc++")
    (out / (name + ".libraries.txt")).write_text(listing)
    run = subprocess.run(command + [str(out / name)], cwd=out, env=env, text=True,
                         capture_output=True, timeout=60)
    (out / (name + ".stdout")).write_text(run.stdout)
    (out / (name + ".stderr")).write_text(run.stderr)
    results[name] = {"exit": run.returncode, "stdout": run.stdout}
    if run.returncode:
        raise RuntimeError("runtime check failed: " + name)
(out / "RESULT.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
