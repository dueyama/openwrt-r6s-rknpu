// SPDX-License-Identifier: GPL-2.0
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sync_file.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>

#include "rknpu_ioctl.h"
#include "rknpu_drv.h"
#include "rknpu_reset.h"
#include "rknpu_gem.h"
#include "rknpu_fence.h"
#include "rknpu_mem.h"
#include "rknpu_iommu.h"
#include "rknpu_job.h"
#include "r6s_diag.h"
#include "r6s_task_range.h"

#define _REG_READ(base, offset) readl(base + (offset))
#define _REG_WRITE(base, value, offset) writel(value, base + (offset))

#define REG_READ(offset) _REG_READ(rknpu_core_base, offset)
#define REG_WRITE(value, offset) _REG_WRITE(rknpu_core_base, value, offset)


/* Disabled unless an owner explicitly enables the read-only load parameter.
 * One selected core0 job, at most three snapshots; no scheduler/IRQ decision.
 * PM has one additional delayed-off report, with no new timer or PM request. */
bool r6s_diag_once;
module_param(r6s_diag_once, bool, 0444);
MODULE_PARM_DESC(r6s_diag_once, "One bounded core0 metadata/register snapshot job; default off");

static void r6s_diag_select(struct rknpu_job *job, int core_index,
                           int selected_start, int selected_number)
{
    struct rknpu_device *d = job->rknpu_dev;
    struct rknpu_submit *a = job->args;
    if (!r6s_diag_once || core_index != 0 || !r6s_diag_can_read(d) ||
        atomic_cmpxchg(&d->r6s_diag_claimed, 0, 1) != 0)
        return;
    job->r6s_diag_selected = true;
    atomic_set(&job->r6s_diag_stages, 0);
    pr_info("R6S_NPU_DIAG task global=%u/%u sub0=%u/%u selected=%d/%d flags=%#x cores=%u mask=%#x task_BO_flags=%#x BO_classes=%d/%d/%d/%d sync_contig_SG=%d/%d first_cfg=%u last_cfg=%u first_irq=%#x last_irq=%#x cmd_high32=%u base_high32=%u\n",
        a->task_start, a->task_number, a->subcore_task[0].task_start,
        a->subcore_task[0].task_number, selected_start, selected_number,
        a->flags, job->use_core_num, a->core_mask,
        ((struct rknpu_gem_object *)(uintptr_t)a->task_obj_addr)->flags,
        atomic_read(&d->r6s_diag_bo_classes[0]),
        atomic_read(&d->r6s_diag_bo_classes[1]),
        atomic_read(&d->r6s_diag_bo_classes[2]),
        atomic_read(&d->r6s_diag_bo_classes[3]),
        atomic_read(&d->r6s_diag_sync_classes[0]),
        atomic_read(&d->r6s_diag_sync_classes[1]),
        job->first_task->regcfg_amount, job->last_task->regcfg_amount,
        job->first_task->int_mask, job->last_task->int_mask,
        !!(job->first_task->regcmd_addr >> 32), !!(a->task_base_addr >> 32));
}

static void r6s_diag_job_snapshot(struct rknpu_job *job, unsigned int stage)
{
    struct rknpu_device *d = job->rknpu_dev;
    struct r6s_diag_snapshot s;
    unsigned int bit;
    if (!r6s_diag_once || !job->r6s_diag_selected || stage > 2)
        return;
    bit = 1U << stage;
    if (atomic_fetch_or(bit, &job->r6s_diag_stages) & bit)
        return;
    if (!r6s_diag_read(d, &s)) {
        pr_info("R6S_NPU_DIAG snapshot=%u skipped unavailable/suspended banks\n", stage);
        return;
    }
    pr_info("R6S_NPU_DIAG snapshot=%u pc_mask=%#x status=%#x raw=%#x task=%#x cna_status=%#x cna_pointer=%#x core_status=%#x core_pointer=%#x refs=%d active=%u pending_off=%u\n",
        stage, s.pc_mask, s.pc_status, s.pc_raw, s.pc_task,
        s.cna_status, s.cna_pointer, s.core_status, s.core_pointer,
        atomic_read(&d->power_refcount), !!pm_runtime_active(d->dev),
        !!delayed_work_pending(&d->power_off_work));
}

static int rknpu_wait_core_index(int core_mask)
{
	int index = 0;

	switch (core_mask) {
	case RKNPU_CORE0_MASK:
	case RKNPU_CORE0_MASK | RKNPU_CORE1_MASK:
	case RKNPU_CORE0_MASK | RKNPU_CORE1_MASK | RKNPU_CORE2_MASK:
		index = 0;
		break;
	case RKNPU_CORE1_MASK:
		index = 1;
		break;
	case RKNPU_CORE2_MASK:
		index = 2;
		break;
	default:
		break;
	}

	return index;
}

static int rknpu_core_mask(int core_index)
{
	int core_mask = RKNPU_CORE_AUTO_MASK;

	switch (core_index) {
	case 0:
		core_mask = RKNPU_CORE0_MASK;
		break;
	case 1:
		core_mask = RKNPU_CORE1_MASK;
		break;
	case 2:
		core_mask = RKNPU_CORE2_MASK;
		break;
	default:
		break;
	}

	return core_mask;
}

