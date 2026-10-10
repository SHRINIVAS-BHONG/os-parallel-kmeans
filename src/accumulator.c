#include "accumulator.h"
#include "aligned_memory.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

AlignedAccumulator* accumulators_create(int nthreads, size_t k, size_t d) {
    if (nthreads <= 0) return NULL;

    // Allocate the array of AlignedAccumulator properly aligned to 64 bytes.
    AlignedAccumulator* accs = (AlignedAccumulator*)aligned_malloc(nthreads * sizeof(AlignedAccumulator), 64);
    if (!accs) return NULL;

    for (int i = 0; i < nthreads; i++) {
        accs[i].k = k;
        accs[i].d = d;

        // Allocate sums: pad to 64 bytes to prevent false sharing across thread arrays
        size_t sums_size = k * d * sizeof(double);
        size_t padded_sums = (sums_size + 63) & ~63;
        accs[i].sums = (double*)aligned_malloc(padded_sums, 64);

        // Allocate counts: pad to 64 bytes
        size_t counts_size = k * sizeof(size_t);
        size_t padded_counts = (counts_size + 63) & ~63;
        accs[i].counts = (size_t*)aligned_malloc(padded_counts, 64);

        // Clear to 0 initially
        if (accs[i].sums) memset(accs[i].sums, 0, padded_sums);
        if (accs[i].counts) memset(accs[i].counts, 0, padded_counts);
    }
    return accs;
}

void accumulators_reset(AlignedAccumulator *accs, int nthreads) {
    if (!accs) return;
    for (int i = 0; i < nthreads; i++) {
        size_t sums_size = accs[i].k * accs[i].d * sizeof(double);
        if (accs[i].sums) memset(accs[i].sums, 0, sums_size);
        
        size_t counts_size = accs[i].k * sizeof(size_t);
        if (accs[i].counts) memset(accs[i].counts, 0, counts_size);
    }
}

double accumulators_reduce(const AlignedAccumulator *accs, int nthreads, double *out_centroids) {
    if (nthreads <= 0 || !accs || !out_centroids) return 0.0;
    
    size_t k = accs[0].k;
    size_t d = accs[0].d;
    
    double* global_sums = (double*)calloc(k * d, sizeof(double));
    size_t* global_counts = (size_t*)calloc(k, sizeof(size_t));
    
    if (!global_sums || !global_counts) {
        if (global_sums) free(global_sums);
        if (global_counts) free(global_counts);
        return 0.0;
    }
    
    for (int i = 0; i < nthreads; i++) {
        for (size_t cluster = 0; cluster < k; cluster++) {
            global_counts[cluster] += accs[i].counts[cluster];
            for (size_t dim = 0; dim < d; dim++) {
                global_sums[cluster * d + dim] += accs[i].sums[cluster * d + dim];
            }
        }
    }
    
    double max_shift = 0.0;
    for (size_t cluster = 0; cluster < k; cluster++) {
        if (global_counts[cluster] > 0) {
            double shift_sq = 0.0;
            for (size_t dim = 0; dim < d; dim++) {
                double new_val = global_sums[cluster * d + dim] / (double)global_counts[cluster];
                double old_val = out_centroids[cluster * d + dim];
                double diff = new_val - old_val;
                shift_sq += diff * diff;
                
                out_centroids[cluster * d + dim] = new_val;
            }
            double shift = sqrt(shift_sq);
            if (shift > max_shift) {
                max_shift = shift;
            }
        }
    }
    
    free(global_sums);
    free(global_counts);
    return max_shift;
}

void accumulators_destroy(AlignedAccumulator *accs, int nthreads) {
    if (!accs) return;
    for (int i = 0; i < nthreads; i++) {
        if (accs[i].sums) aligned_free(accs[i].sums);
        if (accs[i].counts) aligned_free(accs[i].counts);
    }
    aligned_free(accs);
}
