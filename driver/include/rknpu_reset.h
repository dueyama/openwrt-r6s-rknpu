/* SPDX-License-Identifier: GPL-2.0 */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#ifndef __LINUX_RKNPU_RESET_H
#define __LINUX_RKNPU_RESET_H

#include <linux/reset.h>

#include "rknpu_drv.h"

int rknpu_reset_get(struct rknpu_device *rknpu_dev);

int rknpu_soft_reset(struct rknpu_device *rknpu_dev);

/* Internal hardware operation; caller must hold reset_lock and gate dispatch. */
int rknpu_reset_hw(struct rknpu_device *rknpu_dev);

#endif