/* The RK3588 core0 port still receives the vendor subcore table.
 * Validate before queueing, dereferencing a task or writing any MMIO. */
static int r6s_rk3588_job_task_range(struct rknpu_job *job, int core_index,
                                  struct r6s_task_range *range)
{
    struct rknpu_device *dev;
    u64 table_bytes;
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
    struct rknpu_gem_object *task_obj;
#else
    struct rknpu_mem_object *task_obj;
#endif

    if (!job || !job->rknpu_dev || !job->args || !range)
        return -EINVAL;
    dev = job->rknpu_dev;
    if (!dev->rk3588_core0_banks || !dev->config)
        return -EINVAL;
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
    task_obj = (struct rknpu_gem_object *)(uintptr_t)job->args->task_obj_addr;
    if (!task_obj)
        return -EINVAL;
    table_bytes = min_t(u64, task_obj->base.size, task_obj->size);
#else
    task_obj = (struct rknpu_mem_object *)(uintptr_t)job->args->task_obj_addr;
    if (!task_obj)
        return -EINVAL;
    table_bytes = task_obj->size;
#endif
    return r6s_rk3588_task_range(job->args, table_bytes, !!task_obj->kv_addr,
                              core_index, job->use_core_num,
                              dev->config->num_irqs, dev->config->core_mask,
                              dev->config->max_submit_number, range);
}

static int r6s_rk3588_job_task_chunk(struct rknpu_job *job, int core_index,
                                  struct r6s_task_range *chunk)
{
    struct r6s_task_range range;
    int ret = r6s_rk3588_job_task_range(job, core_index, &range);

    if (ret)
        return ret;
    return r6s_task_range_chunk(&range,
                               atomic_read(&job->submit_count[core_index]),
                               job->rknpu_dev->config->max_submit_number,
                               chunk);
}

static int rknpu_get_task_number(struct rknpu_job *job, int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	int task_num = job->args->task_number;

	if (core_index >= RKNPU_MAX_CORES || core_index < 0) {
		LOG_ERROR("invalid rknpu core index: %d", core_index);
		return 0;
	}

	if (rknpu_dev->rk3588_core0_banks) {
		struct r6s_task_range range;

		if (r6s_rk3588_job_task_range(job, core_index, &range))
			return 0;
		return range.number;
	}

	if (rknpu_dev->config->num_irqs > 1) {
		if (job->use_core_num == 1 || job->use_core_num == 2)
			task_num =
				job->args->subcore_task[core_index].task_number;
		else if (job->use_core_num == 3)
			task_num = job->args->subcore_task[core_index + 2]
					   .task_number;
	}

	return task_num;
}

static void rknpu_job_free(struct rknpu_job *job)
{
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	struct rknpu_gem_object *task_obj = NULL;

	task_obj =
		(struct rknpu_gem_object *)(uintptr_t)job->args->task_obj_addr;
	if (task_obj)
		rknpu_gem_object_put(&task_obj->base);
#endif

	if (job->fence)
		dma_fence_put(job->fence);

	if (job->args_owner)
		kfree(job->args);

	kfree(job);
}

static int rknpu_job_cleanup(struct rknpu_job *job)
{
	rknpu_job_free(job);

	return 0;
}

static void rknpu_job_cleanup_work(struct work_struct *work)
{
	struct rknpu_job *job =
		container_of(work, struct rknpu_job, cleanup_work);

	rknpu_job_cleanup(job);
}

static inline struct rknpu_job *rknpu_job_alloc(struct rknpu_device *rknpu_dev,
						struct rknpu_submit *args)
{
	struct rknpu_job *job = NULL;
	int i = 0;
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	struct rknpu_gem_object *task_obj = NULL;
#endif

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return NULL;

	job->timestamp = ktime_get();
	job->rknpu_dev = rknpu_dev;
	job->use_core_num = (args->core_mask & RKNPU_CORE0_MASK) +
			    ((args->core_mask & RKNPU_CORE1_MASK) >> 1) +
			    ((args->core_mask & RKNPU_CORE2_MASK) >> 2);
	atomic_set(&job->run_count, job->use_core_num);
	atomic_set(&job->interrupt_count, job->use_core_num);
	job->iommu_domain_id = args->iommu_domain_id;
	for (i = 0; i < rknpu_dev->config->num_irqs; i++)
		atomic_set(&job->submit_count[i], 0);
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	task_obj = (struct rknpu_gem_object *)(uintptr_t)args->task_obj_addr;
	if (task_obj)
		rknpu_gem_object_get(&task_obj->base);
#endif

	if (!(args->flags & RKNPU_JOB_NONBLOCK)) {
		job->args = args;
		job->args_owner = false;
		return job;
	}

	job->args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!job->args) {
		kfree(job);
		return NULL;
	}
	*job->args = *args;
	job->args_owner = true;

	INIT_WORK(&job->cleanup_work, rknpu_job_cleanup_work);

	return job;
}

