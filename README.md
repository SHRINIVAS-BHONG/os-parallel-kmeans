# High-Performance Multi-Threaded Parallel K-Means

A high-performance parallel computing runtime for K-Means clustering designed with operating system concurrency, synchronization, and memory-hierarchy principles in C using POSIX Threads (`pthreads`).

## Architecture Overview

```
                    ┌──────────────────────────┐
                    │       Input Dataset      │
                    └────────────┬─────────────┘
                                 │
                                 ▼
                    ┌──────────────────────────┐
                    │   Dataset / Task Manager │
                    └────────────┬─────────────┘
                                 │ creates chunks/tasks
                                 ▼
             ┌────────────────────────────────────────┐
             │       Dynamic Task Queue               │
             │   mutex + condition variables          │
             └───────────────┬────────────────────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
         ┌─────────┐    ┌─────────┐    ┌─────────┐
         │Worker 1 │    │Worker 2 │ .. │Worker N │
         │ pthread │    │ pthread │    │ pthread │
         └────┬────┘    └────┬────┘    └────┬────┘
              │              │              │
              ▼              ▼              ▼
       Local Accumulator Local Accumulator Local Accumulator
       Cache-line padded  Cache-line padded  Cache-line padded
              │              │              │
              └──────────────┼──────────────┘
                             ▼
                    ┌──────────────────┐
                    │ Global Barrier   │
                    │ pthread_barrier  │
                    └────────┬─────────┘
                             ▼
                    ┌──────────────────┐
                    │ Merge / Update   │
                    │ Cluster Centers  │
                    └────────┬─────────┘
                             │
                             ▼
                       Next Iteration
```

## Modular Team Division

| Role | Module | Focus Area & Deliverables |
|---|---|---|
| **Person 1** | **Thread Pool & Dynamic Scheduler** | Worker thread lifecycle, bounded ring-buffer task queue, `pthread_mutex`, condition variables, dynamic task distribution (`src/thread_pool.c`, `src/task_queue.c`, `include/thread_pool.h`, `include/task_queue.h`) |
| **Person 2** | **Cache Optimization & Memory Architecture** | Cache-line alignment (`alignas(64)`), padding, false-sharing elimination, spatial locality (`src/aligned_memory.c`, `src/accumulator.c`) |
| **Person 3** | **Synchronization & Local Accumulation** | Thread-local accumulators, lock contention reduction, phase barrier synchronization (`pthread_barrier_t`), reduction (`src/synchronization.c`) |
| **Person 4** | **K-Means Engine & Benchmarking** | Algorithmic workflow, sequential vs. static vs. dynamic comparison, scaling experiments, convergence checks (`src/kmeans.c`, `benchmarks/benchmark.c`) |

## Directory Structure

```
os-parallel-kmeans/
├── src/                  # Source implementation files
├── include/              # Header definitions and public APIs
├── benchmarks/           # Benchmark suites & result plots/logs
│   └── results/
├── datasets/             # Test datasets (10K - 5M points)
├── scripts/              # Data generation and benchmarking scripts
├── .gitignore            # Git ignore file
├── Makefile              # Build configuration
└── README.md             # Project overview and documentation
```
