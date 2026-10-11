# R10 port changes

The original vendor driver targets Rockchip BSP interfaces. This integration
keeps the vendor submission ABI while adapting the selected path to the tested
Linux 6.12.112/RK3588S environment.

* `rknpu_mainline_iommu.c` uses the mainline IOMMU/DMA boundary instead of the BSP
  helpers. The NPU requires a translated domain; an identity domain is refused.
* `rknpu_mainline_devfreq.c` uses regulator/clock OPP support with a fixed
  200 MHz profile. The DTS supplies a 700 mV target with the declared voltage
  bounds. This release does not qualify higher frequencies or dynamic tuning.
* Driver probe and PM code handle the RK3588 three-domain arrangement, clocks,
  reset resources and error cleanup. The board/SoC checks keep this path scoped
  to the intended R6S/RK3588 configuration.
* The DTS describes all three register/IRQ/reset groups, four MMU register
  windows, six MMU clocks and three power domains. Four-cell GIC interrupt
  specifiers are retained.
* The Linux IOMMU patch adds the private `r6s,rk3588-iommu-3core` compatible,
  attaches the three PM domains, checks MMIO mappings and obtains six clocks.
  A translated DMA default is selected only for this provider/NPU combination;
  existing providers retain their previous default-domain behavior.
* Task selection validates the selected descriptor intervals against the buffer
  object size and integer limits before submission side effects. For cfg3,
  selected partitions are independent of the global summary interval. The cfg1
  parent-containment rule is retained. Explicit masks 1, 2, 4, 3 and 7 are
  supported; AUTO (0), 5 and 6 are rejected.
* Three-core ABI slots are `[2,3,4]`; one/two-core slots are indexed by core.
  Chunking retains the hardware submission limit. Queue, DMA, IRQ, timeout,
  abort and PM controls remain part of the full driver path.
* GEM lifetime, DMA synchronization, version-specific kernel APIs and
  diagnostics are adapted in the accompanying driver delta. Dormant BSP source
  files remain for correspondence; the Makefile selects the mainline files.

The initial source extraction preserved R10 functional code and DTS properties.
Subsequent job-lifetime, timeout, DMA fallback and fence error-path fixes are
recorded in [the first error-path review](ERROR_PATH_REVIEW.md). Revision 12
adds [serialized reset recovery and DMA32 initialization](RESET_DMA32_REVIEW.md),
including a fail-closed recovery state. Original R10 and current source hashes
remain in `provenance/r10-source-files.json`. The initial revision 10 OpenWrt
package built successfully. The revision 12 driver passed a separate module
build, but a revision 12 APK and firmware image have not been built. No automatic
module-load entry is added. Installation and boot behavior remain untested.

This is a downstream, experimental integration. The private DT compatible has
not been represented as an accepted upstream binding, and schema/upstream
acceptance is not claimed. Host predicate tests do not establish complete
kernel concurrency, DMA or hardware fault behavior.