static inline int rknpu_job_wait(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	struct rknpu_task *last_task = NULL;
	struct rknpu_subcore_data *subcore_data = NULL;
	struct rknpu_job *entry, *q;
	void __iomem *rknpu_core_base = NULL;
	int core_index = rknpu_wait_core_index(job->args->core_mask);
	unsigned long flags;
	int wait_count = 0;
	bool continue_wait = false;
	int ret = -EINVAL;
	int i = 0;

	subcore_data = &rknpu_dev->subcore_datas[core_index];

	do {
		ret = wait_event_timeout(subcore_data->job_done_wq,
					 job->flags & RKNPU_JOB_DONE ||
						 rknpu_dev->soft_reseting,
					 msecs_to_jiffies(args->timeout));

		if (++wait_count >= 3)
			break;

		if (ret == 0) {
			int64_t elapse_time_us = 0;
			spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
			elapse_time_us = ktime_us_delta(ktime_get(),
							job->hw_commit_time);
			continue_wait =
				job->hw_commit_time == 0 ?
					true :
					(elapse_time_us < args->timeout * 1000);
			spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
			LOG_ERROR(
				"job: %p, mask: %#x, job iommu domain id: %d, dev iommu domain id: %d, wait_count: %d, continue wait: %d, commit elapse time: %lldus, wait time: %lldus, timeout: %uus\n",
				job, args->core_mask, job->iommu_domain_id,
				rknpu_dev->iommu_domain_id, wait_count,
				continue_wait,
				(job->hw_commit_time == 0 ? 0 : elapse_time_us),
				ktime_us_delta(ktime_get(), job->timestamp),
				args->timeout * 1000);
		}
	} while (ret == 0 && continue_wait);

	last_task = job->last_task;
	if (!last_task) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		for (i = 0; i < job->use_core_num; i++) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			list_for_each_entry_safe(
				entry, q, &subcore_data->todo_list, head[i]) {
				if (entry == job) {
					list_del(&job->head[i]);
					break;
				}
			}
		}
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

		LOG_ERROR("job commit failed\n");
		return ret < 0 ? ret : -EINVAL;
	}

	last_task->int_status = job->int_status[core_index];

	if (ret <= 0) {
		args->task_counter = 0;
		rknpu_core_base = rknpu_dev->base[core_index];
		if (args->flags & RKNPU_JOB_PC) {
			uint32_t task_status = REG_READ(
				rknpu_dev->config->pc_task_status_offset);
			args->task_counter =
				(task_status &
				 rknpu_dev->config->pc_task_number_mask);
		}

		LOG_ERROR(
			"failed to wait job, task counter: %d, flags: %#x, ret = %d, elapsed time: %lldus\n",
			args->task_counter, args->flags, ret,
			ktime_us_delta(ktime_get(), job->timestamp));

		return ret < 0 ? ret : -ETIMEDOUT;
	}

	if (!(job->flags & RKNPU_JOB_DONE))
		return -EINVAL;

	args->task_counter = args->task_number;
	args->hw_elapse_time = job->hw_elapse_time;

	return 0;
}

/* Restores RK3588 S_POINTER initialization independently of IRQ count.
 * Other SoCs retain their existing path. No access derives from PC+0x1004. */
static inline int r6s_rk3588_pointer_init(struct rknpu_device *dev, int core)
{
	if (!dev->rk3588_core0_banks)
		return 0;
	if (core < 0 || core >= dev->config->num_irqs ||
		!dev->cna_base[core] || !dev->core_base[core])
		return -EINVAL;
	writel(0xe + 0x10000000 * core, dev->cna_base[core] + 0x4);
	writel(0xe + 0x10000000 * core, dev->core_base[core] + 0x4);
	return 0;
}

