# Benchmarks

This page documents the performance of the `image_cluster` tool.

## Test Environment
- **Data**: Synthetic 128-dimensional sequences generated via `image-cluster-mktxtseq`.
- **Compiler**: GCC/Clang with `-O3 -march=native -funroll-loops` (AVX2 enabled).

## Scenario 1: Random Walk (1000 frames)

**Dataset**:
- 1000 frames, 128 dimensions.
- Random walk pattern (highly correlated).
- `rlim=0.5`.

**Results**:

| Mode | Total Time (ms) | Dist Calls | Notes |
|------|-----------------|------------|-------|
| Standard | ~39 ms | 1,846 | Baseline performance. |
| Transition Matrix (`-tm 1.0`) | ~38 ms | 1,832 | Slight reduction in calls by using history. |
| Geometric Prob (`-gprob`) | ~37 ms | 1,846 | Efficient candidate ranking. |

## Scenario 2: Uniform Random (1000 frames)

**Dataset**:
- 1000 frames, 128 dimensions.
- Uniform random distribution (uncorrelated, "hard" clustering).
- `rlim=2.5`.

**Results**:

| Mode | Total Time (ms) | Dist Calls | Notes |
|------|-----------------|------------|-------|
| Standard (Triangle Ineq) | ~756 ms | 985,683 | Fast metric evaluation (AVX2) dominates. |
| 4-Point Pruning (`-te4`) | ~11,811 ms | 605,013 | Reduced calls by ~40%, but high logic overhead. |
| 5-Point Pruning (`-te5`) | ~98,744 ms | 559,962 | Reduced calls by ~45%, but very high logic overhead. |

## Performance Characteristics

1.  **Metric vs Logic**: The distance metric (`framedist`) is heavily optimized (AVX2). For simple Euclidean distance in RAM, it is often faster to compute the distance than to run complex pruning logic (`-te4`, `-te5`).
2.  **Pruning Power**: `-te5` is mathematically the strongest pruner, significantly reducing the number of metric evaluations. It is recommended when the distance metric is **computationally expensive** (e.g., complex image similarity, disk-based retrieval) or when dimensions are extremely high (>1000) such that triangle inequality fails.
3.  **Correlated Data**: For time-series data (random walk, video), standard pruning combined with `-tm` or `-gprob` offers the best balance of speed and accuracy.
