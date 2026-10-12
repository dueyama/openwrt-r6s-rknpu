# Third-party notices

**Rockchip RKNPU:** Copyright (C) Rockchip Electronics Co.Ltd. Original author
notices, including Felix Zeng, remain in the source. The upstream driver files
use the legacy kernel `GPL-2.0` SPDX identifier. Original and modified file
hashes identify the exact source correspondence. Mainline adaptation files
and R6S helpers retain their marked GPL identifiers.

**Linux and OpenWrt:** Kernel source and DTS copyrights remain with their
respective contributors. The Linux IOMMU baseline is GPL-2.0-only; DTS material
uses its marked GPL-2.0-or-later/MIT alternative. The integration patch is a
delta, not a replacement license for its target files. OpenWrt's existing board
patches are obtained separately from the pinned upstream and retain their
original authorship and licenses.

**GNU C Library:** Copyright notices in original source and patches remain
with the Free Software Foundation and their stated contributors. The supplied
patches modify LGPL-2.1-or-later library material and test/build files under
their original terms. Copies of LGPL-2.1 and GPL license texts are included.
Adapted backports record their GNU origin commits in `runtime/sources.json`.

**GCC/libstdc++ and Ubuntu packaging:** Original copyright notices remain with
the Free Software Foundation and stated contributors. GCC's GPL-3.0 terms and
the GCC Runtime Library Exception 3.1 apply where stated by the source; that
exception is not a blanket license for every GCC build or documentation file.
Ubuntu-selected patches are projected to their libstdc++ hunks only, with
original-patch hashes retained. The source package versions and exact series
are recorded. Original GNU patch author headers are preserved.

**Offline sigmoid analysis:** The table-generation algorithm in
`analysis/fp16-sigmoid/rk3588_sigmoid_reference.py` references Greg Ordinary and
the rocket-userspace contributors at commit
`1401c46e75e45adee66b08b97a62e633306de016`. The pinned C source declares
`GPL-3.0-or-later` and `Copyright (C) 2026 The rocket-userspace authors`.
The Python adaptation credits that source and retains GPL-3.0-or-later, with
the unmodified GPL version 3 text in `analysis/fp16-sigmoid/COPYING`.
This is a separate CPU analysis module, not a kernel-linked component or a
verified hardware emulator. No upstream driver/device library is vendored.
Related rockchip-npu-notes documents are linked as references, not copied.
Exact pins, hashes and publication edits are in that directory's PROVENANCE.json.

**Related projects:** antonioacg/rknpu-rk3588 and w568w/rknpu-module are credited
as related work in `docs/PROVENANCE.md`. No license from those repositories is
being used to license this tree, and a direct copied-code relationship is not
asserted.

License texts in `LICENSES/` are reproduced without modification. The Linux
GPL-2.0 and MIT files include upstream SPDX usage metadata as well as the license
text. Component notices take precedence over any repository overview.
