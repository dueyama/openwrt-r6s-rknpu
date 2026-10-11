/* SPDX-License-Identifier: GPL-2.0-only */
/* Host model of kernel services, not an NPU or DMA hardware emulator. */
#pragma once
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

typedef uint8_t __u8; typedef int8_t __s8;
typedef uint16_t __u16; typedef int16_t __s16;
typedef uint32_t __u32; typedef int32_t __s32;
typedef uint64_t __u64; typedef int64_t __s64;
typedef uint32_t u32; typedef int64_t s64; typedef uint64_t u64;
typedef int64_t ktime_t;
typedef struct { int value; } atomic_t;
typedef atomic_t refcount_t;
typedef int spinlock_t;
typedef int irqreturn_t;
#define __iomem
#define CONFIG_ROCKCHIP_RKNPU_DRM_GEM 1
#define CONFIG_ROCKCHIP_RKNPU_FENCE 1
#define GFP_KERNEL 0
#define ERESTARTSYS 512
#define S64_MAX INT64_MAX
#define IRQ_NONE 0
#define IRQ_HANDLED 1
static void host_log(const char *format, ...) {}
#define LOG_ERROR(...) host_log(__VA_ARGS__)
#define LOG_DEBUG(...) ((void)0)
#define LOG_DEV_WARN(...) ((void)0)
#define LOG_DEV_ERROR(...) ((void)0)
#define READ_ONCE(x) (x)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
static void (*before_lock)(spinlock_t *);
static void spin_lock_init(spinlock_t *lock) { *lock = 0; }
static void lock_host(spinlock_t *lock) {
    if (before_lock) before_lock(lock);
    assert(*lock == 0); *lock = 1;
}
static void unlock_host(spinlock_t *lock) { assert(*lock == 1); *lock = 0; }
#define spin_lock_irqsave(lock,flags) do { (flags)=0; lock_host(lock); } while (0)
#define spin_unlock_irqrestore(lock,flags) do { (void)(flags); unlock_host(lock); } while (0)
static void atomic_set(atomic_t *a,int v) { a->value=v; }
static int atomic_read(const atomic_t *a) { return a->value; }
static bool atomic_dec_and_test(atomic_t *a) { return --a->value == 0; }
static int atomic_inc_return(atomic_t *a) { return ++a->value; }
static void refcount_set(refcount_t *a,int v) { a->value=v; }
static void refcount_inc(refcount_t *a) { assert(a->value > 0); ++a->value; }
static bool refcount_dec_and_test(refcount_t *a) { assert(a->value > 0); return --a->value == 0; }
struct list_head { struct list_head *next, *prev; };
static void INIT_LIST_HEAD(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(const struct list_head *h) { return h->next == h; }
static void list_add_tail(struct list_head *n,struct list_head *h) {
    n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n;
}
static void list_del_init(struct list_head *n) {
    n->next->prev=n->prev; n->prev->next=n->next; INIT_LIST_HEAD(n);
}
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
struct work_struct { void (*run)(struct work_struct *); bool queued; };
static struct work_struct *works[128]; static int nwork;
#define INIT_WORK(w,fn) do { (w)->run=(fn); (w)->queued=false; } while(0)
static void schedule_work(struct work_struct *w) {
    assert(!w->queued && nwork < 128); w->queued=true; works[nwork++]=w;
}
static void flush_work(void) { while(nwork) { struct work_struct *w=works[--nwork]; w->queued=false; w->run(w); } }
static int live_allocs, alloc_calls, fail_alloc_at;
static void *host_alloc(size_t n) {
    if (++alloc_calls == fail_alloc_at) return NULL;
    void *p=calloc(1,n); assert(p); ++live_allocs; return p;
}
static void *kzalloc(size_t n,int flags) { return host_alloc(n); }
static void *kmemdup(const void *p,size_t n,int flags) { void *q=host_alloc(n); if(q) memcpy(q,p,n); return q; }
static void kfree(void *p) { if(p) { assert(live_allocs>0); --live_allocs; free(p); } }
struct dma_fence { int refs, error, signals; bool foreign; };
static struct dma_fence input_fence;
static bool valid_fence=true;
static long fence_wait_result=1, fence_wait_ticks;
static int fence_alloc_error, fence_fd_error;
static void dma_fence_put(struct dma_fence *f) { assert(f->refs>0); --f->refs; }
static void dma_fence_set_error(struct dma_fence *f,int error) { assert(!f->signals); f->error=error; }
static void dma_fence_signal(struct dma_fence *f) { assert(!f->signals); ++f->signals; }
static struct dma_fence *sync_file_get_fence(int fd) {
    if(!valid_fence) return NULL;
    ++input_fence.refs; return &input_fence;
}
static bool dma_fence_match_context(struct dma_fence *f,int context) { return !f->foreign; }
/* Deliberately use HZ=250 so passing milliseconds as jiffies is observable. */
static unsigned host_hz=250;
static unsigned long msecs_to_jiffies(uint32_t ms) { return ((uint64_t)ms*host_hz+999)/1000; }
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,long ticks) { fence_wait_ticks=ticks; return fence_wait_result; }
static ktime_t now_us;
static ktime_t ktime_get(void) { return now_us; }
static uint64_t ktime_get_ns(void) { return now_us*1000; }
static int64_t ktime_us_delta(ktime_t a,ktime_t b) { return a-b; }
static ktime_t ktime_sub(ktime_t a,ktime_t b) { return a-b; }
static void wake_up(int *wq) { ++*wq; }
struct drm_device { void *dev; void *dev_private; };
struct drm_gem_object { struct drm_device *dev; int refs; };
struct sg_table; struct page;
struct rknpu_gem_object {
    struct drm_gem_object base; unsigned int flags; unsigned long size;
    void *cookie, *kv_addr; uint64_t dma_addr; unsigned long dma_attrs, num_pages;
    struct page **pages; struct sg_table *sgt;
};
static void rknpu_gem_object_get(struct drm_gem_object *o) { ++o->refs; }
static void rknpu_gem_object_put(struct drm_gem_object *o) { assert(o->refs>0); --o->refs; }
struct drm_file;
#include "rknpu_job.h"
struct r6s_task_range { int unused; };
struct rknpu_config { int num_irqs, max_submit_number, pc_task_status_offset, pc_task_number_mask; unsigned core_mask; uint64_t dma_mask; };
struct rknpu_subcore_data {
    struct list_head todo_list; int job_done_wq; struct rknpu_job *job;
    int64_t task_num; struct { ktime_t busy_time; } timer;
};
struct rknpu_fence_context { int context; };
struct rknpu_device {
    struct rknpu_config *config; struct rknpu_subcore_data subcore_datas[3];
    spinlock_t irq_lock; bool rk3588_core0_banks, soft_reseting, iommu_en;
    int r6s_busy[3], domain_refs, resets, commits;
    void *base[3]; struct rknpu_fence_context *fence_ctx;
};
static int domain_error, commit_error, wait_result, range_error;
static void r6s_busy_end(int *busy,uint64_t time,bool failed) { ++*busy; }
static int rknpu_get_task_number(struct rknpu_job *j,int core) { return j->args->subcore_task[core].task_number; }
static int rknpu_iommu_domain_get_and_switch(struct rknpu_device *d,int id) { if(domain_error)return domain_error; ++d->domain_refs; return 0; }
static void rknpu_iommu_domain_put(struct rknpu_device *d) { assert(d->domain_refs>0); --d->domain_refs; }
static void r6s_diag_job_snapshot(struct rknpu_job *j,unsigned stage) {}
static int rknpu_soft_reset(struct rknpu_device *d) { ++d->resets; return 0; }
static void rknpu_job_commit(struct rknpu_job *j) { ++j->rknpu_dev->commits; j->ret=commit_error; }
static int rknpu_job_subcore_commit(struct rknpu_job *j,int core) { rknpu_job_commit(j); return j->ret; }
static int rknpu_job_wait(struct rknpu_job *j) { return wait_result; }
static int r6s_rk3588_job_task_range(struct rknpu_job *j,int core,struct r6s_task_range *r) { return range_error; }
static struct dma_fence output_fence;
static int rknpu_fence_alloc(struct rknpu_job *j) { if(fence_alloc_error)return fence_alloc_error; output_fence.refs=1; j->fence=&output_fence; return 0; }
int rknpu_fence_get_fd(struct rknpu_job *j);
#define REG_READ(offset) (((uint32_t *)rknpu_core_base)[(offset)/4])
#define REG_WRITE(value,offset) (((uint32_t *)rknpu_core_base)[(offset)/4]=(value))
/* DMA allocator and page allocator have deliberately distinct release APIs. */
typedef unsigned gfp_t;
#define DMA_ATTR_FORCE_CONTIGUOUS 1
#define DMA_ATTR_WRITE_COMBINE 2
#define DMA_ATTR_NO_KERNEL_MAPPING 4
#define __GFP_ZERO 1
#define __GFP_HIGHMEM 2
#define __GFP_DMA32 4
#define DMA_BIT_MASK(n) ((1ULL<<(n))-1)
#define PAGE_SHIFT 12
#define KERNEL_VERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6,12,112)
struct page { int unused; };
struct scatterlist { uint64_t dma_address; unsigned length; };
struct sg_table { struct scatterlist *sgl; unsigned nents; };
#define for_each_sg(head,s,n,i) for((i)=0,(s)=(head);(i)<(int)(n);++(i),++(s))
#define sg_dma_address(s) ((s)->dma_address)
#define sg_phys(s) ((s)->dma_address)
static int dma_calls,dma_failures,dma_frees,page_gets,page_puts,page_error,sg_error,array_error;
static struct scatterlist scatter;
static void *rknpu_gem_alloc_page(size_t n) { return host_alloc(n*sizeof(struct page *)); }
static void rknpu_gem_free_page(void *p) { kfree(p); }
static void *dma_alloc_attrs(void *dev,size_t size,uint64_t *addr,gfp_t gfp,unsigned long attrs) {
    ++dma_calls; if(dma_calls<=dma_failures)return NULL;
    void *p=host_alloc(size); if(p)*addr=0x1000; return p;
}
static void dma_free_attrs(void *dev,size_t size,void *cookie,uint64_t addr,unsigned long attrs) { assert(cookie); ++dma_frees; kfree(cookie); }
static int dma_get_sgtable_attrs(void *dev,struct sg_table *s,void *cookie,uint64_t addr,size_t size,unsigned long attrs) {
    if(sg_error)return sg_error;
    s->sgl=&scatter; s->nents=1; scatter.dma_address=addr; return 0;
}
static int drm_prime_sg_to_page_array(struct sg_table *sg,struct page **pages,unsigned n) { return array_error; }
static void sg_free_table(struct sg_table *sg) { assert(sg); }
/* These two service stubs validate allocator selection and pairing. They do
 * not model shmem, an IOMMU mapping or cache coherency. */
static int rknpu_gem_get_pages(struct rknpu_gem_object *o) {
    ++page_gets; assert(o->pages==NULL); if(page_error)return page_error;
    o->pages=rknpu_gem_alloc_page(o->size>>PAGE_SHIFT); assert(o->pages);
    o->dma_addr=0x2000; return 0;
}
static void rknpu_gem_put_pages(struct rknpu_gem_object *o) {
    assert(page_gets>page_puts); ++page_puts; assert(o->pages); kfree(o->pages);
}
#define O_CLOEXEC 0
struct sync_file { void *file; };
static struct sync_file syncfile;
static int fd_live;
static bool syncfile_fail;
static int get_unused_fd_flags(int flags) { if(fence_fd_error)return fence_fd_error; ++fd_live; return 5; }
static void put_unused_fd(int fd) { assert(fd_live==1); --fd_live; }
static struct sync_file *sync_file_create(struct dma_fence *f) { return syncfile_fail ? NULL : &syncfile; }
static void fd_install(int fd,void *file) {}
