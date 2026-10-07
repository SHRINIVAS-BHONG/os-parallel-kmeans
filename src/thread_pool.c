/**
 * @file thread_pool.c
 * @brief Thread pool and dynamic work scheduler runtime implementation.
 */

#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>

/**
 * @brief High-resolution monotonic clock returning current time in nanoseconds.
 */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Worker thread loop function.
 */
static void* worker_main(void *arg) {
    WorkerArg *w = (WorkerArg *)arg;
    ThreadPool *pool = w->pool;
    int tid = w->tid;
    Task task;

    /*
     * Continuously pop tasks from the dynamic queue.
     * queue_pop blocks until a task is available, or returns false when
     * the queue has been closed and all pending work is drained.
     */
    while (queue_pop(&pool->q, &task)) {
        uint64_t t0 = get_time_ns();

        /*
         * PARALLEL EXECUTION:
         * Executed completely unlocked so threads never block each other during compute.
         */
        if (pool->fn) {
            pool->fn(&task, tid, task.ctx);
        }

        uint64_t elapsed = get_time_ns() - t0;
        pool->stats[tid].busy_ns += elapsed;
        pool->stats[tid].tasks_done++;

        /*
         * COMPLETION ACCOUNTING:
         * Decrement pending counter and signal main thread if all tasks finished.
         */
        pthread_mutex_lock(&pool->done_lock);
        pool->pending--;
        if (pool->pending == 0) {
            pthread_cond_broadcast(&pool->all_done);
        }
        pthread_mutex_unlock(&pool->done_lock);
    }

    return NULL;
}

ThreadPool* pool_create(int nthreads, size_t queue_cap, task_fn_t fn) {
    if (nthreads < 1 || !fn) {
        return NULL;
    }

    ThreadPool *pool = (ThreadPool *)calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;

    pool->nthreads = nthreads;
    pool->fn = fn;
    pool->pending = 0;

    if (queue_init(&pool->q, queue_cap) != QUEUE_OK) {
        free(pool);
        return NULL;
    }

    if (pthread_mutex_init(&pool->done_lock, NULL) != 0) {
        queue_destroy(&pool->q);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->all_done, NULL) != 0) {
        pthread_mutex_destroy(&pool->done_lock);
        queue_destroy(&pool->q);
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t *)malloc(nthreads * sizeof(pthread_t));
    pool->worker_args = (WorkerArg *)malloc(nthreads * sizeof(WorkerArg));
    pool->stats = (WorkerStats *)calloc(nthreads, sizeof(WorkerStats));

    if (!pool->threads || !pool->worker_args || !pool->stats) {
        if (pool->threads) free(pool->threads);
        if (pool->worker_args) free(pool->worker_args);
        if (pool->stats) free(pool->stats);
        pthread_cond_destroy(&pool->all_done);
        pthread_mutex_destroy(&pool->done_lock);
        queue_destroy(&pool->q);
        free(pool);
        return NULL;
    }

    /* Spawn worker threads */
    for (int t = 0; t < nthreads; t++) {
        pool->worker_args[t].pool = pool;
        pool->worker_args[t].tid = t;

        int rc = pthread_create(&pool->threads[t], NULL, worker_main, &pool->worker_args[t]);
        if (rc != 0) {
            /* Unwind cleanly if any thread creation fails */
            queue_close(&pool->q);
            for (int j = 0; j < t; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            free(pool->worker_args);
            free(pool->stats);
            pthread_cond_destroy(&pool->all_done);
            pthread_mutex_destroy(&pool->done_lock);
            queue_destroy(&pool->q);
            free(pool);
            return NULL;
        }
    }

    return pool;
}

int pool_submit(ThreadPool *pool, Task task) {
    if (!pool) return QUEUE_ERR_INVALID;

    /* Increment pending counter BEFORE pushing to queue */
    pthread_mutex_lock(&pool->done_lock);
    pool->pending++;
    pthread_mutex_unlock(&pool->done_lock);

    int rc = queue_push(&pool->q, task);
    if (rc != QUEUE_OK) {
        /* Rollback pending counter if enqueue failed */
        pthread_mutex_lock(&pool->done_lock);
        pool->pending--;
        if (pool->pending == 0) {
            pthread_cond_broadcast(&pool->all_done);
        }
        pthread_mutex_unlock(&pool->done_lock);
        return rc;
    }

    return QUEUE_OK;
}

