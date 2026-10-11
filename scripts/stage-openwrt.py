#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage the integration in a disposable OpenWrt checkout, without building."""
import argparse
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--openwrt", required=True, type=Path)
args = parser.parse_args()
tree = args.openwrt.resolve()
target = tree / "target/linux/rockchip"
if not (target / "patches-6.12").is_dir() or not (tree / "rules.mk").is_file():
    parser.error("expected an OpenWrt checkout using rockchip patches-6.12")
items = [
    (root / "driver", target / "files/drivers/rknpu"),
    (root / "kernel/r6s-npu.dtsi", target / "files/arch/arm64/boot/dts/rockchip/r6s-npu.dtsi"),
    (root / "patches/0003-linux-rknpu-integration.patch", target / "patches-6.12/990-r6s-rknpu.patch"),
    (root / "patches/0002-linux-6.12.112-r6s-iommu.patch", target / "patches-6.12/991-r6s-rknpu-iommu.patch"),
    (root / "openwrt/kmod-r6s-rknpu", tree / "package/kernel/kmod-r6s-rknpu"),
]
for _, destination in items:
    if destination.exists() or destination.is_symlink():
        parser.error("refusing to overwrite: " + str(destination))
for source, destination in items:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination)
    else:
        shutil.copyfile(source, destination)
print("Staged R10 integration. Select the R6S target and kmod-r6s-rknpu before building.")
