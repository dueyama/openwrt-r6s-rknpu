# SPDX-License-Identifier: GPL-2.0-only
include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk
PKG_NAME:=kmod-r6s-rknpu
PKG_RELEASE:=11
PKG_LICENSE:=GPL-2.0-only
include $(INCLUDE_DIR)/package.mk

define KernelPackage/r6s-rknpu
  SUBMENU:=Other modules
  TITLE:=R6S three-core RKNPU 0.9.8 Linux 6.12 port
  DEPENDS:=@TARGET_rockchip_armv8 +kmod-drm
  KCONFIG:= \
    CONFIG_ROCKCHIP_RKNPU=m \
    CONFIG_ROCKCHIP_RKNPU_DRM_GEM=y \
    CONFIG_ROCKCHIP_RKNPU_FENCE=y \
    CONFIG_ROCKCHIP_RKNPU_DEBUG_FS=n \
    CONFIG_ROCKCHIP_RKNPU_PROC_FS=n \
    CONFIG_ROCKCHIP_RKNPU_SRAM=n \
    CONFIG_ROCKCHIP_RKNPU_DMA_HEAP=n \
    CONFIG_DEVFREQ_GOV_USERSPACE=y \
    CONFIG_SYNC_FILE=y \
    CONFIG_PM_DEVFREQ=y \
    CONFIG_PM_OPP=y \
    CONFIG_ROCKCHIP_IOMMU=y
  FILES:=$(LINUX_DIR)/drivers/rknpu/rknpu.ko
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef
define Build/Configure
endef
define Build/Compile
	sed -n '/[[:space:]]drivers\/rknpu\/rknpu[[:space:]]/p' $(LINUX_DIR)/Module.symvers > $(PKG_BUILD_DIR)/Module.symvers
endef

# No AUTOLOAD: experimental device activation requires an operator instruction.
$(eval $(call KernelPackage,r6s-rknpu))
