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
#include <stdatomic.h>
#include <sched.h>

typedef uint8_t __u8; typedef int8_t __s8;
typedef uint16_t __u16; typedef int16_t __s16;
typedef uint32_t __u32; typedef int32_t __s32;
typedef uint64_t __u64; typedef int64_t __s64;
typedef uint32_t u32; typedef int64_t s64; typedef uint64_t u64;
typedef int64_t ktime_t;
typedef struct { _Atomic int value; } atomic_t;
typedef atomic_t refcount_t;
typedef _Atomic int spinlock_t;
struct mutex { _Atomic int locked; };
typedef int irqreturn_t;
#define __iomem
#define CONFIG_ROCKCHIP_RKNPU_DRM_GEM 1
#define CONFIG_ROCKCHIP_RKNPU_FENCE 1
#define GFP_KERNEL 0
#define ERESTARTSYS 512
#define ENOTSUPP 524
#define S64_MAX INT64_MAX
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define RKNPU_OFFSET_INT_MASK 0x20
static void host_log(const char *format, ...) {}
#define LOG_ERROR(...) host_log(__VA_ARGS__)
#define LOG_WARN(...) host_log(__VA_ARGS__)
#define LOG_INFO(...) host_log(__VA_ARGS__)
#define LOG_DEBUG(...) host_log(__VA_ARGS__)
#define LOG_DEV_WARN(...) ((void)0)
#define LOG_DEV_ERROR(...) ((void)0)
#define READ_ONCE(x) (x)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
static void (*before_lock)(spinlock_t *);
static void (*after_unlock)(spinlock_t *);
static void spin_lock_init(spinlock_t *lock) { *lock = 0; }
static void lock_host(spinlock_t *lock) {
    if (before_lock) before_lock(lock);
    #ifdef RKNPU_HOST_THREADS
    while(atomic_exchange(lock,1))sched_yield();
#else
    assert(*lock == 0); *lock = 1;
#endif
}
static void unlock_host(spinlock_t *lock) { assert(*lock == 1); *lock = 0; if(after_unlock)after_unlock(lock); }
static void mutex_lock(struct mutex *m) {
#ifdef RKNPU_HOST_THREADS
    while(atomic_exchange(&m->locked,1))sched_yield();
#else
    assert(!m->locked); m->locked=1;
#endif
}
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked=0; }
static void (*wait_event_hook)(void);
#define wait_event(wq,condition) do { if(!(condition) && wait_event_hook)wait_event_hook(); assert(condition); } while(0)
static int module_refs;
#define THIS_MODULE NULL
static void __module_get(void *m) { ++module_refs; }
static void module_put(void *m) { assert(module_refs>0); --module_refs; }
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
static void (*fence_signal_hook)(void);
static void dma_fence_signal(struct dma_fence *f) { if(fence_signal_hook)fence_signal_hook(); assert(!f->signals); ++f->signals; }
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
typedef unsigned gfp_t;
struct address_space { gfp_t gfp; };
struct file { struct address_space *f_mapping; };
struct drm_gem_object { struct drm_device *dev; int refs; unsigned long size; struct file *filp; const void *funcs; };
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
struct rknpu_device;
struct rknpu_config { void (*state_init)(struct rknpu_device *); int num_irqs, max_submit_number, pc_task_status_offset, pc_task_number_mask; unsigned core_mask; uint64_t dma_mask; };
struct rknpu_subcore_data {
    struct list_head todo_list; int job_done_wq; struct rknpu_job *job;
    int64_t task_num; struct { ktime_t busy_time; } timer;
};
struct rknpu_fence_context { int context; };
struct rknpu_device {
    struct rknpu_config *config; struct rknpu_subcore_data subcore_datas[3];
    spinlock_t irq_lock, dispatch_lock; struct mutex reset_lock, domain_lock;
    bool rk3588_core0_banks, soft_reseting, iommu_en, reset_failed, recovery_power_ref, recovery_module_ref;
    void *dev; int bypass_soft_reset, num_srsts, power_refs;
    struct reset_control **srsts; struct rknpu_job *hardware[3];
    int r6s_busy[3], domain_refs, resets, commits;
    void *base[3]; struct rknpu_fence_context *fence_ctx;
};
static int domain_error, commit_error, wait_result, range_error;
static void r6s_busy_end(int *busy,uint64_t time,bool failed) { ++*busy; }
static int rknpu_get_task_number(struct rknpu_job *j,int core) { return j->args->subcore_task[core].task_number; }
static int rknpu_iommu_domain_get_and_switch(struct rknpu_device *d,int id) { if(domain_error)return domain_error; ++d->domain_refs; return 0; }
static void rknpu_iommu_domain_put(struct rknpu_device *d) { assert(d->domain_refs>0); --d->domain_refs; }
static void r6s_diag_job_snapshot(struct rknpu_job *j,unsigned stage) {}
int rknpu_soft_reset(struct rknpu_device *d);
static int power_error;
static int rknpu_power_get(struct rknpu_device *d) { if(power_error)return power_error; ++d->power_refs; return 0; }
static void rknpu_power_put_delay(struct rknpu_device *d) { assert(d->power_refs>0); --d->power_refs; }
static void rknpu_job_commit(struct rknpu_job *j) {
    struct rknpu_device *d=j->rknpu_dev;
    assert(d->dispatch_lock && !d->soft_reseting); ++d->commits; j->ret=commit_error;
    if(!commit_error)for(int i=0;i<3;i++)if(j->args->core_mask&(1u<<i)) { assert(d->subcore_datas[i].job==j); d->hardware[i]=j; }
}
static int rknpu_job_subcore_commit(struct rknpu_job *j,int core) {
    assert(j->rknpu_dev->dispatch_lock && !j->rknpu_dev->soft_reseting);
    ++j->rknpu_dev->commits; j->rknpu_dev->hardware[core]=j; return 0;
}
static int rknpu_job_wait(struct rknpu_job *j) { return wait_result; }
static int r6s_rk3588_job_task_range(struct rknpu_job *j,int core,struct r6s_task_range *r) { return range_error; }
static struct dma_fence output_fence;
static int rknpu_fence_alloc(struct rknpu_job *j) { if(fence_alloc_error)return fence_alloc_error; output_fence.refs=1; j->fence=&output_fence; return 0; }
int rknpu_fence_get_fd(struct rknpu_job *j);
#define REG_READ(offset) (((volatile uint32_t *)rknpu_core_base)[(offset)/4])
static bool stuck_irq, stuck_raw_irq;
#define REG_WRITE(value,offset) do { uint32_t *regs=rknpu_core_base; regs[(offset)/4]=(value); if((offset)==RKNPU_OFFSET_INT_CLEAR) { if(!stuck_irq)regs[RKNPU_OFFSET_INT_STATUS/4]=0; if(!stuck_raw_irq)regs[RKNPU_OFFSET_INT_RAW_STATUS/4]=0; } } while(0)
/* DMA allocator and page allocator have deliberately distinct release APIs. */
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
#define sg_virt(s) ((void *)(uintptr_t)(s)->dma_address)
static int dma_calls,dma_failures,dma_frees,page_gets,page_puts,page_error,sg_error,array_error;
static struct scatterlist scatter;
static void *rknpu_gem_alloc_page(size_t n) { return host_alloc(n*sizeof(struct page *)); }
static void rknpu_gem_free_page(void *p) { kfree(p); }
static gfp_t dma_gfp, page_gfp;
static void *dma_alloc_attrs(void *dev,size_t size,uint64_t *addr,gfp_t gfp,unsigned long attrs) {
    dma_gfp=gfp; ++dma_calls; if(dma_calls<=dma_failures)return NULL;
    void *p=host_alloc(size); if(p)*addr=0x1000; return p;
}
static void dma_free_attrs(void *dev,size_t size,void *cookie,uint64_t addr,unsigned long attrs) { assert(cookie); ++dma_frees; kfree(cookie); }
static int dma_get_sgtable_attrs(void *dev,struct sg_table *s,void *cookie,uint64_t addr,size_t size,unsigned long attrs) {
    if(sg_error)return sg_error;
    s->sgl=&scatter; s->nents=1; scatter.dma_address=addr; return 0;
}
static int drm_prime_sg_to_page_array(struct sg_table *sg,struct page **pages,unsigned n) { return array_error; }
static void sg_free_table(struct sg_table *sg) { assert(sg); }
/* Real GEM initialization and rknpu_gem_get_pages run above these services. */
static const int rknpu_gem_object_funcs;
static int gem_init_error;
static int drm_gem_object_init(struct drm_device *d,struct drm_gem_object *o,unsigned long size) {
    if(gem_init_error)return gem_init_error;
    o->dev=d; o->size=size; o->filp=host_alloc(sizeof(*o->filp));
    o->filp->f_mapping=host_alloc(sizeof(*o->filp->f_mapping));
    o->filp->f_mapping->gfp=__GFP_HIGHMEM; return 0;
}
static void drm_gem_object_release(struct drm_gem_object *o) {
    kfree(o->filp->f_mapping); kfree(o->filp);
}
static gfp_t mapping_gfp_mask(struct address_space *m) { return m->gfp; }
static void mapping_set_gfp_mask(struct address_space *m,gfp_t gfp) { m->gfp=gfp; }
static struct page **drm_gem_get_pages(struct drm_gem_object *o) {
    ++page_gets; if(o->filp)page_gfp=o->filp->f_mapping->gfp;
    if(page_error)return (void *)(intptr_t)page_error;
    return rknpu_gem_alloc_page(16);
}
static void drm_gem_put_pages(struct drm_gem_object *o,struct page **p,bool dirty,bool access) {
    ++page_puts; assert(page_gets>=page_puts); kfree(p);
}
static struct sg_table *drm_prime_pages_to_sg(struct drm_device *d,struct page **p,unsigned n) {
    struct sg_table *s=host_alloc(sizeof(*s)); s->sgl=&scatter; s->nents=1; scatter.dma_address=0x2000; return s;
}
#define DMA_BIDIRECTIONAL 0
#define VM_MAP 0
#define PAGE_KERNEL 0
typedef uint64_t dma_addr_t;
static int rknpu_iommu_dma_map_sg(void *d,struct scatterlist *s,unsigned n,int dir,bool aligned) { return n; }
static void rknpu_iommu_dma_unmap_sg(void *d,struct scatterlist *s,unsigned n,int dir,bool aligned) {}
static void iommu_flush_iotlb_all(void *d) {}
static void *vmap(struct page **p,unsigned n,int flags,int prot) { return host_alloc(1); }
static void vunmap(void *p) { kfree(p); }
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct iommu_domain { int unused; };
static struct iommu_domain test_domain;
static bool missing_domain;
static int attach_error, attaches;
static struct iommu_domain *iommu_get_domain_for_dev(void *d) { return missing_domain ? NULL : &test_domain; }
static void iommu_detach_device(struct iommu_domain *d,void *dev) {}
static int iommu_attach_device(struct iommu_domain *d,void *dev) { ++attaches; return attach_error; }
struct reset_control { int asserted, assert_error, deassert_error, status_error; bool status_mismatch, status_stuck_asserted; };
static void (*reset_hook)(void);
static int reset_asserts, reset_deasserts, reset_delays, reset_completed;
static int reset_control_assert(struct reset_control *r) {
    if(reset_hook)reset_hook();
    ++reset_asserts;
    if(r->assert_error)return r->assert_error;
    r->asserted=1; return 0;
}
static int reset_control_deassert(struct reset_control *r) {
    ++reset_deasserts; if(r->deassert_error)return r->deassert_error; r->asserted=0; return 0;
}
static int reset_control_status(struct reset_control *r) {
    if(r->status_error)return r->status_error;
    return r->status_stuck_asserted ? 1 : (r->status_mismatch ? !r->asserted : r->asserted);
}
static void udelay(int us) { ++reset_delays; }
static void reset_state_init(struct rknpu_device *d) {
    ++d->resets; ++reset_completed;
    for(int i=0;i<3;i++) { d->hardware[i]=NULL; if(!stuck_irq)((uint32_t *)d->base[i])[RKNPU_OFFSET_INT_STATUS/4]=0; if(!stuck_raw_irq)((uint32_t *)d->base[i])[RKNPU_OFFSET_INT_RAW_STATUS/4]=0; }
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
