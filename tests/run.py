#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run the production range predicates with a minimal portable host shim."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="rknpu-host-tests-") as name:
    tmp = Path(name)
    (tmp / "linux").mkdir()
    (tmp / "drm").mkdir()
    (tmp / "drm/drm.h").write_text("/* IOCTL macros are not used by these predicate tests. */\n")
    (tmp / "linux/types.h").write_text("""#pragma once
#include <stdint.h>
typedef uint8_t __u8; typedef int8_t __s8;
typedef uint16_t __u16; typedef int16_t __s16;
typedef uint32_t __u32; typedef int32_t __s32;
typedef uint64_t __u64; typedef int64_t __s64;
typedef uint32_t u32; typedef uint64_t u64;
""")
    # glibc's errno.h itself includes linux/errno.h; avoid a circular shim.
    (tmp / "linux/errno.h").write_text(
        "#include_next <linux/errno.h>\n" if sys.platform.startswith("linux")
        else "#include <errno.h>\n")
    (tmp / "linux/limits.h").write_text("#include <limits.h>\n")
    (tmp / "linux/ioctl.h").write_text("#include <sys/ioctl.h>\n")
    for label, flags in [("normal", []), ("sanitized", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
        exe = tmp / label
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", *flags,
                        "-I" + str(tmp), "-I" + str(root / "driver/include"),
                        str(root / "tests/task_range.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

# Exercise the actual production error-path functions with injected failures.
subprocess.run([sys.executable, str(root / "tests/error_paths.py")], check=True)
