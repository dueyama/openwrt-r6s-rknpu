/* SPDX-License-Identifier: GPL-2.0 */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
#ifndef R6S_TASK_RANGE_H
#define R6S_TASK_RANGE_H

#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/types.h>

struct r6s_task_range {
	int start;
	int number;
};

/* Global request summary and independent RK3588 cfg3 selected-core BO ranges.
 * No task contents or MMIO reads. Legacy cfg1 containment is unchanged. */
static inline int r6s_rk3588_task_range(const struct rknpu_submit *args,
				     u64 table_bytes, bool mapped,
				     int core_index, int use_cores,
				     unsigned int configured_irqs,
				     unsigned int configured_mask,
				     u64 max_submit,
				     struct r6s_task_range *range)
{
	u64 global_end, sub_end;
	u32 sub_start, sub_number;

	unsigned int mask, bits, index;
	if (!args || !range || !mapped || core_index < 0 || core_index >= 3 ||
	    (configured_irqs != 1 && configured_irqs != 3) ||
	    configured_mask != ((1U << configured_irqs) - 1) ||
	    (unsigned int)core_index >= configured_irqs || !(args->flags & RKNPU_JOB_PC) ||
	    max_submit == 0 || max_submit > INT_MAX || !args->task_number)
		return -EINVAL;
	mask = args->core_mask;
	/* Explicit masks supported by the vendor commit dispatcher. AUTO is
	 * refused before side effects; userspace selects all three explicitly. */
	if ((mask != 1 && mask != 2 && mask != 4 && mask != 3 && mask != 7) ||
	    (mask & ~configured_mask) || !(mask & (1U << core_index)))
		return -EINVAL;
	bits = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1);
	if ((unsigned int)use_cores != bits)
		return -EINVAL;
	/* Vendor ABI: three-core partition entries are [2,3,4]. */
	index = use_cores == 3 ? core_index + 2 : core_index;
	sub_start = args->subcore_task[index].task_start;
	sub_number = args->subcore_task[index].task_number;
	global_end = (u64)args->task_start + args->task_number;
	sub_end = (u64)sub_start + sub_number;
	if (!sub_number || global_end > INT_MAX ||
	    global_end > table_bytes / sizeof(struct rknpu_task) ||
	    sub_end > INT_MAX ||
	    sub_end > table_bytes / sizeof(struct rknpu_task) ||
	    sub_number > (u32)(INT_MAX - max_submit + 1))
		return -EINVAL;

	/* Vendor cfg3 selects slots[core] for use1/2 and slots[core+2] for use3
	 * independently from the GEM table.
	 * The summary count is not the physical enclosing descriptor span.
	 * Keep the old parent interval restriction for the legacy cfg1 board. */
	if (configured_irqs != 3 &&
	    (sub_start < args->task_start || sub_end > global_end))
		return -EINVAL;

	range->start = sub_start;
	range->number = sub_number;
	return 0;
}

static inline int r6s_task_range_chunk(const struct r6s_task_range *range,
				     int submit_index, u64 max_submit,
				     struct r6s_task_range *chunk)
{
	u64 offset, start, remaining, number;

	if (!range || !chunk || range->start < 0 || range->number <= 0 ||
	    submit_index < 0 || max_submit == 0 || max_submit > INT_MAX ||
	    (u64)range->start + range->number > INT_MAX)
		return -EINVAL;
	offset = (u64)submit_index * max_submit;
	if (offset >= (u64)range->number)
		return -EINVAL;
	start = (u64)range->start + offset;
	remaining = range->number - offset;
	number = remaining > (u64)max_submit ? (u64)max_submit : remaining;
	chunk->start = start;
	chunk->number = number;
	return 0;
}

#endif