static inline int rknpu_job_subcore_commit_pc(struct rknpu_job *job,
					      int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
	struct rknpu_gem_object *task_obj =
		(struct rknpu_gem_object *)(uintptr_t)args->task_obj_addr;
#endif
#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
	struct rknpu_mem_object *task_obj =
		(struct rknpu_mem_object *)(uintptr_t)args->task_obj_addr;
#endif
	struct rknpu_task *task_base = NULL;
	struct rknpu_task *first_task = NULL;
	struct rknpu_task *last_task = NULL;
	void __iomem *rknpu_core_base;
	int task_start = args->task_start;
	int task_end;
	int task_number = args->task_number;
	int task_pp_en = args->flags & RKNPU_JOB_PINGPONG ? 1 : 0;
	int pc_data_amount_scale = rknpu_dev->config->pc_data_amount_scale;
	int pc_task_number_bits = rknpu_dev->config->pc_task_number_bits;
	int i = 0;
	int submit_index;
	int max_submit_number = rknpu_dev->config->max_submit_number;
	unsigned long flags;

	if (!task_obj) {
		job->ret = -EINVAL;
		return job->ret;
	}

	if (rknpu_dev->rk3588_core0_banks) {
		struct r6s_task_range chunk;

		job->ret = r6s_rk3588_job_task_chunk(job, core_index, &chunk);
		if (job->ret)
			return job->ret;
		task_start = chunk.start;
		task_number = chunk.number;
	}
	rknpu_core_base = rknpu_dev->base[core_index];
	submit_index = atomic_read(&job->submit_count[core_index]);

	job->ret = r6s_rk3588_pointer_init(rknpu_dev, core_index);
	if (job->ret)
		return job->ret;

	if (!rknpu_dev->rk3588_core0_banks && rknpu_dev->config->num_irqs > 1) {
		for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
			if (i == core_index) {
				REG_WRITE((0xe + 0x10000000 * i), 0x1004);
				REG_WRITE((0xe + 0x10000000 * i), 0x3004);
			}
		}

		switch (job->use_core_num) {
		case 1:
		case 2:
			task_start = args->subcore_task[core_index].task_start;
			task_number =
				args->subcore_task[core_index].task_number;
			break;
		case 3:
			task_start =
				args->subcore_task[core_index + 2].task_start;
			task_number =
				args->subcore_task[core_index + 2].task_number;
			break;
		default:
			LOG_ERROR("Unknown use core num %d\n",
				  job->use_core_num);
			break;
		}
	}

	if (!rknpu_dev->rk3588_core0_banks) {
		task_start = task_start + submit_index * max_submit_number;
		task_number = task_number - submit_index * max_submit_number;
		task_number = task_number > max_submit_number ? max_submit_number :
								task_number;
	}
	task_end = task_start + task_number - 1;

	task_base = task_obj->kv_addr;

	first_task = &task_base[task_start];
	last_task = &task_base[task_end];

	if (rknpu_dev->config->pc_dma_ctrl) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		REG_WRITE(first_task->regcmd_addr, RKNPU_OFFSET_PC_DATA_ADDR);
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	} else {
		REG_WRITE(first_task->regcmd_addr, RKNPU_OFFSET_PC_DATA_ADDR);
	}

	REG_WRITE((first_task->regcfg_amount + RKNPU_PC_DATA_EXTRA_AMOUNT +
		   pc_data_amount_scale - 1) /
				  pc_data_amount_scale -
			  1,
		  RKNPU_OFFSET_PC_DATA_AMOUNT);

	REG_WRITE(last_task->int_mask, RKNPU_OFFSET_INT_MASK);

	REG_WRITE(first_task->int_mask, RKNPU_OFFSET_INT_CLEAR);

	REG_WRITE(((0x6 | task_pp_en) << pc_task_number_bits) | task_number,
		  RKNPU_OFFSET_PC_TASK_CONTROL);

	REG_WRITE(args->task_base_addr, RKNPU_OFFSET_PC_DMA_BASE_ADDR);

	job->first_task = first_task;
	job->last_task = last_task;
	job->int_mask[core_index] = last_task->int_mask;

	r6s_diag_select(job, core_index, task_start, task_number);
	r6s_diag_job_snapshot(job, 0);
	if (rknpu_dev->rk3588_core0_banks) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		r6s_busy_begin(&rknpu_dev->r6s_busy[core_index], ktime_get_ns());
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	}
	REG_WRITE(0x1, RKNPU_OFFSET_PC_OP_EN);
	REG_WRITE(0x0, RKNPU_OFFSET_PC_OP_EN);
	r6s_diag_job_snapshot(job, 1);

	return 0;
}

static inline int rknpu_job_subcore_commit(struct rknpu_job *job,
					   int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_submit *args = job->args;
	void __iomem *rknpu_core_base;
	unsigned long flags;

	if (rknpu_dev->rk3588_core0_banks) {
		struct r6s_task_range chunk;

		job->ret = r6s_rk3588_job_task_chunk(job, core_index, &chunk);
		if (job->ret)
			return job->ret;
	}
	rknpu_core_base = rknpu_dev->base[core_index];

	// switch to slave mode
	if (rknpu_dev->config->pc_dma_ctrl) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		REG_WRITE(0x1, RKNPU_OFFSET_PC_DATA_ADDR);
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	} else {
		REG_WRITE(0x1, RKNPU_OFFSET_PC_DATA_ADDR);
	}

	if (!(args->flags & RKNPU_JOB_PC)) {
		job->ret = -EINVAL;
		return job->ret;
	}

	return rknpu_job_subcore_commit_pc(job, core_index);
}

static void rknpu_job_commit(struct rknpu_job *job)
{
	switch (job->args->core_mask) {
	case RKNPU_CORE0_MASK:
		rknpu_job_subcore_commit(job, 0);
		break;
	case RKNPU_CORE1_MASK:
		rknpu_job_subcore_commit(job, 1);
		break;
	case RKNPU_CORE2_MASK:
		rknpu_job_subcore_commit(job, 2);
		break;
	case RKNPU_CORE0_MASK | RKNPU_CORE1_MASK:
		rknpu_job_subcore_commit(job, 0);
		rknpu_job_subcore_commit(job, 1);
		break;
	case RKNPU_CORE0_MASK | RKNPU_CORE1_MASK | RKNPU_CORE2_MASK:
		rknpu_job_subcore_commit(job, 0);
		rknpu_job_subcore_commit(job, 1);
		rknpu_job_subcore_commit(job, 2);
		break;
	default:
		LOG_ERROR("Unknown core mask: %d\n", job->args->core_mask);
		break;
	}
}

