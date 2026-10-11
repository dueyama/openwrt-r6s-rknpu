/* SPDX-License-Identifier: GPL-2.0-only */
/* Synthetic boundary tests; no observed submissions or model data. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "rknpu_ioctl.h"
#include "r6s_task_range.h"

static unsigned checks;
static void expect(int actual, int expected)
{
    assert(actual == expected);
    ++checks;
}

int main(void)
{
    struct rknpu_submit a;
    struct r6s_task_range r, chunk;
    const unsigned masks[] = {1, 2, 4, 3, 7};
    const u64 bytes = 10000 * sizeof(struct rknpu_task);
    memset(&a, 0, sizeof(a));
    a.flags = RKNPU_JOB_PC;
    a.task_start = 10;
    a.task_number = 6;
    for (unsigned i = 0; i < 5; ++i) {
        a.subcore_task[i].task_start = 20 + i * 10;
        a.subcore_task[i].task_number = 2;
    }
    for (unsigned m = 0; m < sizeof(masks)/sizeof(masks[0]); ++m) {
        a.core_mask = masks[m];
        int cores = (a.core_mask & 1) + ((a.core_mask >> 1) & 1) + ((a.core_mask >> 2) & 1);
        for (int core = 0; core < 3; ++core) {
            int expected = a.core_mask & (1U << core) ? 0 : -EINVAL;
            expect(r6s_rk3588_task_range(&a, bytes, true, core, cores, 3, 7, 4095, &r), expected);
            if (!expected) {
                assert(r.start == (int)a.subcore_task[cores == 3 ? core+2 : core].task_start);
                for (unsigned bad = 0; bad < 4; ++bad) {
                    struct rknpu_submit b = a;
                    unsigned slot = cores == 3 ? core + 2 : core;
                    switch (bad) {
                    case 0: b.subcore_task[slot].task_number = 0; break;
                    case 1: b.subcore_task[slot].task_start = 10000; break;
                    case 2: b.subcore_task[slot].task_start = INT_MAX; break;
                    case 3: b.subcore_task[slot].task_start = UINT32_MAX; b.subcore_task[slot].task_number = UINT32_MAX; break;
                    }
                    expect(r6s_rk3588_task_range(&b, bytes, true, core, cores, 3, 7, 4095, &r), -EINVAL);
                }
            }
        }
    }
    for (unsigned mask = 0; mask < 9; ++mask) {
        if (mask == 1 || mask == 2 || mask == 4 || mask == 3 || mask == 7) continue;
        a.core_mask = mask;
        expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 3, 7, 4095, &r), -EINVAL);
    }
    a.core_mask = 1;
    /* Legacy cfg1 still requires containment in the global parent interval. */
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 1, 1, 4095, &r), -EINVAL);
    a.subcore_task[0].task_start = 10;
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 1, 1, 4095, &r), 0);
    expect(r6s_rk3588_task_range(&a, bytes, false, 0, 1, 3, 7, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, -1, 1, 3, 7, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, 3, 1, 3, 7, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 2, 3, 7, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 2, 7, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 3, 1, 4095, &r), -EINVAL);
    expect(r6s_rk3588_task_range(&a, bytes, true, 0, 1, 3, 7, 0, &r), -EINVAL);
    a.subcore_task[0].task_start = 9999;
    a.subcore_task[0].task_number = 1;
    expect(r6s_rk3588_task_range(&a, bytes + 39, true, 0, 1, 3, 7, 4095, &r), 0);
    a.subcore_task[0].task_number = 2;
    expect(r6s_rk3588_task_range(&a, bytes + 39, true, 0, 1, 3, 7, 4095, &r), -EINVAL);
    r = (struct r6s_task_range){100, 5000};
    expect(r6s_task_range_chunk(&r, 0, 4095, &chunk), 0);
    assert(chunk.start == 100 && chunk.number == 4095);
    expect(r6s_task_range_chunk(&r, 1, 4095, &chunk), 0);
    assert(chunk.start == 4195 && chunk.number == 905);
    expect(r6s_task_range_chunk(&r, 2, 4095, &chunk), -EINVAL);
    expect(r6s_task_range_chunk(&r, -1, 4095, &chunk), -EINVAL);
    expect(r6s_task_range_chunk(&r, INT_MAX, INT_MAX, &chunk), -EINVAL);
    printf("PASS: %u synthetic range checks\n", checks);
    return 0;
}
