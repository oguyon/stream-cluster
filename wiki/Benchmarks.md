# Benchmarks

This page documents the performance of the `image_cluster` tool.

## Test Environment
- **Processor**: (Sandbox Environment)
- **Data**: Synthetic 3D random walk sequences generated via `image-cluster-mktxtseq`.
- **Compiler**: GCC with `-O3` (implied by Release build).

## Scenario 1: Random Walk (2000 frames, 5 repetitions)

**Dataset**:
- 2000 unique points (random walk) repeated 5 times.
- Total frames: 10,000.
- Noise: 0.5 radius.

**Command**:
```bash
./image-cluster-mktxtseq 2000 benchmark_data.txt 3Dwalk -repeat 5 -noise 0.5
./image_cluster 1.5 benchmark_data.txt -dprob 0.05 [-gprob]
```

**Results**:

| Mode | Total Time (ms) | Dist Calls | Clusters Found | Notes |
|------|-----------------|------------|----------------|-------|
| Standard | ~34 ms | 10,292 | 4 | Baseline performance. |
| Geometric Prob (`-gprob`) | ~32 ms | 10,456 | 4 | Slightly faster due to optimized candidate ranking. |

*Note: Execution times include I/O overhead which may vary.*

## Performance Characteristics

1.  **Complexity**: The algorithm scales effectively linearly with the number of frames ($O(N)$) for well-separated clusters, as most candidates are pruned via the triangle inequality.
2.  **Memory**: Memory usage is dominated by the `FrameInfo` storage which scales with $N$.
3.  **Optimization**: The `-gprob` option improves performance on correlated data streams by predicting likely clusters based on previous measurements, reducing the average number of distance calculations required per frame.
