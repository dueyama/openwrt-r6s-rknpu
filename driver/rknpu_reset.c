// SPDX-License-Identifier: GPL-2.0
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/iommu.h>

#include "rknpu_reset.h"

#ifndef FPGA_PLATFORM
static inline struct reset_control *rknpu_reset_control_get(struct device *dev,
							    const char *name)
{
	struct reset_control *rst = NULL;

	rst = devm_reset_control_get(dev, name);
	if (IS_ERR(rst))
		LOG_DEV_ERROR(dev,
			      "failed to get rknpu reset control: %s, %ld\n",
			      name, PTR_ERR(rst));

	return rst;
}
#endif

int rknpu_reset_get(struct rknpu_device *rknpu_dev)
{
#ifndef FPGA_PLATFORM
	int i = 0;
	int num_srsts = 0;

	num_srsts = of_count_phandle_with_args(rknpu_dev->dev->of_node,
					       "resets", "#reset-cells");
	if (num_srsts <= 0) {
		LOG_DEV_ERROR(rknpu_dev->dev,
			      "failed to get rknpu resets from dtb\n");
		return num_srsts;
	}

	rknpu_dev->srsts = devm_kcalloc(rknpu_dev->dev, num_srsts,
					sizeof(*rknpu_dev->srsts), GFP_KERNEL);
	if (!rknpu_dev->srsts)
		return -ENOMEM;

	for (i = 0; i < num_srsts; ++i) {
		rknpu_dev->srsts[i] = devm_reset_control_get_exclusive_by_index(
			rknpu_dev->dev, i);
		if (IS_ERR(rknpu_dev->srsts[i])) {
			rknpu_dev->num_srsts = i;
			return PTR_ERR(rknpu_dev->srsts[i]);
		}
	}

	rknpu_dev->num_srsts = num_srsts;

	return num_srsts;
#endif

	return 0;
}

#ifndef FPGA_PLATFORM
static int rknpu_reset_assert(struct reset_control *rst)
{
	int ret = -EINVAL;

	if (!rst)
		return -EINVAL;

	ret = reset_control_assert(rst);
	if (ret < 0) {
		LOG_ERROR("failed to assert rknpu reset: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rknpu_reset_deassert(struct reset_control *rst)
{
	int ret = -EINVAL;

	if (!rst)
		return -EINVAL;

	ret = reset_control_deassert(rst);
	if (ret < 0) {
		LOG_ERROR("failed to deassert rknpu reset: %d\n", ret);
		return ret;
	}

	return 0;
}
#endif

/* Exclusive reset API success guarantees assertion/deassertion. Verify status
 * too when the provider supports it; the pinned Rockchip provider has no
 * .status method (-ENOTSUPP). Other status errors are recovery failures.
 * The job layer owns reset_lock, dispatch gating, and completion notifications. */
int rknpu_reset_hw(struct rknpu_device *rknpu_dev)
{
#ifndef FPGA_PLATFORM
	struct iommu_domain *domain;
	int ret = 0, err, i;

	if (rknpu_dev->bypass_soft_reset || rknpu_dev->num_srsts <= 0)
		return -EOPNOTSUPP;

	for (i = 0; i < rknpu_dev->num_srsts; i++) {
		err = rknpu_reset_assert(rknpu_dev->srsts[i]);
		if (!err) {
			err = reset_control_status(rknpu_dev->srsts[i]);
			err = err == -ENOTSUPP || err > 0 ? 0 :
			      (err < 0 ? err : -EIO);
		}
		if (err && !ret)
			ret = err;
	}
	/* Do not restart partially stopped hardware after an assertion failure. */
	if (ret)
		return ret;
	udelay(10);

	for (i = 0; i < rknpu_dev->num_srsts; i++) {
		err = rknpu_reset_deassert(rknpu_dev->srsts[i]);
		if (!err) {
			err = reset_control_status(rknpu_dev->srsts[i]);
			err = err == -ENOTSUPP ? 0 : (err > 0 ? -EIO : err);
		}
		if (err && !ret)
			ret = err;
	}
	if (ret)
		return ret;
	udelay(10);

	mutex_lock(&rknpu_dev->domain_lock);
	domain = rknpu_dev->iommu_en ?
		iommu_get_domain_for_dev(rknpu_dev->dev) : NULL;
	if (rknpu_dev->iommu_en && !domain)
		ret = -ENODEV;
	else if (domain) {
		iommu_detach_device(domain, rknpu_dev->dev);
		ret = iommu_attach_device(domain, rknpu_dev->dev);
	}
	mutex_unlock(&rknpu_dev->domain_lock);
	if (ret)
		return ret;
	if (rknpu_dev->config->state_init)
		rknpu_dev->config->state_init(rknpu_dev);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}
