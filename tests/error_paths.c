/* SPDX-License-Identifier: GPL-2.0-only */
#include "error_paths_host.h"
#include "production.inc"

static struct rknpu_device dev;
static struct rknpu_config config;
static struct rknpu_fence_context context;
static struct rknpu_gem_object task;
static uint32_t registers[3][64];
static int cases;
static struct reset_control resets[6];
static struct reset_control *reset_ptrs[6];
static void setup(void)
{
    flush_work(); assert(live_allocs==0 && module_refs==0); ++cases;
    memset(&dev,0,sizeof(dev)); memset(&config,0,sizeof(config));
    memset(&task,0,sizeof(task)); memset(registers,0,sizeof(registers));
    config.num_irqs=3; config.core_mask=7; config.max_submit_number=4095;
    config.dma_mask=UINT64_MAX; config.state_init=reset_state_init;
    memset(resets,0,sizeof(resets)); for(int i=0;i<6;i++)reset_ptrs[i]=&resets[i];
    dev.srsts=reset_ptrs; dev.num_srsts=6; dev.config=&config; dev.fence_ctx=&context;
    dev.rk3588_core0_banks=true; task.base.refs=1;
    for(int i=0;i<3;i++) { INIT_LIST_HEAD(&dev.subcore_datas[i].todo_list); dev.base[i]=registers[i]; }
    alloc_calls=fail_alloc_at=0; now_us=100;
    domain_error=commit_error=wait_result=range_error=0;
    input_fence=(struct dma_fence){.foreign=true}; output_fence=(struct dma_fence){0};
    valid_fence=true; fence_wait_result=1; fence_wait_ticks=0; host_hz=250;
    fence_alloc_error=fence_fd_error=fd_live=0; syncfile_fail=false; before_lock=NULL; after_unlock=NULL; wait_event_hook=NULL;
    reset_hook=NULL; fence_signal_hook=NULL; power_error=gem_init_error=attach_error=attaches=0;
    reset_asserts=reset_deasserts=reset_delays=reset_completed=0;
    missing_domain=stuck_irq=stuck_raw_irq=false; dma_gfp=page_gfp=0;
    dma_calls=dma_failures=dma_frees=page_gets=page_puts=page_error=sg_error=array_error=0;
}
static struct rknpu_submit arguments(unsigned mask,bool async)
{
    struct rknpu_submit a={.task_number=1,.core_mask=mask,.timeout=1000,
        .flags=RKNPU_JOB_PC | (async?RKNPU_JOB_NONBLOCK:0),.task_obj_addr=(uintptr_t)&task};
    for(int i=0;i<3;i++)a.subcore_task[i].task_number=1;
    return a;
}
static struct rknpu_job *make_job(unsigned mask,bool async)
{
    struct rknpu_submit a=arguments(mask,async);
    struct rknpu_job *j=rknpu_job_alloc(&dev,&a); assert(j);
    if(async) j->flags|=RKNPU_JOB_ASYNC;
    for(int i=0;i<3;i++)j->int_mask[i]=3;
    return j;
}
static void irq(int core,unsigned status)
{
    registers[core][RKNPU_OFFSET_INT_STATUS/4]=status;
    if(status==1 && !dev.soft_reseting)dev.hardware[core]=NULL;
    assert(rknpu_irq_handler(0,&dev,core)==(status && !dev.soft_reseting?IRQ_HANDLED:IRQ_NONE));
}
static void empty_device(void)
{
    for(int i=0;i<3;i++) {
        assert(!dev.subcore_datas[i].job);
        assert(list_empty(&dev.subcore_datas[i].todo_list));
        assert(dev.subcore_datas[i].task_num==0);
    }
    assert(dev.domain_refs==0);
}
static void released(void) { flush_work(); assert(live_allocs==0); assert(task.base.refs==1); }
static struct rknpu_job *interrupted_job;
static void abort_before_irq_lock(spinlock_t *lock)
{
    if(lock!=&dev.dispatch_lock)return;
    after_unlock=NULL;
    rknpu_job_abort(interrupted_job,-ETIMEDOUT);
    rknpu_job_put(interrupted_job); /* submitter left; IRQ must retain its ref */
    flush_work(); assert(live_allocs==2); assert(task.base.refs==2);
}
static void completion_before_timeout_lock(spinlock_t *lock)
{
    if(lock!=&dev.dispatch_lock)return;
    before_lock=NULL;
    irq(0,1); /* completion wins after timeout_clean acquired its reference */
}
static void test_jobs(void)
{
    /* Invalid IRQ -> abort -> later IRQ: no stale core reference or double put. */
    setup(); struct rknpu_job *j=make_job(7,false); rknpu_job_schedule(j);
    irq(0,4); assert(dev.subcore_datas[0].job==j);
    rknpu_job_abort(j,-ETIMEDOUT); empty_device(); assert(dev.resets==1);
    rknpu_job_put(j); released(); for(int i=0;i<3;i++)irq(i,1); released();

    setup(); j=make_job(1,false); rknpu_job_schedule(j);
    interrupted_job=j; after_unlock=abort_before_irq_lock;
    irq(0,4); empty_device(); released(); /* abort while IRQ already holds ptr */

    setup(); j=make_job(7,true); rknpu_job_schedule(j);
    irq(0,1); assert(dev.subcore_datas[0].task_num==0);
    rknpu_job_abort(j,-ETIMEDOUT); rknpu_job_abort(j,-ETIMEDOUT);
    empty_device(); assert(dev.resets==1); rknpu_job_put(j); released();

    setup(); j=make_job(4,false); dev.soft_reseting=true; rknpu_job_schedule(j);
    assert(!list_empty(&dev.subcore_datas[2].todo_list));
    rknpu_job_abort(j,-EINVAL); empty_device(); rknpu_job_put(j); released();

    setup(); j=make_job(7,true); rknpu_job_schedule(j); rknpu_job_put(j);
    irq(0,1); irq(1,1); assert(live_allocs==2); irq(2,1); empty_device(); released();

    setup(); j=make_job(1,true); j->args->subcore_task[0].task_number=4096;
    rknpu_job_schedule(j); irq(0,1); assert(dev.commits==2);
    assert(dev.subcore_datas[0].job==j); irq(0,1); empty_device(); rknpu_job_put(j); released();

    setup(); j=make_job(1,true); rknpu_job_schedule(j);
    irq(0,0); assert(dev.subcore_datas[0].job==j); rknpu_job_abort(j,-EIO); rknpu_job_put(j); released();

    setup(); struct rknpu_job *blocker=make_job(2,true); rknpu_job_schedule(blocker);
    j=make_job(3,true); rknpu_job_schedule(j);
    assert(dev.subcore_datas[0].job==j && dev.subcore_datas[1].job==blocker);
    rknpu_job_abort(j,-EINVAL); assert(!dev.subcore_datas[0].job);
    assert(!dev.subcore_datas[1].job && dev.subcore_datas[1].task_num==0);
    assert(list_empty(&dev.subcore_datas[1].todo_list) && dev.domain_refs==0);
    rknpu_job_put(j); irq(1,1); rknpu_job_put(blocker); empty_device(); released();

    for(int aborting=0;aborting<2;aborting++) {
        setup(); j=make_job(7,true); assert(rknpu_fence_alloc(j)==0); rknpu_job_schedule(j);
        irq(0,1); irq(1,1); assert(output_fence.signals==0);
        if(aborting) { rknpu_job_abort(j,-ETIMEDOUT); rknpu_job_abort(j,-ETIMEDOUT); }
        else irq(2,1);
        assert(output_fence.signals==1 && output_fence.error==(aborting?-ETIMEDOUT:0));
        rknpu_job_put(j); empty_device(); released(); assert(output_fence.refs==0);
    }

    setup(); struct rknpu_submit a=arguments(1,false); j=rknpu_job_alloc(&dev,&a);
    a.timeout=4; assert(j->args!=&a && j->args->timeout==1000); rknpu_job_put(j); released();

    setup(); j=make_job(1,true); dev.subcore_datas[0].task_num=INT64_MAX;
    rknpu_job_schedule(j); assert(j->ret==-EOVERFLOW && dev.domain_refs==0);
    rknpu_job_abort(j,j->ret); rknpu_job_put(j); released();
}
static void test_timeout(void)
{
    /* Includes the original ~1ms false expiry and a u32 multiplication boundary. */
    const uint32_t limits[]={0,1,1000,UINT32_MAX};
    for(unsigned n=0;n<sizeof(limits)/sizeof(limits[0]);n++) {
        setup(); struct rknpu_job *j=make_job(7,true); j->args->timeout=limits[n];
        rknpu_job_schedule(j); int64_t deadline=j->timestamp+(int64_t)limits[n]*1000;
        now_us=deadline-1; rknpu_job_timeout_clean(&dev,7);
        assert(dev.subcore_datas[0].job==j && dev.resets==0);
        now_us=deadline; rknpu_job_timeout_clean(&dev,7);
        empty_device(); assert(dev.resets==1); assert(j->ret==-ETIMEDOUT);
        rknpu_job_put(j); released();
    }
    setup(); struct rknpu_job *j=make_job(1,true); rknpu_job_schedule(j);
    now_us=j->timestamp+1100; rknpu_job_timeout_clean(&dev,1);
    assert(dev.resets==0 && dev.subcore_datas[0].job==j);
    rknpu_job_abort(j,-EIO); rknpu_job_put(j); released();

    setup(); j=make_job(4,false); rknpu_job_schedule(j); now_us+=2000000;
    rknpu_job_timeout_clean(&dev,4); assert(dev.resets==0 && dev.subcore_datas[2].job==j);
    rknpu_job_abort(j,-EIO); rknpu_job_put(j); released();

    setup(); j=make_job(1,true); rknpu_job_schedule(j); now_us+=900000;
    struct rknpu_job *queued=make_job(1,true); rknpu_job_schedule(queued);
    now_us=j->timestamp+1000000; rknpu_job_timeout_clean(&dev,1);
    assert(dev.subcore_datas[0].job==queued && dev.domain_refs==1);
    assert(queued->ret==0 && dev.subcore_datas[0].task_num==1);
    rknpu_job_put(j); rknpu_job_abort(queued,-EIO); rknpu_job_put(queued); released();

    setup(); j=make_job(1,true); rknpu_job_schedule(j); now_us+=1000000;
    interrupted_job=j; before_lock=completion_before_timeout_lock;
    rknpu_job_timeout_clean(&dev,1); assert(dev.resets==0 && j->ret==0);
    empty_device(); rknpu_job_put(j); released();
}
static void test_fences(void)
{
    for(int async=0;async<2;async++) {
        const int errors[]={-EINVAL,-ERESTARTSYS,-EIO,-ETIMEDOUT};
        for(unsigned i=0;i<4;i++) {
            setup(); struct rknpu_submit a=arguments(1,async); a.flags|=RKNPU_JOB_FENCE_IN;
            valid_fence=i!=0; fence_wait_result=i==3 ? 0 : errors[i];
            assert(rknpu_submit(&dev,&a)==errors[i]); released(); empty_device();
            assert(input_fence.refs==0 && dev.commits==0);
            if(i)assert(fence_wait_ticks==250);
        }
    }
    for(int fail=1;fail<=2;fail++) {
        setup(); struct rknpu_submit a=arguments(1,true); fail_alloc_at=fail;
        assert(rknpu_submit(&dev,&a)==-ENOMEM); released();
    }
    for(int same=0;same<2;same++) {
        setup(); struct rknpu_submit a=arguments(1,true); a.flags|=RKNPU_JOB_FENCE_IN;
        input_fence.foreign=!same; assert(rknpu_submit(&dev,&a)==0);
        assert(input_fence.refs==0); assert(fence_wait_ticks==(same?0:250));
        struct rknpu_job *j=dev.subcore_datas[0].job; assert(j); j->int_mask[0]=3;
        irq(0,1); released(); empty_device();
    }
    setup(); struct rknpu_submit a=arguments(1,true); a.flags|=RKNPU_JOB_FENCE_IN;
    a.timeout=UINT32_MAX; host_hz=1000; fence_wait_result=UINT32_MAX;
    assert(rknpu_submit(&dev,&a)==0 && fence_wait_ticks==UINT32_MAX);
    dev.subcore_datas[0].job->int_mask[0]=3; irq(0,1); released();

    setup(); a=arguments(1,true); a.flags|=RKNPU_JOB_FENCE_OUT;
    fence_fd_error=-EMFILE; assert(rknpu_submit(&dev,&a)==-EMFILE);
    released(); assert(output_fence.refs==0 && dev.commits==0);

    setup(); a=arguments(1,true); a.flags|=RKNPU_JOB_FENCE_OUT;
    syncfile_fail=true; assert(rknpu_submit(&dev,&a)==-ENOMEM);
    released(); assert(fd_live==0 && output_fence.refs==0);

    setup(); a=arguments(1,true); a.flags|=RKNPU_JOB_FENCE_OUT;
    fence_alloc_error=-ENOMEM; assert(rknpu_submit(&dev,&a)==-ENOMEM); released();

    setup(); a=arguments(1,true); domain_error=-EINVAL;
    assert(rknpu_submit(&dev,&a)==-EINVAL); released(); empty_device();

    setup(); a=arguments(1,true); commit_error=-EINVAL;
    assert(rknpu_submit(&dev,&a)==-EINVAL); released(); empty_device();
}
static void test_dma(void)
{
    for(int fallback=0;fallback<2;fallback++) {
        setup(); struct drm_device drm={.dev_private=&dev};
        struct rknpu_gem_object o={.base.dev=&drm,.size=8192};
        dev.iommu_en=true; dma_failures=fallback;
        assert(rknpu_gem_alloc_buf(&o)==0);
        rknpu_gem_free_buf(&o); released();
        assert(dma_frees==(fallback && RKNPU_GEM_ALLOC_FROM_PAGES ? 0 : 1));
        assert(page_puts==(fallback && RKNPU_GEM_ALLOC_FROM_PAGES ? 1 : 0));
    }
    setup(); struct drm_device drm={.dev_private=&dev};
    struct rknpu_gem_object o={.base.dev=&drm,.size=8192};
    dev.iommu_en=true; dma_failures=2; page_error=-ENOMEM;
    assert(rknpu_gem_alloc_buf(&o)<0); released();

    setup(); o=(struct rknpu_gem_object){.base.dev=&drm,.size=8192}; dma_failures=1;
    assert(rknpu_gem_alloc_buf(&o)<0); assert(dma_calls==1 && page_gets==0); released();

    setup(); o=(struct rknpu_gem_object){.base.dev=&drm,.size=8192,.flags=RKNPU_MEM_NON_CONTIGUOUS};
    dev.iommu_en=true; assert(rknpu_gem_alloc_buf(&o)==0); rknpu_gem_free_buf(&o); released();
    assert(page_puts==(RKNPU_GEM_ALLOC_FROM_PAGES?1:0));

    for(int where=0;where<3;where++) {
        setup(); o=(struct rknpu_gem_object){.base.dev=&drm,.size=8192};
        if(where==0)fail_alloc_at=3; else if(where==1)sg_error=-ENOMEM; else array_error=-EINVAL;
        assert(rknpu_gem_alloc_buf(&o)<0); assert(dma_frees==1); released();
    }
}
/* The mocked controller records hardware ownership independently of slots.
 * All reset, recovery, IRQ, scheduling and GEM GFP selection above are real
 * production functions. Only controller/DRM/PM services are replaced. */
