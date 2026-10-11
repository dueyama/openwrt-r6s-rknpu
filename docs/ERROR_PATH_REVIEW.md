# Error-path review of the initial public revision

Reviewed baseline: `493d8b7d1f733bee42d029ff601831a367eaf8f3`.
The four findings below are accepted from source analysis. The corresponding
patterns also occur in the SHA256-pinned Rockchip 0.9.8 archive identified in
[PROVENANCE.md](PROVENANCE.md); they are not attributed specifically to the
Linux 6.12 port. This review does not report an observed device failure.

## Findings and changes

| Finding | Baseline behavior | Correction |
| --- | --- | --- |
| High: IRQ/abort job lifetime | An unexpected IRQ sets `irq_entry` and returns. Abort then skips clearing that core's job pointer but frees the job; another IRQ or the load timer can follow the stale pointer. | Remove the sticky entry flag. Detach every active/queued reference under `irq_lock`, balance per-core task counts, and retain references for the submitter, scheduler and transient IRQ/queue/timeout readers. Serialize commit, completion and abort with a per-job lock. |
| High: asynchronous timeout units | Elapsed microseconds are compared directly with the millisecond timeout. A 1000 ms job can be expired by a later NONBLOCK submission after about 1 ms. | Widen to signed 64-bit before converting ms to microseconds. Select only asynchronous jobs under `irq_lock` and retain a reference before dropping the lock. Reuse the common abort path instead of freeing a job independently per core or draining unrelated queued jobs. |
| High: DMA fallback pairing | A failed contiguous DMA allocation changes the NON_CONTIGUOUS flag, then retries `dma_alloc_attrs`; free/mmap choose the page-backed path using that flag. | With page allocation enabled, release the temporary DMA page array and fall back through `rknpu_gem_get_pages`. With page allocation disabled, retain the paired DMA allocation/free path. |
| Medium: input-fence error leaks | Invalid fence descriptors and negative waits return without releasing the job or its GEM reference. | Every error drops the submitter's job reference; the job releases its owned argument copy, GEM reference and output fence exactly once when its final reader exits. |

Job arguments are now copied for synchronous submissions as well as NONBLOCK
submissions: an IRQ already holding a job reference must not access an ioctl
argument buffer after that ioctl returns. All jobs initialize cleanup work, and
the final release runs in workqueue context because GEM destruction may sleep.
A scheduler reference also prevents early asynchronous completion from freeing
the job while the submitting thread still reads its result.

The same ownership changes cover partially queued multi-core jobs and sparse
core masks. Scheduler/IOMMU ownership and output-fence signaling are finalized
once, including timeout failures. A timeout retains the existing age basis
(time since job allocation, including queue time); it is not redefined as an
execution-only deadline. Synchronous submissions retain their own wait/abort
path and are not destroyed by another submission's asynchronous cleanup.

Adjacent fence cleanup is covered too: waits convert milliseconds to jiffies,
a zero wait result returns `-ETIMEDOUT`, positive `long` wait results are not
truncated to `int`, failed argument-copy allocation does not acquire a GEM
reference, output-fence fd errors unwind the job, and a failed sync-file
allocation returns its reserved fd.

## Regression checks

Run `python3 scripts/verify.py`. The supplied tests comprise:

* The existing 67 production task-range cases, normally and under ASan/UBSan.
* 46 error-path scenarios, normally and under ASan/UBSan, with both settings of
  `RKNPU_GEM_ALLOC_FROM_PAGES`. `tests/error_paths.py` extracts the actual driver
  functions at test time; the job struct and UAPI come from production headers.
  Kernel services and the GEM test object are represented by host stubs.
* Invalid IRQ followed by abort and another IRQ; a deterministic abort between
  IRQ pointer acquisition and processing; normal completion winning a timeout
  race; partial multi-core completion; active plus queued references; sparse
  masks; chunk resubmission; and single output-fence/domain release.
* Timeout boundaries at 0, 1, 1000 and `UINT32_MAX` ms, the original 1100 us
  false-expiry case, preservation of younger queued jobs, and protection of
  synchronous jobs from asynchronous cleanup.
* Injected DMA/page allocation and SG conversion failures, plus invalid,
  interrupted, failed, timed-out and successful fence waits and fd failures.
* Six temporary mutations reintroduce stale-core retention, missing IRQ
  ownership, the timeout-unit error, wrong DMA fallback, and each of the two
  input-fence leaks. Each mutant must compile and then fail a runtime assertion.

The complete suite passed with Apple Clang on macOS and GCC on native AArch64
Linux. The corrected driver also passed an external-module build against the
configured Linux 6.12.112 OpenWrt kernel with `-Werror` and modpost. The baseline
kernel tree was mounted read-only; only the separate module output was written.
Patch 0001 was reconstructed and checked against the pinned vendor archive.
Current source hashes and results are in
[the review verification record](../verification/error-path-review-2026-10-11.json).

## Limits

These are host fault-injection tests and a module compilation check. The IRQ
interleavings are deterministic simulations, not a complete SMP execution or
kernel KASAN/KCSAN/lockdep run. The DMA stubs verify allocator selection and
release pairing, not real DMA mapping, cache coherency or memory pressure.
The synchronous wait implementation and real reset/MMIO behavior are not
executed by this harness.

The vendor-wide reset policy, reset failure/bypass handling, attribution of
late hardware interrupts to a subsequent job, power management, and full
module-unload/workqueue behavior still require separate kernel/device testing.
These changes do not establish successful hardware recovery in those cases.
No firmware image or installable APK was rebuilt for this review, and the
revised module was not loaded on a device. Package revision 11 distinguishes
the changed source from the initial revision 10 package; the earlier firmware,
APK and runtime hashes in `results-2026-10-11.json` describe the initial source.
Numerical equivalence, cold boot, service startup and endurance remain outside
the verification performed here.
