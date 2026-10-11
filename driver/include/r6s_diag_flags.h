/* SPDX-License-Identifier: GPL-2.0-only */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
/* Classification only: does not add/remove SDK flags, mappings or sync. */
#ifndef R6S_DIAG_FLAGS_H
#define R6S_DIAG_FLAGS_H
static inline unsigned int r6s_diag_bo_class(unsigned int flags)
{
    return ((flags & RKNPU_MEM_CACHEABLE) ? 2U : 0U) |
           ((flags & RKNPU_MEM_KERNEL_MAPPING) ? 1U : 0U);
}
#endif
