# Welcome to the Image Cluster Wiki

This project provides a high-speed, distance-based clustering tool optimized for sequential data (images, sensor logs, etc.).

## Sections

*   **[Benchmarks](Benchmarks.md)**: Performance analysis and timing results on synthetic datasets.
*   **[Practical Use](Practical-Use.md)**: Real-world scenarios, workflows, and tips for getting the most out of the tool.
*   **[Algorithm Details](Algorithm-Details.md)**: Detailed description of the clustering algorithm and optimizations.

## Quick Start

```bash
# Build
mkdir build && cd build
cmake .. && make

# Run on sample data
./image_cluster a1.5 input.txt -outdir results
```

See the [README](../README.md) for detailed installation and usage instructions.
