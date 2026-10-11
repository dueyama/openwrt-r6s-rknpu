# Provenance and references

Machine-readable pins and hashes are in `provenance/upstreams.json`,
`provenance/r10-source-files.json` and `runtime/sources.json`.

## Direct source

| Component | Fixed source | Relationship |
| --- | --- | --- |
| RKNPU driver | `airockchip/rknn-llm` at `f7390530443bf84f0394255a449d7cbe81e69d1c`, archive `rknpu_driver_0.9.8_20241009.tar.bz2` | Direct source for driver/ and patch 0001 |
| Linux IOMMU | `gregkh/linux` at `7aba70ab2e8d8ab3cd45abcaa15e1119a00dc42a` (v6.12.112) | Direct baseline for patch 0002 |
| Linux build integration | Same Linux revision plus OpenWrt rockchip patches | Kconfig/Makefile and R6S board integration in patch 0003 |
| OpenWrt | `openwrt/openwrt` at `66673aad8f99e450f6ee2587254b1b2d19d45150` | Public board-patch reference and packaging framework |
| GNU runtime | Pinned Ubuntu source packages and GNU commits in runtime/sources.json | Direct source/patch dependencies for optional private libraries |

The Rockchip driver archive SHA256 is
`188306a3eba7ca48c186fe2a39acf232dec8536484581e54e9b96c3f65677eba`.
Patch 0001 reconstructs the published driver from that archive. The vendored
driver retains upstream SPDX/copyright/author notices. The per-file inventory
identifies unchanged upstream files and modified/new files.

## Device-tree references

The R6S fragment combines board integration with register, clock, reset, power
and OPP information from
[Rockchip's kernel at 77168c8d](https://github.com/rockchip-linux/kernel/tree/77168c8d5ab82399f65a80e9f807b50ba37cf483)
and
[Linux v6.18 at 7d0a66e4](https://github.com/torvalds/linux/blob/7d0a66e4bb9081d75c82ec4957c50034cb0ea449/arch/arm64/boot/dts/rockchip/rk3588-base.dtsi).
It is adapted for this RKNPU integration, including the private MMU compatible,
explicit three-domain mapping and conservative fixed OPP. It is not a verbatim
upstream DTS. The original dual-license identifier is preserved.

## Related work, not established code ancestry

* [antonioacg/rknpu-rk3588](https://github.com/antonioacg/rknpu-rk3588/tree/deaea9e0b935992391a9c7ddfff7725973d353c2),
  reviewed at `deaea9e0b935992391a9c7ddfff7725973d353c2` on 2026-10-11.
* [w568w/rknpu-module](https://github.com/w568w/rknpu-module/tree/a8792fe6b633f90cf2c6808267cac327537ab4cf),
  reviewed at `a8792fe6b633f90cf2c6808267cac327537ab4cf` on 2026-10-11.

These projects provide related mainline RKNPU implementations. The revisions
above are fixed reference points, not established direct source inputs to this
tree. Direct code ancestry from these projects is not established; incorporated
sources are identified in the direct-source table above.
Their contributors retain their copyrights; linking a reference is not a
substitute for its license if code is incorporated later.

## Excluded proprietary and application inputs

RKNN Toolkit2 2.3.2 at `59a913d172e7f5ff03c9076e2ec7b1b1288ffd08` is a
userspace compatibility reference. Its SDK header contains a
proprietary/confidential notice. SDK headers, copied declarations, shared
libraries and model files are not distributed. Obtain and review those
components separately from their owner. A repository-level license must not be
used to override component-specific notices.

Application code, feature extraction, models, traffic collection and user
interfaces are outside this repository's scope. GNU runtime patches apply to
their identified library sources and do not grant rights to excluded SDK or
application components.