static void rknpu_job_next(struct rknpu_device *rknpu_dev, int core_index)
{
	struct rknpu_job *job = NULL;
	struct rknpu_subcore_data *subcore_data = NULL;
	unsigned long flags;

	if (rknpu_dev->soft_reseting)
		return;

	subcore_data = &rknpu_dev->subcore_datas[core_index];

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);

	if (subcore_data->job || list_empty(&subcore_data->todo_list)) {
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
		return;
	}

	job = list_first_entry(&subcore_data->todo_list, struct rknpu_job,
			       head[core_index]);

	list_del_init(&job->head[core_index]);
	subcore_data->job = job;
	job->hw_commit_time = ktime_get();
	job->hw_recoder_time = job->hw_commit_time;
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	if (atomic_dec_and_test(&job->run_count))
		rknpu_job_commit(job);
}

static void rknpu_job_done(struct rknpu_job *job, int ret, int core_index)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	ktime_t now;
	unsigned long flags;
	int max_submit_number = rknpu_dev->config->max_submit_number;

	if (rknpu_dev->rk3588_core0_banks) {
		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		r6s_busy_end(&rknpu_dev->r6s_busy[core_index], ktime_get_ns(), ret != 0);
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	}
	if (atomic_inc_return(&job->submit_count[core_index]) <
	    (rknpu_get_task_number(job, core_index) + max_submit_number - 1) /
		    max_submit_number) {
		rknpu_job_subcore_commit(job, core_index);
		return;
	}

	subcore_data = &rknpu_dev->subcore_datas[core_index];

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	subcore_data->job = NULL;
	subcore_data->task_num -= rknpu_get_task_number(job, core_index);
	now = ktime_get();
	job->hw_elapse_time = ktime_sub(now, job->hw_commit_time);
	subcore_data->timer.busy_time += ktime_sub(now, job->hw_recoder_time);
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	if (atomic_dec_and_test(&job->interrupt_count)) {
		int use_core_num = job->use_core_num;

		rknpu_iommu_domain_put(rknpu_dev);

		job->flags |= RKNPU_JOB_DONE;
		job->ret = ret;

		if (job->fence)
			dma_fence_signal(job->fence);

		if (job->flags & RKNPU_JOB_ASYNC)
			schedule_work(&job->cleanup_work);

		if (use_core_num > 1)
			wake_up(&(&rknpu_dev->subcore_datas[0])->job_done_wq);
		else
			wake_up(&subcore_data->job_done_wq);
	}

	rknpu_job_next(rknpu_dev, core_index);
}

static int rknpu_schedule_core_index(struct rknpu_device *rknpu_dev)
{
	int core_num = rknpu_dev->config->num_irqs;
	int task_num = rknpu_dev->subcore_datas[0].task_num;
	int core_index = 0;
	int i = 0;

	for (i = 1; i < core_num; i++) {
		if (task_num > rknpu_dev->subcore_datas[i].task_num) {
			core_index = i;
			task_num = rknpu_dev->subcore_datas[i].task_num;
		}
	}

	return core_index;
}

static void rknpu_job_schedule(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	int i = 0, core_index = 0;
	unsigned long flags;

	if (job->args->core_mask == RKNPU_CORE_AUTO_MASK) {
		core_index = rknpu_schedule_core_index(rknpu_dev);
		job->args->core_mask = rknpu_core_mask(core_index);
		job->use_core_num = 1;
		atomic_set(&job->run_count, job->use_core_num);
		atomic_set(&job->interrupt_count, job->use_core_num);
	}

	if (rknpu_iommu_domain_get_and_switch(rknpu_dev, job->iommu_domain_id)) {
		job->ret = -EINVAL;
		return;
	}

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	if (rknpu_dev->rk3588_core0_banks) {
		for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
			int count;
			s64 queued;
			if (!(job->args->core_mask & rknpu_core_mask(i))) continue;
			count = rknpu_get_task_number(job, i);
			queued = rknpu_dev->subcore_datas[i].task_num;
			if (count <= 0 || queued < 0 || count > S64_MAX - queued) {
				job->ret = -EOVERFLOW;
				spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
				return;
			}
		}
	}
	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			list_add_tail(&job->head[i], &subcore_data->todo_list);
			subcore_data->task_num += rknpu_get_task_number(job, i);
		}
	}
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i))
			rknpu_job_next(rknpu_dev, i);
	}
}

