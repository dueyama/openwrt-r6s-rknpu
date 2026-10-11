# NanoPi R6S / RK3588S NPU driver port for OpenWrt

An experimental port of Rockchip's RKNPU 0.9.8 driver to Linux 6.12.112 on
the FriendlyElec NanoPi R6S (RK3588S), with three-core device-tree integration,
OpenWrt packaging, and optional private GNU runtime build recipes.

The R6S combines hardware well suited to router use with an on-chip NPU. Our
initial web searches turned up relatively few documented examples of using that
NPU for router workloads, which motivated us to explore its potential for network
anomaly detection.

This led us to undertake the port to explore applying the method described in
[Kitsune: An Ensemble of Autoencoders for Online Network Intrusion Detection](https://arxiv.org/abs/1802.09089)
by Mirsky et al. to NPU-assisted network anomaly detection on an R6S running
OpenWrt. This repository shares the NPU driver port and platform integration
developed for that exploration. The application implementation is outside its
scope; no reproduction of the paper's results, detection accuracy, or performance
gain from NPU acceleration is claimed here.

This repository packages the R10 port with subsequent
[job, DMA and fence error-path corrections](docs/ERROR_PATH_REVIEW.md). Hardware bring-up and three-core job
execution have been reported on an R6S. Strict numerical comparison has not
passed; FP16 exact equivalence and application detection accuracy remain
unverified. Cold boot, automatic service activation and long-duration stability
are not validated. See [validation and limits](docs/VALIDATION.md) for the scope
of the available evidence.

## Project at a glance

| Question | Answer |
| --- | --- |
| Target hardware | FriendlyElec NanoPi R6S, Rockchip RK3588S, AArch64 |
| Software baseline | OpenWrt, Linux 6.12.112, Rockchip RKNPU 0.9.8, R10 port |
| Main contribution | NPU driver port, three-core device tree, IOMMU integration and build recipes |
| Research motivation | Exploring NPU-assisted network anomaly detection inspired by Kitsune / KitNET |
| Application scope | The Kitsune application, models and proprietary RKNN SDK are outside this repository |
| Verified scope | Isolated builds and synthetic checks; detailed results and hardware limits are in [VALIDATION.md](docs/VALIDATION.md) |

For a compact documentation index, see [llms.txt](llms.txt). Structured project
metadata is in [repo-metadata.jsonld](repo-metadata.jsonld); fixed source revisions
and dated verification results are linked below. These summaries describe this
repository and do not replace the component source notices or validation record.

## Contents

| Path | Purpose |
| --- | --- |
| `driver/` | R10 driver source, with original SPDX and copyright notices |
| `kernel/` | R6S DTS fragment and relevant kernel configuration |
| `patches/` | Exact deltas from pinned Rockchip/Linux sources and OpenWrt integration |
| `openwrt/` | Explicit-activation kernel package definition |
| `runtime/` | Optional glibc/libstdc++ source pins, patches, and isolated build recipes |
| `tests/` | Production task-range tests and error-path fault injection |
| `verification/` | Isolated Linux container and minimal OpenWrt build configuration |
| `provenance/` | Upstream pins, hashes, and per-file R10 source correspondence |

Start with [build instructions](docs/BUILD.md) and [port design](docs/PORTING.md).
The host-only check requires Python 3 and a C compiler with AddressSanitizer and
UndefinedBehaviorSanitizer support:

```sh
python3 scripts/verify.py
```

The driver uses the vendor RKNN interface. RKNN SDK headers, runtime binaries and
models must be obtained separately under their applicable terms; none are
distributed here. Application code and firmware binaries are also outside this
source repository's scope.

## Source and acknowledgements

The direct driver source is Rockchip's
[RKNPU 0.9.8 archive](https://github.com/airockchip/rknn-llm/tree/f7390530443bf84f0394255a449d7cbe81e69d1c/rknpu-driver).
The kernel changes build on [Linux](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/)
and [OpenWrt](https://github.com/openwrt/openwrt), with hardware information from
[Rockchip's kernel](https://github.com/rockchip-linux/kernel/tree/77168c8d5ab82399f65a80e9f807b50ba37cf483).

Related mainline RKNPU work includes
[antonioacg/rknpu-rk3588](https://github.com/antonioacg/rknpu-rk3588) and
[w568w/rknpu-module](https://github.com/w568w/rknpu-module).
Both are acknowledged as related implementations. Direct code ancestry from
these projects is not established; the direct source inputs are listed in
[Provenance](docs/PROVENANCE.md).

This independent integration combines sources from Rockchip, Linux, OpenWrt
and GNU libraries. [Provenance](docs/PROVENANCE.md) distinguishes direct source
inputs from related work and lists fixed reference revisions.

## Licensing

This repository contains multiple licenses. Driver SPDX identifiers and upstream
copyright notices are retained; the DTS fragment is dual licensed. GNU library
patches retain their component licenses and applicable GCC runtime exception.
New integration scripts, tests and documentation are GPL-2.0-only unless marked
otherwise. Read [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
