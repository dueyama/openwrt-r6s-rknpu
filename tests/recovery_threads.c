/* SPDX-License-Identifier: GPL-2.0-only */
/* Host threads exercise the production gate; this is not kernel lockdep/KCSAN. */
#define RKNPU_HOST_THREADS 1
#define main deterministic_tests_main
#include "error_paths.c"
#undef main
#include <pthread.h>
#include <unistd.h>

static _Atomic int reset_entered, reset_release, submit_entered, submit_returned;
static struct rknpu_job *thread_job, *thread_next;
static void paused_reset(void)
{
    atomic_store(&reset_entered,1);
    while(!atomic_load(&reset_release))sched_yield();
}
static void *abort_thread(void *p) { rknpu_job_abort(thread_job,-ETIMEDOUT); return NULL; }
static void *submit_thread(void *p)
{
    atomic_store(&submit_entered,1);
    rknpu_job_schedule(thread_next);
    atomic_store(&submit_returned,1);
    return NULL;
}
static void *irq_thread(void *p) { rknpu_irq_handler(0,&dev,0); return NULL; }
int main(void)
{
    alarm(30); /* A gate deadlock fails the test instead of hanging CI. */
    setup(); thread_job=make_job(1,true); thread_next=make_job(1,true);
    assert(!rknpu_fence_alloc(thread_job)); rknpu_job_schedule(thread_job);
    pthread_t recovery, submit;
    reset_hook=paused_reset;
    assert(!pthread_create(&recovery,NULL,abort_thread,NULL));
    while(!atomic_load(&reset_entered))sched_yield();
    assert(!pthread_create(&submit,NULL,submit_thread,NULL));
    while(!atomic_load(&submit_entered))sched_yield();
    for(int i=0;i<1000;i++)sched_yield();
    assert(!atomic_load(&submit_returned));
    assert(!output_fence.signals && !dev.subcore_datas[0].job_done_wq);
    assert(dev.commits==1 && dev.hardware[0]==thread_job);
    registers[0][RKNPU_OFFSET_INT_STATUS/4]=1;
    assert(rknpu_irq_handler(0,&dev,0)==IRQ_NONE);
    rknpu_job_next(&dev,0); assert(dev.commits==1);
    atomic_store(&reset_release,1);
    assert(!pthread_join(recovery,NULL)); assert(!pthread_join(submit,NULL));
    assert(output_fence.signals==1 && reset_completed==1 && submit_returned);
    assert(dev.subcore_datas[0].job==thread_next && dev.hardware[0]==thread_next);
    assert(!dev.reset_failed && !dev.soft_reseting);
    rknpu_job_put(thread_job); irq(0,1); rknpu_job_put(thread_next); empty_device(); released();

    /* IRQ versus recovery: either completed before the gate, or canceled after
     * reset. A stale status must never be assigned to the queued successor. */
    for(int n=0;n<100;n++) {
        setup(); thread_job=make_job(1,true); thread_next=make_job(1,true);
        rknpu_job_schedule(thread_job); rknpu_job_schedule(thread_next);
        unsigned status=n%2 ? 1 : 4;
        registers[0][RKNPU_OFFSET_INT_STATUS/4]=status;
        if(status==1)dev.hardware[0]=NULL; /* hardware completed, IRQ pending */
        pthread_t interrupt;
        assert(!pthread_create(&recovery,NULL,abort_thread,NULL));
        assert(!pthread_create(&interrupt,NULL,irq_thread,NULL));
        assert(!pthread_join(recovery,NULL)); assert(!pthread_join(interrupt,NULL));
        assert(thread_job->flags&RKNPU_JOB_DONE);
        assert(dev.subcore_datas[0].job==thread_next && dev.hardware[0]==thread_next);
        assert(!(thread_next->flags&RKNPU_JOB_DONE));
        rknpu_job_put(thread_job); irq(0,1); rknpu_job_put(thread_next);
        empty_device(); released();
    }
    printf("PASS: recovery blocks concurrent submit/IRQ plus 100 IRQ/abort races\n");
    return 0;
}