int pool_submit_batch(ThreadPool *pool, const Task *tasks, size_t n) {
    if (!pool || !tasks || n == 0) return QUEUE_ERR_INVALID;

    /* Batch increment reduces done_lock contention */
    pthread_mutex_lock(&pool->done_lock);
    pool->pending += (long)n;
    pthread_mutex_unlock(&pool->done_lock);

    for (size_t i = 0; i < n; i++) {
        int rc = queue_push(&pool->q, tasks[i]);
        if (rc != QUEUE_OK) {
            /* Roll back remaining unpushed tasks */
            long unpushed = (long)(n - i);
            pthread_mutex_lock(&pool->done_lock);
            pool->pending -= unpushed;
            if (pool->pending == 0) {
                pthread_cond_broadcast(&pool->all_done);
            }
            pthread_mutex_unlock(&pool->done_lock);
            return rc;
        }
    }

    return QUEUE_OK;
}

void pool_wait_iteration(ThreadPool *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->done_lock);
    while (pool->pending > 0) {
        pthread_cond_wait(&pool->all_done, &pool->done_lock);
    }
    pthread_mutex_unlock(&pool->done_lock);
}

void pool_shutdown(ThreadPool *pool) {
    if (!pool) return;

    /* Close queue and broadcast wakeup to all sleeping workers */
    queue_close(&pool->q);

    /* Join all worker threads */
    for (int t = 0; t < pool->nthreads; t++) {
        pthread_join(pool->threads[t], NULL);
    }

    /* Destroy synchronization primitives */
    pthread_cond_destroy(&pool->all_done);
    pthread_mutex_destroy(&pool->done_lock);
    queue_destroy(&pool->q);

    /* Free dynamically allocated memory */
    free(pool->threads);
    free(pool->worker_args);
    free(pool->stats);
    free(pool);
}

void pool_reset_stats(ThreadPool *pool) {
    if (!pool || !pool->stats) return;
    for (int t = 0; t < pool->nthreads; t++) {
        pool->stats[t].tasks_done = 0;
        pool->stats[t].busy_ns = 0;
    }
}

void pool_print_stats(const ThreadPool *pool) {
    if (!pool || !pool->stats) return;

    uint64_t total_tasks = 0;
    uint64_t max_busy = 0;
    uint64_t min_busy = UINT64_MAX;
    uint64_t total_busy = 0;

    printf("\n=== Thread Pool Dynamic Scheduling Telemetry ===\n");
    printf(" Worker | Tasks Executed | Busy Time (ms) | Utilization \n");
    printf("--------+----------------+----------------+-------------\n");

    for (int t = 0; t < pool->nthreads; t++) {
        uint64_t tasks = pool->stats[t].tasks_done;
        uint64_t busy_ns = pool->stats[t].busy_ns;
        double busy_ms = (double)busy_ns / 1e6;

        total_tasks += tasks;
        total_busy += busy_ns;
        if (busy_ns > max_busy) max_busy = busy_ns;
        if (busy_ns < min_busy) min_busy = busy_ns;

        printf("  %4d  | %14" PRIu64 " | %14.2f |   [active]\n", t, tasks, busy_ms);
    }

    double avg_busy_ms = (double)total_busy / (pool->nthreads * 1e6);
    double imbalance_ratio = (avg_busy_ms > 0) ? ((double)max_busy / 1e6) / avg_busy_ms : 1.0;

    printf("--------------------------------------------------------\n");
    printf(" Total tasks processed: %" PRIu64 "\n", total_tasks);
    printf(" Avg worker busy time : %.2f ms\n", avg_busy_ms);
    printf(" Max worker busy time : %.2f ms\n", (double)max_busy / 1e6);
    printf(" Load Imbalance Ratio : %.2fx (1.00x = ideal balance)\n", imbalance_ratio);
    printf("========================================================\n\n");
}

const WorkerStats* pool_get_stats(const ThreadPool *pool, int tid) {
    if (!pool || !pool->stats || tid < 0 || tid >= pool->nthreads) {
        return NULL;
    }
    return &pool->stats[tid];
}
