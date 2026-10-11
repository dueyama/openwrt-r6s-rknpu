/* SPDX-License-Identifier: GPL-2.0 */
/* R6S Linux 6.12 port; modified 2026-10-11. See docs/PORTING.md. */
#ifndef R6S_BUSY_H
#define R6S_BUSY_H
#include <linux/types.h>
struct r6s_busy_counter { u64 total_ns, start_ns, starts, finishes, aborts; bool active; };
/* All callers hold irq_lock; read snapshots never change the counter. */
static inline void r6s_busy_begin(struct r6s_busy_counter *b, u64 now)
{ if (!b->active) { b->active = true; b->start_ns = now; b->starts++; } }
static inline void r6s_busy_end(struct r6s_busy_counter *b, u64 now, bool abort)
{ if (b->active) { if (now >= b->start_ns) b->total_ns += now-b->start_ns;
  b->active = false; if (abort) b->aborts++; else b->finishes++; } }
static inline u64 r6s_busy_read(const struct r6s_busy_counter *b, u64 now)
{ return b->total_ns + (b->active && now >= b->start_ns ? now-b->start_ns : 0); }
#endif
