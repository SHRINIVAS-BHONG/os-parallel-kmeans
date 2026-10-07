/**
 * @file thread_pool.h
 * @brief Thread pool and dynamic work scheduler runtime using POSIX threads.
 *
 * Implements a fixed-size worker thread pool with work-stealing/pull-based
 * dynamic task distribution, iteration barrier synchronization, and
 * cache-line padded per-thread profiling metrics.
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "task_queue.h"
#include <stdint.h>
#include <stdalign.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Function pointer signature for parallel worker tasks.
 *
 * @param task Pointer to the task item being executed.
 * @param tid Unique worker thread ID (0 to nthreads - 1).
 * @param ctx Arbitrary context pointer passed in the task.
 */
typedef void (*task_fn_t)(const Task *task, int tid, void *ctx);

/**
 * @brief Cache-line padded and aligned statistics structure per thread.
 *
 * Guarantees that each thread's counters reside in distinct 64-byte L1 cache lines,
 * eliminating false sharing during continuous performance profiling.
 */
typedef struct {
    alignas(64) uint64_t tasks_done;  /**< Count of tasks executed by this worker */
    uint64_t busy_ns;                 /**< Nanoseconds spent executing task_fn */
    char padding[48];                 /**< Defensive padding to guarantee 64-byte line */
} WorkerStats;

/* Forward declaration */
typedef struct ThreadPool ThreadPool;

/**
 * @brief Thread argument passed to each worker thread.
 */
typedef struct {
    ThreadPool *pool;  /**< Pointer to owning thread pool */
    int tid;           /**< Worker thread index [0, nthreads - 1] */
} WorkerArg;

/**
 * @brief Main Thread Pool runtime structure.
 */
struct ThreadPool {
    int nthreads;              /**< Number of active worker threads */
    pthread_t *threads;        /**< Array of POSIX thread handles */
    WorkerArg *worker_args;    /**< Separate argument struct per worker */
    TaskQueue q;               /**< Underlying bounded task queue */
    task_fn_t fn;              /**< Default task callback function */

    /* Iteration Barrier Synchronization (Mode A) */
    long pending;              /**< Total submitted tasks pending completion */
    pthread_mutex_t done_lock; /**< Mutex protecting pending counter */
    pthread_cond_t all_done;   /**< Signaled when pending decrements to 0 */

    /* Telemetry */
    WorkerStats *stats;        /**< Array of aligned stats, indexed by tid */
};

/**
 * @brief Creates a worker thread pool.
 *
 * Spawns nthreads workers that immediately wait on the dynamic task queue.
 *
 * @param nthreads Number of worker threads (must be >= 1).
 * @param queue_cap Maximum queue capacity (rounded to power of 2).
 * @param fn Default callback function for processing tasks.
 * @return Pointer to allocated ThreadPool, or NULL on error.
 */
ThreadPool* pool_create(int nthreads, size_t queue_cap, task_fn_t fn);

/**
 * @brief Submits a single task to the thread pool.
 *
 * Atomically increments pending counter before pushing to queue.
 *
 * @param pool Pointer to ThreadPool.
 * @param task Task data structure.
 * @return QUEUE_OK on success, or error code.
 */
int pool_submit(ThreadPool *pool, Task task);

/**
 * @brief Submits a batch of tasks to the thread pool.
 *
 * Updates pending counter once for the entire batch to reduce lock contention.
 *
 * @param pool Pointer to ThreadPool.
 * @param tasks Array of tasks to push.
 * @param n Number of tasks in the batch.
 * @return QUEUE_OK on success, or error code.
 */
int pool_submit_batch(ThreadPool *pool, const Task *tasks, size_t n);

/**
 * @brief Blocks the caller (main thread) until all pending tasks have completed.
 *
 * Guarantees a POSIX happens-before synchronization edge between all worker
 * thread writes and subsequent code in the main thread (e.g. accumulator reduction).
 *
 * @param pool Pointer to ThreadPool.
 */
void pool_wait_iteration(ThreadPool *pool);

/**
 * @brief Closes the queue, joins all worker threads, and releases all resources.
 *
 * @param pool Pointer to ThreadPool.
 */
void pool_shutdown(ThreadPool *pool);

/**
 * @brief Resets per-thread performance counters to zero.
 *
 * @param pool Pointer to ThreadPool.
 */
void pool_reset_stats(ThreadPool *pool);

/**
 * @brief Prints load-balance statistics (tasks processed and busy time per thread).
 *
 * @param pool Pointer to ThreadPool.
 */
void pool_print_stats(const ThreadPool *pool);

/**
 * @brief Retrieves statistics for a specific thread index.
 *
 * @param pool Pointer to ThreadPool.
 * @param tid Thread ID [0, nthreads - 1].
 * @return Pointer to WorkerStats, or NULL if tid invalid.
 */
const WorkerStats* pool_get_stats(const ThreadPool *pool, int tid);

#ifdef __cplusplus
}
#endif

#endif /* THREAD_POOL_H */
