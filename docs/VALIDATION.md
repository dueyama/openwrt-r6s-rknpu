# Validation status

This document distinguishes reported R10 hardware results from reproducible
checks supplied with this repository. Neither establishes universal hardware
compatibility or application accuracy.

## FP16 sigmoid investigation (2026-10-12)

The [public-table CPU investigation](../analysis/fp16-sigmoid/FP16_SIGMOID_LUT.md)
reproduced 1,026 u16 entries (2,052 bytes) exactly. Three fixed interpolation
hypotheses each matched 0/257 saved final NN scores. Maximum score difference
fell about 70%, while mean absolute difference increased 83–93%. Every full-NN
row required an unverified outside-domain saturation assumption; actual RKNN
LUT/conversion settings and fused precision remain unidentified.

The earlier 113/257 failures against a project-defined FP32 development tolerance
remain failures. This is not an IEEE compliance test or an intrusion-detection
accuracy evaluation. Public code reproduces the table/hypothesis calculation;
private models, inputs and per-record traces are not distributed. No new NPU/SDK
execution or router change was performed for this CPU investigation.

## Reset and DMA32 corrections

The follow-up review of `289960f` accepted both findings. Revision 12 has 70
production-function error scenarios, both allocator configurations, twelve
mutation checks and host-thread recovery races. The suite passed normally and
under ASan/UBSan on macOS and AArch64 Linux; macOS host-thread tests also passed
ThreadSanitizer. A separate final module build passed `-Werror` and modpost.
See [recovery behavior, reset failure policy and limitations](RESET_DMA32_REVIEW.md).
No revised firmware/APK or device qualification is implied.

## Earlier error-path corrections

The review of initial public revision `493d8b7d` accepted four findings and added
46 production-function fault scenarios, allocator variants and six mutation
checks. See [findings, corrections and limits](ERROR_PATH_REVIEW.md). The fixed
module passed a separate Linux 6.12.112 compilation/modpost check; the full
OpenWrt, APK, firmware and GNU runtime results below remain historical evidence
for the initial source and are not rerun or relabeled as fixed-source results.

## R10 validation background

* Linux 6.12.112, RKNPU 0.9.8, R10 three-core integration on an R6S.
* Strict module compilation/modpost succeeded; 168 undefined symbols were
  resolved against the recorded kernel exports. The source and kernel inputs
  were checked by hash in that build.
* An earlier synthetic host harness reported 85 normal and ASan/UBSan cases
  covering ranges, RAM-backed MMIO, queue/GEM/IOMMU and abort/counter behavior.
  The public harness below is a smaller, newly written suite, not that harness.
* Hardware bring-up and three-core job execution have been reported on an R6S.
  The build and host tests supplied here do not reproduce those hardware results.
* Strict numerical comparison has not passed. FP16 exact equivalence and
  detection accuracy remain unverified.
* Cold boot, automatic service activation and long-duration stability are not
  validated.

## Source and build verification

* Patch 0001 applies without fuzz to the SHA256-pinned Rockchip archive and
  reproduces the current driver, including the error-path corrections.
  The inventory retains original R10 hashes and identifies subsequent changes.
* Patch 0002 applies without fuzz to the pinned Linux 6.12.112 IOMMU file and
  reproduces the recorded R10 IOMMU source.
* Patch 0003 applies to Linux Kconfig/Makefile and the R6S board context after
  OpenWrt's existing board patches. The pre-integration board context was
  independently reconstructed from public patches and matched exactly.
* `tests/run.py` passes 67 synthetic range checks both normally and with
  AddressSanitizer/UndefinedBehaviorSanitizer. It uses the actual production
  range header and vendor GPL UAPI structs with a small host type/include shim.
  The separate error-path suite models selected allocation and IRQ lifetimes
  with host stubs; neither suite executes kernel scheduling or NPU hardware.
* On 2026-10-11, both GNU source preparations completed on a separate native
  AArch64 Linux host with a case-sensitive volume. All pinned archive, patch
  and changed-source hashes passed, including all 8 glibc and 11 libstdc++
  additional patches.
* Fresh glibc and libstdc++ builds completed from the extracted recipes.
  All six output libraries were identified as AArch64 ELF and hashed.
  The loader retains executable permission; libstdc++ has both gthreads and
  POSIX memalign enabled in its generated configuration headers.
* `runtime/check.py` passed using those newly built private libraries and
  loader. The recorded loader resolution selects the new libc and libstdc++.
  Synthetic checks cover version, invalid fopen conversion modes, resolver
  initialization, path normalization, C++ threads and aligned-allocation
  overflow rejection. This is not the full upstream test suite, an SDK test,
  or a complete OpenWrt runtime-closure check.
* The 67 range checks also passed normally and under ASan/UBSan in the new
  Linux container. The Linux errno shim uses the system Linux header without
  creating a recursive glibc include.
* A fresh OpenWrt `make -j2 V=s` completed from the pinned public commit and
  minimal in-tree package configuration. Kernel 6.12.112, the published driver
  source, R10 IOMMU source and required configuration were verified in that tree.
* The new RKNPU module passed strict compilation/modpost. All 168 undefined
  symbols resolve against the new kernel exports. The packaged module is
  76,488 bytes and has vermagic `6.12.112 SMP mod_unload aarch64`.
* The generated R6S DTB passed checks for all three NPU core register regions,
  shared interrupts, four MMU register regions, clocks/resets, 200 MHz assigned
  rate, voltage OPP and power/IOMMU phandle references.
* Both ext4 and squashfs images passed checksum, extracted gzip payload, MBR
  signature and fwtool metadata checks. Both identify the R6S and rockchip/armv8.
  The NPU APK contains the same module as the assembled root filesystem, with
  no NPU autoload entry. The individual local APK is unsigned; content extraction
  does not establish distribution trust or install anything.
* Machine-readable hashes and check results are in
  [the dated verification record](../verification/results-2026-10-11.json).

These checks establish source correspondence, compilation and selected synthetic
behavior. They do not establish full hardware qualification, complete SDK
security coverage, a complete inference application or bit-for-bit firmware
reproducibility.
