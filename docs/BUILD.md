# Building the R6S integration

Use a fresh, disposable OpenWrt build checkout on a supported Linux build host.
These commands prepare and build source; deployment is a separate operator task.
The repository has no flashing, reboot, service-start or automatic module-load
script. Python is required only on the build/test host.

## Source baseline

The tested kernel is Linux **6.12.112** and the driver is **0.9.8, R10 port**.
The OpenWrt build baseline is commit
`66673aad8f99e450f6ee2587254b1b2d19d45150`. This repository provides NPU integration
for that baseline. On 2026-10-11, a fresh minimal full build from this
baseline completed in a separate AArch64 Linux environment, producing both ext4
and squashfs images. No image was deployed. See the [isolated verification
recipe](../verification/README.md) and [results](../verification/results-2026-10-11.json).

OpenWrt's existing rockchip patches must be applied before the integration
patch. They introduce `rk3588s-nanopi-r6.dtsi`; the DTS hunk therefore does not
apply directly to the unpatched Linux 6.12.112 board file. The pre-integration
R6S board file was reconstructed from pinned public OpenWrt patches and matched
the recorded build input exactly. Its hashes are in `provenance/upstreams.json`.

## Stage and build

Install the normal OpenWrt build prerequisites for the build host, then:

```sh
git clone https://github.com/openwrt/openwrt.git openwrt-build
git -C openwrt-build checkout 66673aad8f99e450f6ee2587254b1b2d19d45150
python3 scripts/stage-openwrt.py --openwrt ./openwrt-build
cd openwrt-build
./scripts/feeds update -a
./scripts/feeds install -a
make menuconfig
```

Select Rockchip, ARMv8, FriendlyElec NanoPi R6S and `kmod-r6s-rknpu`.
Record feed commits if building a full image: `feeds update -a` follows each
configured feed ref and is not a bit-for-bit firmware reproduction guarantee.

```sh
make defconfig
make download -j2
make target/linux/compile -j2 V=s
make package/kernel/kmod-r6s-rknpu/compile -j2 V=s
```

Confirm that the prepared kernel is actually 6.12.112. Inspect the resolved kernel
configuration against `kernel/config.fragment`. Required facilities include DRM
GEM, sync fences, PM/OPP/devfreq, Rockchip IOMMU and the userspace governor.
SRAM and DMA-heap alternatives are disabled. Debug/proc diagnostics are disabled
in the recorded configuration. The package deliberately has no `AUTOLOAD` entry.
Kconfig may omit an unavailable disabled symbol entirely; an absent SRAM or
DMA-heap symbol also means disabled, rather than requiring a literal unset line.

The staging script refuses to overwrite existing destinations. It copies the
already-patched `driver/` tree; do not apply patch 0001 again to that tree.
Patch 0001 is supplied for provenance and independent reconstruction from the
original Rockchip archive. Patches 0002 and 0003 are applied by OpenWrt.

## Module-only rebuild against an exact prepared kernel

When a matching, fully built kernel tree and OpenWrt toolchain already exist:

```sh
cp -R driver module-build
make -C "$KERNEL_DIR" M="$PWD/module-build" ARCH=arm64 \
  CROSS_COMPILE=aarch64-openwrt-linux-musl- -j2 modules
```

Set the toolchain `PATH` and OpenWrt `STAGING_DIR` for that build first. A matching
`Module.symvers`, configuration, compiler and kernel ABI are required. Retain
strict modpost and `-Werror`; do not use a force-load option to bypass ABI checks.
The recorded stripped R10 module was 76,488 bytes with SHA256
`634e3831903243d379b8db6252df67d5be4c948cf586a5401940544a8a59af0e`
and vermagic `6.12.112 SMP mod_unload aarch64`. It is not distributed here and a
different build environment need not produce that binary hash.

The IOMMU change and device tree are kernel/firmware integration requirements.
Building only `rknpu.ko` against an unmodified kernel does not install those
changes. Module loading is explicit, and the render node number is not assumed
to be the same on every system.

## Optional userspace

See [runtime/README.md](../runtime/README.md) for the private GNU loader boundary.
The OpenWrt system libc remains musl. Obtain SDK inputs separately; check their
terms and ABI compatibility before attempting inference. Models and application
code are outside this repository's scope.
