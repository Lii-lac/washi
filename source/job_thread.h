// A thread for a job that waits on the network (a page keeps drawing and listening for B meanwhile): libnx's Thread on the console, a pthread on a PC.
// The stack is big because curl and TLS want one. Also a lock and a sleep for jobs that hand work back and forth (ui_thumbs.c), and a count to wait on.
#ifndef JOB_THREAD_H
#define JOB_THREAD_H

#ifdef __SWITCH__
#include <switch.h>
typedef struct {
    Thread th;
} JobThread;

static inline int job_thread_start(JobThread *t, void (*fn)(void *), void *arg)
{
    if (R_FAILED(threadCreate(&t->th, fn, arg, NULL, 0x80000, 0x2D, -2))) return -1; // (below the main thread)
    if (R_FAILED(threadStart(&t->th))) {
        threadClose(&t->th);
        return -1;
    }
    return 0;
}

// a job that only works (no network): a small stack, on a core of its own (1 or 2; the main thread is on core 0)
static inline int job_thread_start_on(JobThread *t, void (*fn)(void *), void *arg, int core)
{
    if (R_FAILED(threadCreate(&t->th, fn, arg, NULL, 0x20000, 0x2C, core))) return -1;
    if (R_FAILED(threadStart(&t->th))) {
        threadClose(&t->th);
        return -1;
    }
    return 0;
}

static inline void job_thread_join(JobThread *t)
{
    threadWaitForExit(&t->th);
    threadClose(&t->th);
}

typedef struct {
    Mutex m;
} JobLock;
static inline void job_lock_init(JobLock *l) { mutexInit(&l->m); }
static inline void job_lock(JobLock *l) { mutexLock(&l->m); }
static inline void job_unlock(JobLock *l) { mutexUnlock(&l->m); }
static inline void job_sleep_ms(int ms) { svcSleepThread((int64_t)ms * 1000000); }

// a count to wait on: a worker that sleeps until it is handed something (ui_home.c's painters)
typedef struct {
    Semaphore s;
} JobSem;
static inline void job_sem_init(JobSem *q, int n) { semaphoreInit(&q->s, (u64)n); }
static inline void job_sem_post(JobSem *q) { semaphoreSignal(&q->s); }
static inline void job_sem_wait(JobSem *q) { semaphoreWait(&q->s); }
#else
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
typedef struct {
    pthread_t th;
    void (*fn)(void *);
    void *arg;
} JobThread;

static inline void *job_thread_main(void *p)
{
    JobThread *t = p;
    t->fn(t->arg);
    return NULL;
}

static inline int job_thread_start(JobThread *t, void (*fn)(void *), void *arg)
{
    t->fn = fn;
    t->arg = arg;
    return pthread_create(&t->th, NULL, job_thread_main, t) == 0 ? 0 : -1;
}

static inline int job_thread_start_on(JobThread *t, void (*fn)(void *), void *arg, int core)
{
    (void)core;
    return job_thread_start(t, fn, arg);
}

static inline void job_thread_join(JobThread *t)
{
    pthread_join(t->th, NULL);
}

typedef struct {
    pthread_mutex_t m;
} JobLock;
static inline void job_lock_init(JobLock *l) { pthread_mutex_init(&l->m, NULL); }
static inline void job_lock(JobLock *l) { pthread_mutex_lock(&l->m); }
static inline void job_unlock(JobLock *l) { pthread_mutex_unlock(&l->m); }
static inline void job_sleep_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

typedef struct {
    sem_t s;
} JobSem;
static inline void job_sem_init(JobSem *q, int n) { sem_init(&q->s, 0, (unsigned)n); }
static inline void job_sem_post(JobSem *q) { sem_post(&q->s); }
static inline void job_sem_wait(JobSem *q)
{
    while (sem_wait(&q->s) != 0) {} // (again if a signal woke it)
}
#endif

#endif