static int recovery_checks;
static void stopped_before_fence(void)
{
    assert(reset_completed > 0);
    assert(dev.soft_reseting); /* gate stays closed through notifications */
    for(int i=0;i<3;i++)assert(dev.hardware[i]==NULL);
    ++recovery_checks;
}
static void during_reset(void)
{
    int commits=dev.commits;
    assert(reset_completed==0 && output_fence.signals==0);
    assert(dev.reset_lock.locked && dev.soft_reseting);
    for(int i=0;i<3;i++) {
        assert(dev.subcore_datas[i].job_done_wq==0);
        rknpu_job_next(&dev,i);
        registers[i][RKNPU_OFFSET_INT_STATUS/4]=1;
        assert(rknpu_irq_handler(0,&dev,i)==IRQ_NONE);
    }
    assert(dev.commits==commits && output_fence.signals==0);
    ++recovery_checks;
}
static void retry_from_wait(void)
{
    assert(dev.reset_failed && output_fence.signals==0);
    assert(dev.subcore_datas[0].job && !(dev.subcore_datas[0].job->flags&RKNPU_JOB_DONE));
    assert(module_refs==1 && dev.power_refs==1);
    resets[0].assert_error=0;
    assert(rknpu_soft_reset(&dev)==0);
    ++recovery_checks;
}
static void test_recovery(void)
{
    setup(); struct rknpu_job *j=make_job(1,true), *other=make_job(2,true);
    struct rknpu_job *next=make_job(1,true), *idle=make_job(4,true);
    assert(!rknpu_fence_alloc(j));
    rknpu_job_schedule(j); rknpu_job_schedule(other); rknpu_job_schedule(next);
    /* Represents a queued core just before its job_next call acquires the gate. */
    dev.soft_reseting=true; rknpu_job_schedule(idle); dev.soft_reseting=false;
    recovery_checks=0; reset_hook=during_reset; fence_signal_hook=stopped_before_fence;
    rknpu_job_abort(j,-ETIMEDOUT);
    assert(recovery_checks==7 && reset_completed==1);
    assert(output_fence.signals==1 && output_fence.error==-ETIMEDOUT);
    assert(j->ret==-ETIMEDOUT && other->ret==-EIO);
    assert(dev.subcore_datas[0].job==next && dev.hardware[0]==next);
    assert(!dev.subcore_datas[1].job && !dev.hardware[1]);
    assert(dev.subcore_datas[2].job==idle && dev.hardware[2]==idle);
    assert(dev.domain_refs==2 && !dev.soft_reseting && !dev.reset_failed);
    reset_hook=NULL; fence_signal_hook=NULL;
    rknpu_job_put(j); rknpu_job_put(other); irq(0,1); irq(2,1);
    rknpu_job_put(next); rknpu_job_put(idle); empty_device(); released();

    /* Explicit resets must use the same gate and cancel every active job. */
    setup(); j=make_job(1,true); other=make_job(2,true);
    rknpu_job_schedule(j); rknpu_job_schedule(other);
    assert(rknpu_soft_reset(&dev)==0); empty_device();
    assert(j->ret==-EIO && other->ret==-EIO && dev.resets==1);
    rknpu_job_put(j); rknpu_job_put(other); released();

    for(int failure=0;failure<12;failure++) {
        setup(); j=make_job(1,true); next=make_job(1,true);
        assert(!rknpu_fence_alloc(j)); rknpu_job_schedule(j); rknpu_job_schedule(next);
        switch(failure) {
        case 0: resets[0].assert_error=-EIO; break;
        case 1: resets[0].deassert_error=-EIO; break;
        case 2: resets[0].status_error=-EIO; break;
        case 3: resets[0].status_mismatch=true; break;
        case 4: dev.bypass_soft_reset=1; break;
        case 5: dev.num_srsts=0; break;
        case 6: dev.iommu_en=true; attach_error=-EIO; break;
        case 7: dev.iommu_en=true; missing_domain=true; break;
        case 8: stuck_irq=true; registers[0][RKNPU_OFFSET_INT_STATUS/4]=1; break;
        case 9: power_error=-EIO; break;
        case 10: stuck_raw_irq=true; registers[0][RKNPU_OFFSET_INT_RAW_STATUS/4]=1; break;
        case 11: resets[0].status_stuck_asserted=true; break;
        }
        rknpu_job_abort(j,-ETIMEDOUT);
        assert(dev.soft_reseting && dev.reset_failed && module_refs==1);
        assert(!(j->flags&RKNPU_JOB_DONE) && !output_fence.signals);
        assert(dev.subcore_datas[0].job==j && dev.domain_refs==2);
        assert(dev.subcore_datas[0].job_done_wq==0 && task.base.refs==3);
        assert(nwork==0 && dev.commits==1);
        int asserts=reset_asserts;
        rknpu_job_abort(j,-ETIMEDOUT); /* quarantined jobs do not auto retry */
        rknpu_job_next(&dev,0); irq(0,1);
        assert(reset_asserts==asserts && dev.commits==1 && !output_fence.signals);
        struct rknpu_submit rejected=arguments(4,true);
        assert(rknpu_submit(&dev,&rejected)==-EIO); flush_work();
        assert(task.base.refs==3 && dev.commits==1);
        if(failure==0 || failure==2 || failure==3)assert(reset_deasserts==0);
        memset(resets,0,sizeof(resets)); dev.bypass_soft_reset=0; dev.num_srsts=6;
        power_error=attach_error=0; missing_domain=stuck_irq=stuck_raw_irq=false;
        assert(rknpu_soft_reset(&dev)==0);
        assert(output_fence.signals==1 && output_fence.error==-EIO);
        assert(dev.subcore_datas[0].job==next && dev.hardware[0]==next);
        assert(!dev.soft_reseting && !dev.reset_failed && module_refs==0 && dev.power_refs==0);
        rknpu_job_put(j); irq(0,1); rknpu_job_put(next); empty_device(); released();
    }
    /* Linux 6.12 Rockchip has no .status callback: exclusive API success is
     * sufficient; this optional query is not the reset operation itself. */
    setup(); j=make_job(1,true); rknpu_job_schedule(j);
    for(int i=0;i<6;i++)resets[i].status_error=-ENOTSUPP;
    rknpu_job_abort(j,-ETIMEDOUT); assert(dev.resets==1 && !dev.reset_failed);
    rknpu_job_put(j); empty_device(); released();

    /* A failed stop cannot return a synchronous ioctl as reusable memory. */
    setup(); struct rknpu_submit a=arguments(1,false); wait_result=-ETIMEDOUT;
    resets[0].assert_error=-EIO; wait_event_hook=retry_from_wait;
    recovery_checks=0; assert(rknpu_submit(&dev,&a)==-ETIMEDOUT);
    assert(recovery_checks==1 && !dev.reset_failed); released(); empty_device();

    /* A queued-only cancellation needs no device reset or collateral abort. */
    setup(); j=make_job(1,true); next=make_job(1,true);
    rknpu_job_schedule(j); rknpu_job_schedule(next);
    rknpu_job_abort(next,-ETIMEDOUT); assert(dev.resets==0 && dev.hardware[0]==j);
    rknpu_job_put(next); irq(0,1); rknpu_job_put(j); empty_device(); released();
}
static void test_gfp_initialization(void)
{
    for(int dma32=0;dma32<2;dma32++)for(int explicit_pages=0;explicit_pages<2;explicit_pages++) {
        setup(); struct drm_device drm={.dev_private=&dev}; dev.iommu_en=true;
        config.dma_mask=DMA_BIT_MASK(40);
        unsigned flags=RKNPU_MEM_ZEROING | (dma32?RKNPU_MEM_DMA32:0) |
                       (explicit_pages?RKNPU_MEM_NON_CONTIGUOUS:0);
        struct rknpu_gem_object *o=rknpu_gem_init(&drm,8192,flags);
        assert(!IS_ERR(o) && o->flags==flags);
        dma_failures=explicit_pages?0:1;
        assert(rknpu_gem_alloc_buf(o)==0);
        gfp_t seen=RKNPU_GEM_ALLOC_FROM_PAGES?page_gfp:dma_gfp;
        assert((seen&__GFP_DMA32)==(dma32?__GFP_DMA32:0));
        assert(seen&__GFP_ZERO);
        if(dma32)assert(!(seen&__GFP_HIGHMEM));
        if(RKNPU_GEM_ALLOC_FROM_PAGES) {
            assert(page_gets==1 && dma_calls==(explicit_pages?0:1));
            assert((seen&__GFP_HIGHMEM)==(dma32?0:__GFP_HIGHMEM));
        }
        rknpu_gem_free_buf(o); rknpu_gem_release(o); released();
    }
    for(int iommu=0;iommu<2;iommu++) {
        setup(); struct drm_device drm={.dev_private=&dev}; dev.iommu_en=iommu;
        config.dma_mask=DMA_BIT_MASK(32);
        struct rknpu_gem_object *o=rknpu_gem_init(&drm,8192,0); assert(!IS_ERR(o));
        assert(mapping_gfp_mask(o->base.filp->f_mapping)&__GFP_DMA32);
        assert(!(mapping_gfp_mask(o->base.filp->f_mapping)&__GFP_HIGHMEM));
        rknpu_gem_release(o); released();
    }
    setup(); struct drm_device drm={.dev_private=&dev}; gem_init_error=-ENOMEM;
    assert(IS_ERR(rknpu_gem_init(&drm,8192,RKNPU_MEM_DMA32))); released();
}

int main(void)
{
    test_jobs(); test_timeout(); test_fences(); test_dma(); test_recovery(); test_gfp_initialization();
    released(); printf("PASS: %d production error-path scenarios (page allocator=%d)\n",cases,RKNPU_GEM_ALLOC_FROM_PAGES);
    return 0;
}
