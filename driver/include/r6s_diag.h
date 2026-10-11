/* SPDX-License-Identifier: GPL-2.0-only */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/* Bounded, non-destructive RK3588 core0 snapshots from already-owned banks.
 * Offsets: Rockchip TRM ch36 / Linux v6.18 rocket_registers.h. No command,
 * task-buffer, model, input/output, IOMMU or unreserved-bank reads. */
#ifndef R6S_NPU_DIAG_H
#define R6S_NPU_DIAG_H

struct r6s_diag_snapshot {
    u32 pc_mask, pc_status, pc_raw, pc_task;
    u32 cna_status, cna_pointer, core_status, core_pointer;
};

static inline bool r6s_diag_can_read(struct rknpu_device *d)
{
    return d && d->rk3588_core0_banks && d->config &&
        d->config->num_irqs == 1 && d->base[0] &&
        d->cna_base[0] && d->core_base[0] && d->dev &&
        atomic_read(&d->power_refcount) > 0 && pm_runtime_active(d->dev);
}

static inline bool r6s_diag_read(struct rknpu_device *d,
                               struct r6s_diag_snapshot *s)
{
    if (!s || !r6s_diag_can_read(d))
        return false;
    s->pc_mask = readl(d->base[0] + 0x20);
    s->pc_status = readl(d->base[0] + 0x28);
    s->pc_raw = readl(d->base[0] + 0x2c);
    s->pc_task = readl(d->base[0] + 0x3c);
    s->cna_status = readl(d->cna_base[0] + 0x0);
    s->cna_pointer = readl(d->cna_base[0] + 0x4);
    s->core_status = readl(d->core_base[0] + 0x0);
    s->core_pointer = readl(d->core_base[0] + 0x4);
    return true;
}
#endif
