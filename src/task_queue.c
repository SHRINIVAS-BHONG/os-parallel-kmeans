/**
 * @file task_queue.c
 * @brief Thread-safe bounded circular ring-buffer task queue implementation.
 */

#include "task_queue.h"
#include <stdlib.h>
#include <stdint.h>

/**
 * @brief Computes the next power of 2 greater than or equal to n (minimum 2).
 */
static size_t next_power_of_two(size_t n) {
    if (n < 2) return 2;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFFU
    n |= n >> 32;
#endif
    return n + 1;
}

int queue_init(TaskQueue *q, size_t cap) {
    if (!q) return QUEUE_ERR_INVALID;

    q->cap = next_power_of_two(cap);
    q->buf = (Task *)malloc(q->cap * sizeof(Task));
    if (!q->buf) {
        return QUEUE_ERR_NOMEM;
    }

    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->closed = false;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        free(q->buf);
        q->buf = NULL;
        return QUEUE_ERR_INVALID;
    }

    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        free(q->buf);
        q->buf = NULL;
        return QUEUE_ERR_INVALID;
    }

    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        pthread_cond_destroy(&q->not_empty);
        pthread_mutex_destroy(&q->lock);
        free(q->buf);
        q->buf = NULL;
        return QUEUE_ERR_INVALID;
    }

    return QUEUE_OK;
}

int queue_push(TaskQueue *q, Task task) {
    if (!q) return QUEUE_ERR_INVALID;

    pthread_mutex_lock(&q->lock);

    /* Block if queue is full until space is made or queue is closed */
    while (q->count == q->cap && !q->closed) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }

    if (q->closed) {
        pthread_mutex_unlock(&q->lock);
        return QUEUE_ERR_CLOSED;
    }

    /* Enqueue at tail */
    q->buf[q->tail] = task;
    q->tail = (q->tail + 1) & (q->cap - 1);
    q->count++;

    /* Wake one worker waiting for a task */
    pthread_cond_signal(&q->not_empty);

    pthread_mutex_unlock(&q->lock);
    return QUEUE_OK;
}

bool queue_pop(TaskQueue *q, Task *out_task) {
    if (!q) return false;

    pthread_mutex_lock(&q->lock);

    /* Block if queue is empty until an item arrives or queue is closed */
    while (q->count == 0 && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }

    /* If queue is closed and completely drained, tell worker to exit */
    if (q->count == 0 && q->closed) {
        pthread_mutex_unlock(&q->lock);
        return false;
    }

    /* Dequeue from head */
    if (out_task) {
        *out_task = q->buf[q->head];
    }
    q->head = (q->head + 1) & (q->cap - 1);
    q->count--;

    /* Signal producer that a slot has become available */
    pthread_cond_signal(&q->not_full);

    pthread_mutex_unlock(&q->lock);
    return true;
}

bool queue_try_pop(TaskQueue *q, Task *out_task) {
    if (!q) return false;

    pthread_mutex_lock(&q->lock);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return false;
    }

    if (out_task) {
        *out_task = q->buf[q->head];
    }
    q->head = (q->head + 1) & (q->cap - 1);
    q->count--;

    pthread_cond_signal(&q->not_full);

    pthread_mutex_unlock(&q->lock);
    return true;
}

void queue_close(TaskQueue *q) {
    if (!q) return;

    pthread_mutex_lock(&q->lock);
    q->closed = true;
    /* Wake all sleeping consumers and producers to allow clean exit */
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

void queue_destroy(TaskQueue *q) {
    if (!q) return;

    pthread_mutex_lock(&q->lock);
    q->closed = true;
    pthread_mutex_unlock(&q->lock);

    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);

    if (q->buf) {
        free(q->buf);
        q->buf = NULL;
    }
    q->cap = 0;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
}

size_t queue_size(TaskQueue *q) {
    if (!q) return 0;
    pthread_mutex_lock(&q->lock);
    size_t sz = q->count;
    pthread_mutex_unlock(&q->lock);
    return sz;
}

bool queue_is_empty(TaskQueue *q) {
    return queue_size(q) == 0;
}
