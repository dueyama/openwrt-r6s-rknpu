# Reset ordering and DMA32 review

Reviewed baseline: `289960f256a0e2e0b7fe5a2e4dbdee7d47a2d2c4`.
Both findings are accepted. They were reproduced using the baseline production
functions with host service stubs; no DMA corruption or high physical-page
allocation was observed on hardware.

## Findings

1. **High: completion and redispatch before reset.** The baseline detached the
   aborted job and signaled its fence before calling reset. A fence callback
   could dispatch the queued successor with the reset count still zero. The
   simulated subsequent device reset cleared hardware ownership while the
   successor remained active in software. This is a regression in the first
   public error-path correction, especially relative to the older asynchronous
   reset-before-detach path. It is not attributed to the original R10 port.
2. **Medium: physical DMA32 request lost at GEM initialization.** The baseline
   configured shmem's GFP mask while the zero-initialized object had no request
   flags. On a 40-bit DMA/IOMMU device, a contiguous allocation failure therefore
   reached the new page fallback without `__GFP_DMA32`. The initialization defect
   also affected the older explicit noncontiguous path. A low IOVA does not
   establish allocation from the physical DMA32 zone.

## Recovery implementation

`reset_lock` serializes queue insertion and process-context recovery. A new
`dispatch_lock` covers slot selection through hardware commit, including IRQ
completion and chunk resubmission. Recovery takes that lock to close the gate;
all earlier dispatch/IRQ critical sections have then completed. IRQ handlers
that arrive during recovery leave PC registers alone. New queue insertions wait
for recovery; `job_next` cannot commit while its gate is closed.

The hardware reset affects the entire NPU. Recovery retains references to every
unique active job across all cores, including jobs selected on only some of
their required cores. Pending-only jobs remain queued. While dispatch is gated,
it masks PC interrupts, performs the hardware reset, reattaches the IOMMU domain,
restores device state and clears/checks raw and masked PC interrupt status.
Only after success does it detach the affected jobs, balance task counts, set
DONE, signal error fences and wake waiters. Other active jobs receive `-EIO`;
the triggering job keeps its timeout/error reason. It then reopens dispatch and
starts preserved queued work. Canceling a queued-only job needs no global reset.
Explicit reset ioctls and debug reset requests use this same recovery path.

Lock order is `reset_lock` (process context), `dispatch_lock`, per-job
`job_lock`, then `irq_lock`; no sleeping reset operation runs under a spinlock.
The timeout reader retains its job reference after releasing the dispatch gate.
The synchronous waiter does not treat "reset started" as job completion and
avoids task/MMIO access while recovery is active.

The stop/restart boundary relies on the Linux **exclusive** reset API contract:
successful assert/deassert operations act on the reset line immediately.
Optional status readback is also checked when supported. The pinned Linux
6.12.112 Rockchip provider has assert/deassert callbacks but no status callback;
its `-ENOTSUPP` status result is accepted only after a successful exclusive reset
operation. Other status errors or contradictory status fail recovery. See the
[Linux reset API documentation](https://docs.kernel.org/driver-api/reset.html#shared-and-exclusive-resets).
This API contract is not a measurement of real NPU DMA drain or board recovery.

## Reset failure behavior

Assertion/deassertion failure, absent reset controls, bypass mode, PM failure,
IOMMU reattachment failure, or uncleared interrupts leaves the device blocked.
The active slots, scheduler/task-GEM/IOMMU references and unsignaled fences are
retained; PM and module references acquired by recovery remain held. No DONE or
successful-completion indication is manufactured. New submissions are rejected
with `-EIO`. A submitting ioctl whose job cannot be safely retired remains in an
uninterruptible completion wait instead of returning its buffers for reuse.

Only an explicit device reset retries this state. A successful retry retires
all affected jobs with errors and resumes the preserved queue. If recovery
cannot succeed, blocked calls and retained resources can persist until reboot.
This is a deliberate availability cost, not a guarantee of automatic recovery.
Forced unbind/unload and system suspend/shutdown are not qualified by these tests.

## DMA32 implementation

`rknpu_gem_init` now receives the request flags before computing the shmem GFP
mask. Both the initial noncontiguous path and the contiguous-to-page fallback
therefore preserve DMA32 and zeroing requirements. DMA-buffer imports use zero
request flags; this change does not relocate imported pages.

## Verification

* 67 existing task-range checks, normally and under ASan/UBSan.
* 70 production-function error scenarios (46 earlier cases plus 24 additions),
  with page allocation enabled and disabled, normally and under ASan/UBSan.
* Recovery tests assert no fence signal or completion wakeup before reset,
  no dispatch/IRQ advancement during recovery, and matching hardware-model and
  active-slot ownership afterward. They cover other active cores, queued work,
  repeated aborts, explicit reset, 12 failure variants and retry, unsupported
  optional status readback, and a blocked submitting ioctl.
* GFP tests enter the actual GEM initializer, allocator and page-backed helper.
  The DRM page service records the mapping GFP mask it receives. Tests include
  DMA32 + 40-bit DMA + IOMMU + contiguous allocation failure, explicit
  noncontiguous allocation, non-DMA32 controls, zeroing, and 32-bit/no-IOMMU masks.
* A host-thread test pauses recovery while another submitter tries to enqueue
  and an IRQ arrives. Another 100 races exercise IRQ versus abort. These pass
  normally and under ASan/UBSan on macOS and AArch64 Linux; the macOS thread run
  also passes ThreadSanitizer.
* Twelve temporary mutations compile and fail runtime assertions, including
  early completion, missing dispatch/IRQ gates, completion after reset failure,
  forgetting other active cores and loss of DMA32 at initialization.
* The final driver passed a separate Linux 6.12.112 AArch64 module build with
  `-Werror` and modpost. The baseline kernel volume was read-only. Patch 0001
  reproduces the final driver byte-for-byte from the pinned vendor archive.

Run `python3 scripts/verify.py`; optionally set `RKNPU_TSAN=1` where host
ThreadSanitizer is available. Exact source/test hashes, compilers and module
results are in [the verification record](../verification/reset-dma32-review-2026-10-11.json).
The earlier [error-path record](../verification/error-path-review-2026-10-11.json)
remains a historical record of revision 11 and does not cover these corrections.

## Remaining limits

Host locks, MMIO, reset controllers, hardware ownership, PM, shmem and DMA/IOMMU
services are models. Actual scheduling/IRQ handlers, recovery, reset sequencing,
GEM initialization and allocation-selection functions are compiled from the
production source. The hardware-commit leaf and timed job-wait function remain
host stubs. The harness does not establish kernel SMP memory-order
correctness, lockdep/KASAN/KCSAN safety, physical DMA address placement or cache
coherency. Hardware stop/reset timing, late interrupts, failure recovery and
continued inference need device tests. Numerical equivalence, boot and endurance
remain unqualified. Revision 12 identifies the corrected package source; no
revision 12 firmware or APK was built, and no corrected module was installed.