static void rknpu_job_abort(struct rknpu_job *job)
{
	struct rknpu_device *rknpu_dev = job->rknpu_dev;
	struct rknpu_subcore_data *subcore_data = NULL;
	unsigned long flags;
	int i = 0;

	rknpu_iommu_domain_put(rknpu_dev);

	msleep(100);

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (job->args->core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			if (job == subcore_data->job && !job->irq_entry[i]) {
				if (rknpu_dev->rk3588_core0_banks)
					r6s_busy_end(&rknpu_dev->r6s_busy[i], ktime_get_ns(), true);
				subcore_data->job = NULL;
				subcore_data->task_num -=
					rknpu_get_task_number(job, i);
			}
		}
	}
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);

	if (job->ret == -ETIMEDOUT) {
		LOG_ERROR("job timeout, flags: %#x:\n", job->flags);
		for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
			if (job->args->core_mask & rknpu_core_mask(i)) {
				void __iomem *rknpu_core_base =
					rknpu_dev->base[i];
				LOG_ERROR(
					"\tcore %d irq status: %#x, raw status: %#x, require mask: %#x, task counter: %#x, elapsed time: %lldus\n",
					i, REG_READ(RKNPU_OFFSET_INT_STATUS),
					REG_READ(RKNPU_OFFSET_INT_RAW_STATUS),
					job->int_mask[i],
					(REG_READ(
						 rknpu_dev->config
							 ->pc_task_status_offset) &
					 rknpu_dev->config->pc_task_number_mask),
					ktime_us_delta(ktime_get(),
						       job->timestamp));
			}
		}
		r6s_diag_job_snapshot(job, 2);
		rknpu_soft_reset(rknpu_dev);
	} else {
		LOG_ERROR(
			"job abort, flags: %#x, ret: %d, elapsed time: %lldus\n",
			job->flags, job->ret,
			ktime_us_delta(ktime_get(), job->timestamp));
	}

	rknpu_job_cleanup(job);
}

static inline uint32_t rknpu_fuzz_status(uint32_t status)
{
	uint32_t fuzz_status = 0;

	if ((status & 0x3) != 0)
		fuzz_status |= 0x3;

	if ((status & 0xc) != 0)
		fuzz_status |= 0xc;

	if ((status & 0x30) != 0)
		fuzz_status |= 0x30;

	if ((status & 0xc0) != 0)
		fuzz_status |= 0xc0;

	if ((status & 0x300) != 0)
		fuzz_status |= 0x300;

	if ((status & 0xc00) != 0)
		fuzz_status |= 0xc00;

	return fuzz_status;
}

static inline irqreturn_t rknpu_irq_handler(int irq, void *data, int core_index)
{
	struct rknpu_device *rknpu_dev = data;
	void __iomem *rknpu_core_base = rknpu_dev->base[core_index];
	struct rknpu_subcore_data *subcore_data = NULL;
	struct rknpu_job *job = NULL;
	uint32_t status = 0;
	unsigned long flags;

	/* The PC IRQ is shared with the IOMMU. An unrelated IRQ must not mark
	 * this job entered, clear registers, or advance its queue. RAW status
	 * and a task counter are not a substitute for the masked DPU IRQ. */
	status = REG_READ(RKNPU_OFFSET_INT_STATUS);
	if (!status)
		return IRQ_NONE;

	subcore_data = &rknpu_dev->subcore_datas[core_index];

	spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
	job = subcore_data->job;
	if (!job) {
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
		REG_WRITE(RKNPU_INT_CLEAR, RKNPU_OFFSET_INT_CLEAR);
		rknpu_job_next(rknpu_dev, core_index);
		return IRQ_HANDLED;
	}
	job->irq_entry[core_index] = true;
	spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);


	job->int_status[core_index] = status;

	if (rknpu_fuzz_status(status) != job->int_mask[core_index]) {
		LOG_ERROR(
			"invalid irq status: %#x, raw status: %#x, require mask: %#x, task counter: %#x\n",
			status, REG_READ(RKNPU_OFFSET_INT_RAW_STATUS),
			job->int_mask[core_index],
			(REG_READ(rknpu_dev->config->pc_task_status_offset) &
			 rknpu_dev->config->pc_task_number_mask));
		REG_WRITE(RKNPU_INT_CLEAR, RKNPU_OFFSET_INT_CLEAR);
		return IRQ_HANDLED;
	}

	REG_WRITE(RKNPU_INT_CLEAR, RKNPU_OFFSET_INT_CLEAR);

	rknpu_job_done(job, 0, core_index);

	return IRQ_HANDLED;
}

irqreturn_t rknpu_core0_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 0);
}

irqreturn_t rknpu_core1_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 1);
}

irqreturn_t rknpu_core2_irq_handler(int irq, void *data)
{
	return rknpu_irq_handler(irq, data, 2);
}

static void rknpu_job_timeout_clean(struct rknpu_device *rknpu_dev,
				    int core_mask)
{
	struct rknpu_job *job = NULL;
	unsigned long flags;
	struct rknpu_subcore_data *subcore_data = NULL;
	int i = 0;

	for (i = 0; i < rknpu_dev->config->num_irqs; i++) {
		if (core_mask & rknpu_core_mask(i)) {
			subcore_data = &rknpu_dev->subcore_datas[i];
			job = subcore_data->job;
			if (job &&
			    ktime_us_delta(ktime_get(), job->timestamp) >=
				    job->args->timeout) {
				rknpu_soft_reset(rknpu_dev);

				spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
				subcore_data->job = NULL;
				spin_unlock_irqrestore(&rknpu_dev->irq_lock,
						       flags);

				do {
					schedule_work(&job->cleanup_work);

					spin_lock_irqsave(&rknpu_dev->irq_lock,
							  flags);

					if (!list_empty(
						    &subcore_data->todo_list)) {
						job = list_first_entry(
							&subcore_data->todo_list,
							struct rknpu_job,
							head[i]);
						list_del_init(&job->head[i]);
					} else {
						job = NULL;
					}

					spin_unlock_irqrestore(
						&rknpu_dev->irq_lock, flags);
				} while (job);
			}
		}
	}
}

