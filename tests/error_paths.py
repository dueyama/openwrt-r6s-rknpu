#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile production error paths with fault-injectable host kernel stubs."""
import os
from pathlib import Path
import re
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    # These C functions contain balanced braces, including their comments/strings.
    match = re.search(r"^(?:static (?:inline )?)?[^\n]*\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError("Missing production function: " + name)
    brace = source.index("{", match.end())
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end] + "\n"


def run():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    job = (ROOT / "driver/rknpu_job.c").read_text()
    gem = (ROOT / "driver/rknpu_gem.c").read_text()
    fence = (ROOT / "driver/rknpu_fence.c").read_text()
    reset = (ROOT / "driver/rknpu_reset.c").read_text()
    names = ["rknpu_wait_core_index", "rknpu_core_mask", "rknpu_job_free", "rknpu_job_get", "rknpu_job_put",
             "rknpu_job_cleanup_work", "rknpu_job_alloc", "rknpu_job_next",
             "rknpu_job_finish", "rknpu_job_done", "rknpu_schedule_core_index",
             "rknpu_job_schedule", "rknpu_job_detach", "rknpu_job_timeout_dump", "rknpu_job_recover", "rknpu_soft_reset", "rknpu_job_abort", "rknpu_fuzz_status",
             "rknpu_irq_handler", "rknpu_job_timeout_clean", "rknpu_submit"]
    production = "\n".join(function(reset, name) for name in ["rknpu_reset_assert", "rknpu_reset_deassert", "rknpu_reset_hw"])
    production += "\n".join(function(job, name) for name in names)
    production += function(gem, "rknpu_gem_get_pages") + function(gem, "rknpu_gem_put_pages")
    production += function(gem, "rknpu_gem_init") + function(gem, "rknpu_gem_release")
    production += function(gem, "rknpu_gem_alloc_buf") + function(gem, "rknpu_gem_free_buf")
    production += function(fence, "rknpu_fence_get_fd")
    with tempfile.TemporaryDirectory(prefix="rknpu-error-tests-") as folder:
        tmp = Path(folder)
        (tmp / "production.inc").write_text(production)
        for path in ["linux/spinlock.h", "linux/dma-fence.h", "linux/irq.h",
                     "linux/refcount.h", "linux/types.h", "linux/ioctl.h",
                     "drm/drm_device.h", "drm/drm.h"]:
            target = tmp / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("/* Types and kernel calls are supplied by error_paths_host.h. */\n")
        for pages in [1, 0]:
            for label, flags in [("normal", []), ("sanitized", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]:
                exe = tmp / f"errors-{pages}-{label}"
                subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-parameter", "-Wno-unused-function", "-g", *flags,
                                f"-DRKNPU_GEM_ALLOC_FROM_PAGES={pages}",
                                "-I" + str(tmp), "-I" + str(ROOT / "driver/include"),
                                str(ROOT / "tests/error_paths.c"), "-o", str(exe)], check=True)
                subprocess.run([str(exe)], check=True, cwd=tmp)
        thread_modes = [("normal", []), ("sanitized", ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]
        if os.environ.get("RKNPU_TSAN") == "1":
            thread_modes.append(("thread-sanitized", ["-fsanitize=thread", "-fno-omit-frame-pointer"]))
        for label, flags in thread_modes:
            exe = tmp / f"threads-{label}"
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-Wno-unused-function", "-pthread", "-g", *flags,
                            "-DRKNPU_GEM_ALLOC_FROM_PAGES=1", "-I" + str(tmp),
                            "-I" + str(ROOT / "driver/include"), str(ROOT / "tests/recovery_threads.c"),
                            "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, cwd=tmp)
        # Reintroduce each defect in temporary generated code, never in driver/.
        # Each mutant must compile and fail a runtime assertion, so a typo or
        # compiler error cannot masquerade as a successful regression check.
        mutations = {
            "completion-before-reset": ("ret = rknpu_reset_hw(dev);", "if (timed_out) { rknpu_job_finish(timed_out, reason); }\n\t\tret = rknpu_reset_hw(dev);"),
            "dispatch-during-reset": ("if (rknpu_dev->soft_reseting)\n\t\tgoto out;", "if (false)\n\t\tgoto out;"),
            "irq-during-reset": ("if (rknpu_dev->soft_reseting) {", "if (false) {"),
            "complete-after-reset-failure": ("if (!ret) {\n\t\tfor (i = 0; i < n; i++)", "if (true) {\n\t\tfor (i = 0; i < n; i++)"),
            "forget-other-active-cores": ("if (k == n)\n\t\t\tactive[n++] = job;", "if (k == n && job == timed_out)\n\t\t\tactive[n++] = job;"),
            "ignore-dma32-gfp": (function(gem, "rknpu_gem_init"), function(gem, "rknpu_gem_init").replace("(rknpu_obj->flags & RKNPU_MEM_DMA32)", "0")),
            "abort-stale-core": ("if (core->job == job) {", "if (core->job == job && !job->int_status[i]) {"),
            "irq-missing-reference": ("\trknpu_job_get(job);\n\tspin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);\n\n\tspin_lock_irqsave(&job->job_lock, flags);", "\tspin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);\n\n\tspin_lock_irqsave(&job->job_lock, flags);"),
            "timeout-us-vs-ms": ("(s64)job->args->timeout * 1000", "job->args->timeout"),
            "dma-wrong-allocator": ("#if RKNPU_GEM_ALLOC_FROM_PAGES\n\t\t\t/* Match", "#if 0\n\t\t\t/* Match"),
            "invalid-fence-leak": ("\t\t\trknpu_job_put(job);\n\t\t\treturn -EINVAL;\n\t\t}\n\t\targs->fence_fd", "\t\t\treturn -EINVAL;\n\t\t}\n\t\targs->fence_fd"),
            "fence-wait-leak": ('\t\t\t\t\t  ret);\n\n\t\t\trknpu_job_put(job);\n\t\t\treturn ret;', '\t\t\t\t\t  ret);\n\n\t\t\treturn ret;'),
        }
        for name, (before, after) in mutations.items():
            assert production.count(before) == 1, name
            (tmp / "production.inc").write_text(production.replace(before, after))
            exe = tmp / name
            subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-parameter", "-Wno-unused-function", "-g",
                            "-DRKNPU_GEM_ALLOC_FROM_PAGES=1", "-I" + str(tmp),
                            "-I" + str(ROOT / "driver/include"), str(ROOT / "tests/error_paths.c"),
                            "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, cwd=tmp)
            assert result.returncode != 0 and "assert" in result.stderr.lower(), (name, result)
            print("PASS: regression detected " + name, flush=True)


if __name__ == "__main__":
    run()
