/**
 * @file kmeans.h
 * @brief Integration contract for Person 4: Parallel K-Means Engine.
 */

#ifndef KMEANS_H
#define KMEANS_H

#include "thread_pool.h"
#include "accumulator.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared context passed via Task.ctx into the worker thread callback.
 */
typedef struct {
    const double *points;        /**< Input dataset array [N * D] */
    const double *centroids;     /**< Current centroid positions [K * D] */
    int *assignments;            /**< Cluster labels assigned to points [N] */
    size_t n_points;             /**< Total points N */
    size_t k;                    /**< Number of clusters K */
    size_t d;                    /**< Dimensions D */
    AlignedAccumulator *local_accs; /**< Array of P thread-local accumulators */
} KMeansContext;

/**
 * @brief Worker callback function executed dynamically by the thread pool.
 *
 * Implements the distance calculation and assignment for points [start_idx, end_idx).
 *
 * @param task The task specifying point range.
 * @param tid Worker thread index [0, P - 1].
 * @param ctx Pointer to KMeansContext.
 */
void kmeans_assign_chunk_fn(const Task *task, int tid, void *ctx);

/**
 * @brief Parallel K-Means execution driver using Person 1's Thread Pool.
 *
 * @param points Array of N * D data points.
 * @param n Number of data points.
 * @param d Dimensions.
 * @param k Number of clusters.
 * @param max_iters Maximum iterations.
 * @param tolerance Convergence tolerance.
 * @param nthreads Worker thread count P.
 * @param chunk_size Points per dynamic task chunk.
 * @param out_centroids Output array of K * D centroids.
 * @param out_assignments Output array of N cluster labels.
 * @return int Number of iterations until convergence.
 */
int kmeans_parallel_run(
    const double *points,
    size_t n,
    size_t d,
    size_t k,
    int max_iters,
    double tolerance,
    int nthreads,
    size_t chunk_size,
    double *out_centroids,
    int *out_assignments
);

#ifdef __cplusplus
}
#endif

#endif /* KMEANS_H */