static int rknpu_submit(struct rknpu_device *rknpu_dev,
			struct rknpu_submit *args)
{
	struct rknpu_job *job = NULL;
	int ret = -EINVAL;

	if (args->task_number == 0) {
		LOG_ERROR("invalid rknpu task number!\n");
		return -EINVAL;
	}

	if (args->core_mask > rknpu_dev->config->core_mask) {
		LOG_ERROR("invalid rknpu core mask: %#x", args->core_mask);
		return -EINVAL;
	}

	job = rknpu_job_alloc(rknpu_dev, args);
	if (!job) {
		LOG_ERROR("failed to allocate rknpu job!\n");
		return -ENOMEM;
	}

	if (rknpu_dev->rk3588_core0_banks) {
		struct r6s_task_range range;
		int core;
		if (!args->core_mask) { rknpu_job_free(job); return -EINVAL; }
		for (core = 0; core < rknpu_dev->config->num_irqs; core++) {
			if (!(args->core_mask & rknpu_core_mask(core))) continue;
			ret = r6s_rk3588_job_task_range(job, core, &range);
			if (ret) { rknpu_job_free(job); return ret; }
		}
	}
	if (args->flags & RKNPU_JOB_FENCE_IN) {
#ifdef CONFIG_ROCKCHIP_RKNPU_FENCE
		struct dma_fence *in_fence;

		in_fence = sync_file_get_fence(args->fence_fd);

		if (!in_fence) {
			LOG_ERROR("invalid fence in fd, fd: %d\n",
				  args->fence_fd);
			return -EINVAL;
		}
		args->fence_fd = -1;

		/*
		 * Wait if the fence is from a foreign context, or if the fence
		 * array contains any fence from a foreign context.
		 */
		ret = 0;
		if (!dma_fence_match_context(in_fence,
					     rknpu_dev->fence_ctx->context))
			ret = dma_fence_wait_timeout(in_fence, true,
						     args->timeout);
		dma_fence_put(in_fence);
		if (ret < 0) {
			if (ret != -ERESTARTSYS)
				LOG_ERROR("Error (%d) waiting for fence!\n",
					  ret);

			return ret;
		}
#else
		LOG_ERROR(
			"failed to use rknpu fence, please enable rknpu fence config!\n");
		rknpu_job_free(job);
		return -EINVAL;
#endif
	}

	if (args->flags & RKNPU_JOB_FENCE_OUT) {
#ifdef CONFIG_ROCKCHIP_RKNPU_FENCE
		ret = rknpu_fence_alloc(job);
		if (ret) {
			rknpu_job_free(job);
			return ret;
		}
		job->args->fence_fd = rknpu_fence_get_fd(job);
		args->fence_fd = job->args->fence_fd;
#else
		LOG_ERROR(
			"failed to use rknpu fence, please enable rknpu fence config!\n");
		rknpu_job_free(job);
		return -EINVAL;
#endif
	}

	if (args->flags & RKNPU_JOB_NONBLOCK) {
		job->flags |= RKNPU_JOB_ASYNC;
		rknpu_job_timeout_clean(rknpu_dev, job->args->core_mask);
		rknpu_job_schedule(job);
		ret = job->ret;
		if (ret) {
			rknpu_job_abort(job);
			return ret;
		}
	} else {
		rknpu_job_schedule(job);
		if ((args->flags & RKNPU_JOB_PC) &&
		    !(rknpu_dev->rk3588_core0_banks && job->ret))
			job->ret = rknpu_job_wait(job);

		args->task_counter = job->args->task_counter;
		ret = job->ret;
		if (!ret)
			rknpu_job_cleanup(job);
		else
			rknpu_job_abort(job);
	}

	return ret;
}

#ifdef CONFIG_ROCKCHIP_RKNPU_DRM_GEM
int rknpu_submit_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct rknpu_device *rknpu_dev = dev_get_drvdata(dev->dev);
	struct rknpu_submit *args = data;

	return rknpu_submit(rknpu_dev, args);
}
#endif

