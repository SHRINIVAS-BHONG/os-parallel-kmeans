/**
 * @file task_queue.h
 * @brief Thread-safe bounded circular ring-buffer task queue for dynamic scheduling.
 *
 * Implements the producer-consumer pattern using POSIX Threads (pthreads)
 * mutex and condition variables. Supports blocking push/pop, non-blocking try-pop,
 * and clean shutdown broadcasting.
 */

#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes for queue operations */
#define QUEUE_OK            0
#define QUEUE_ERR_FULL     -1
#define QUEUE_ERR_EMPTY    -2
#define QUEUE_ERR_CLOSED   -3
#define QUEUE_ERR_INVALID  -4
#define QUEUE_ERR_NOMEM    -5

/**
 * @brief Work unit representing a slice of data points to process.
 */
typedef struct {
    size_t start_idx;  /**< Start index in dataset [start_idx, end_idx) */
    size_t end_idx;    /**< End index in dataset (exclusive) */
    int task_id;       /**< Identifier for debugging and profiling */
    void *ctx;         /**< User context pointer (e.g., KMeans state) */
} Task;

/**
 * @brief Thread-safe bounded circular task queue.
 */
typedef struct {
    Task *buf;                /**< Circular buffer storage */
    size_t cap;               /**< Queue capacity (enforced power of two) */
    size_t head;              /**< Pop cursor */
    size_t tail;              /**< Push cursor */
    size_t count;             /**< Current number of tasks stored */
    bool closed;              /**< Flag indicating shutdown in progress */
    pthread_mutex_t lock;     /**< Mutex guarding head, tail, count, and closed */
    pthread_cond_t not_empty; /**< Condition variable signaled when task is pushed */
    pthread_cond_t not_full;  /**< Condition variable signaled when slot is freed */
} TaskQueue;

/**
 * @brief Initializes a bounded task queue.
 *
 * @param q Pointer to TaskQueue instance.
 * @param cap Requested capacity (will be rounded up to next power of 2, min 2).
 * @return QUEUE_OK on success, or negative error code.
 */
int queue_init(TaskQueue *q, size_t cap);

/**
 * @brief Enqueues a task into the ring buffer (blocking if full).
 *
 * If the queue is full, blocks on not_full condition variable.
 * Signals not_empty upon successful enqueue.
 *
 * @param q Pointer to TaskQueue.
 * @param task The task to enqueue.
 * @return QUEUE_OK on success, QUEUE_ERR_CLOSED if queue was closed.
 */
int queue_push(TaskQueue *q, Task task);

/**
 * @brief Dequeues a task from the ring buffer (blocking if empty).
 *
 * Blocks on not_empty condition variable if the queue is empty.
 * Signals not_full upon successful dequeue.
 * If queue is closed and empty, returns false.
 *
 * @param q Pointer to TaskQueue.
 * @param out_task Output pointer to store the popped task.
 * @return true if a task was dequeued, false if queue is closed and empty.
 */
bool queue_pop(TaskQueue *q, Task *out_task);

/**
 * @brief Non-blocking dequeue attempt.
 *
 * @param q Pointer to TaskQueue.
 * @param out_task Output pointer to store the popped task.
 * @return true if a task was immediately dequeued, false otherwise.
 */
bool queue_try_pop(TaskQueue *q, Task *out_task);

/**
 * @brief Closes the queue and unblocks all waiting threads.
 *
 * Subsequent pushes will fail with QUEUE_ERR_CLOSED.
 * Subsequent pops will continue draining remaining items until empty, then return false.
 *
 * @param q Pointer to TaskQueue.
 */
void queue_close(TaskQueue *q);

/**
 * @brief Destroys mutexes, condition variables, and frees queue memory.
 *
 * @param q Pointer to TaskQueue.
 */
void queue_destroy(TaskQueue *q);

/**
 * @brief Returns the current number of elements in the queue.
 *
 * @param q Pointer to TaskQueue.
 * @return size_t Count of items.
 */
size_t queue_size(TaskQueue *q);

/**
 * @brief Checks if queue is currently empty.
 *
 * @param q Pointer to TaskQueue.
 * @return true if empty, false otherwise.
 */
bool queue_is_empty(TaskQueue *q);

#ifdef __cplusplus
}
#endif

#endif /* TASK_QUEUE_H */