#ifdef CONFIG_ROCKCHIP_RKNPU_DMA_HEAP
int rknpu_submit_ioctl(struct rknpu_device *rknpu_dev, unsigned long data)
{
	struct rknpu_submit args;
	int ret = -EINVAL;

	if (unlikely(copy_from_user(&args, (struct rknpu_submit *)data,
				    sizeof(struct rknpu_submit)))) {
		LOG_ERROR("%s: copy_from_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	ret = rknpu_submit(rknpu_dev, &args);

	if (unlikely(copy_to_user((struct rknpu_submit *)data, &args,
				  sizeof(struct rknpu_submit)))) {
		LOG_ERROR("%s: copy_to_user failed\n", __func__);
		ret = -EFAULT;
		return ret;
	}

	return ret;
}
#endif

int rknpu_get_hw_version(struct rknpu_device *rknpu_dev, uint32_t *version)
{
	void __iomem *rknpu_core_base = rknpu_dev->base[0];

	if (version == NULL)
		return -EINVAL;

	*version = REG_READ(RKNPU_OFFSET_VERSION) +
		   (REG_READ(RKNPU_OFFSET_VERSION_NUM) & 0xffff);

	return 0;
}

int rknpu_get_bw_priority(struct rknpu_device *rknpu_dev, uint32_t *priority,
			  uint32_t *expect, uint32_t *tw)
{
	void __iomem *base = rknpu_dev->bw_priority_base;

	if (!base)
		return -EINVAL;

	spin_lock(&rknpu_dev->lock);

	if (priority != NULL)
		*priority = _REG_READ(base, 0x0);

	if (expect != NULL)
		*expect = _REG_READ(base, 0x8);

	if (tw != NULL)
		*tw = _REG_READ(base, 0xc);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_set_bw_priority(struct rknpu_device *rknpu_dev, uint32_t priority,
			  uint32_t expect, uint32_t tw)
{
	void __iomem *base = rknpu_dev->bw_priority_base;

	if (!base)
		return -EINVAL;

	spin_lock(&rknpu_dev->lock);

	if (priority != 0)
		_REG_WRITE(base, priority, 0x0);

	if (expect != 0)
		_REG_WRITE(base, expect, 0x8);

	if (tw != 0)
		_REG_WRITE(base, tw, 0xc);

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_clear_rw_amount(struct rknpu_device *rknpu_dev)
{
	void __iomem *rknpu_core_base = rknpu_dev->base[0];
	const struct rknpu_config *config = rknpu_dev->config;
	unsigned long flags;

	if (config->amount_top == NULL) {
		LOG_WARN("Clear rw_amount is not supported on this device!\n");
		return 0;
	}

	if (config->pc_dma_ctrl) {
		uint32_t pc_data_addr = 0;

		spin_lock_irqsave(&rknpu_dev->irq_lock, flags);
		pc_data_addr = REG_READ(RKNPU_OFFSET_PC_DATA_ADDR);

		REG_WRITE(0x1, RKNPU_OFFSET_PC_DATA_ADDR);
		REG_WRITE(0x80000101, config->amount_top->offset_clr_all);
		REG_WRITE(0x00000101, config->amount_top->offset_clr_all);
		if (config->amount_core) {
			REG_WRITE(0x80000101,
				  config->amount_core->offset_clr_all);
			REG_WRITE(0x00000101,
				  config->amount_core->offset_clr_all);
		}
		REG_WRITE(pc_data_addr, RKNPU_OFFSET_PC_DATA_ADDR);
		spin_unlock_irqrestore(&rknpu_dev->irq_lock, flags);
	} else {
		spin_lock(&rknpu_dev->lock);
		REG_WRITE(0x80000101, config->amount_top->offset_clr_all);
		REG_WRITE(0x00000101, config->amount_top->offset_clr_all);
		if (config->amount_core) {
			REG_WRITE(0x80000101,
				  config->amount_core->offset_clr_all);
			REG_WRITE(0x00000101,
				  config->amount_core->offset_clr_all);
		}
		spin_unlock(&rknpu_dev->lock);
	}

	return 0;
}

int rknpu_get_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *dt_wr,
			uint32_t *dt_rd, uint32_t *wd_rd)
{
	void __iomem *rknpu_core_base = rknpu_dev->base[0];
	const struct rknpu_config *config = rknpu_dev->config;
	int amount_scale = config->pc_data_amount_scale;

	if (config->amount_top == NULL) {
		LOG_WARN("Get rw_amount is not supported on this device!\n");
		return 0;
	}

	spin_lock(&rknpu_dev->lock);

	if (dt_wr != NULL) {
		*dt_wr = REG_READ(config->amount_top->offset_dt_wr) *
			 amount_scale;
		if (config->amount_core) {
			*dt_wr += REG_READ(config->amount_core->offset_dt_wr) *
				  amount_scale;
		}
	}

	if (dt_rd != NULL) {
		*dt_rd = REG_READ(config->amount_top->offset_dt_rd) *
			 amount_scale;
		if (config->amount_core) {
			*dt_rd += REG_READ(config->amount_core->offset_dt_rd) *
				  amount_scale;
		}
	}

	if (wd_rd != NULL) {
		*wd_rd = REG_READ(config->amount_top->offset_wt_rd) *
			 amount_scale;
		if (config->amount_core) {
			*wd_rd += REG_READ(config->amount_core->offset_wt_rd) *
				  amount_scale;
		}
	}

	spin_unlock(&rknpu_dev->lock);

	return 0;
}

int rknpu_get_total_rw_amount(struct rknpu_device *rknpu_dev, uint32_t *amount)
{
	const struct rknpu_config *config = rknpu_dev->config;
	uint32_t dt_wr = 0;
	uint32_t dt_rd = 0;
	uint32_t wd_rd = 0;
	int ret = -EINVAL;

	if (config->amount_top == NULL) {
		LOG_WARN(
			"Get total_rw_amount is not supported on this device!\n");
		return 0;
	}

	ret = rknpu_get_rw_amount(rknpu_dev, &dt_wr, &dt_rd, &wd_rd);

	if (amount != NULL)
		*amount = dt_wr + dt_rd + wd_rd;

	return ret;
}
