# High-speed distance-based clustering

**Fast Image Clustering**

A high-speed image clustering tool written in C, optimized for performance. It groups frames (images) into clusters based on Euclidean distance.

High speed is achieved through:
- **Fixed cluster anchor points**: A cluster is defined by an immovable anchor point, created when a frame cannot be allocated to existing clusters. This differs from algorithms like BIRCH, where cluster features evolve, requiring frequent recomputations.
- **Inter-cluster distance pruning**: Each distance measurement defines a hypercircle. Clusters not intersecting this hypercircle are quickly discarded using the triangle inequality.
- **Geometric learning**: The algorithm leverages previous distance computations to prioritize likely candidates. Recurring patterns in frame-to-cluster distances are identified to update selection probabilities.
- **Short-term memory**: Clusters receiving recent allocations are given higher probability (`prob`). This is ideal for correlated streams (e.g., video) where consecutive frames often belong to the same cluster.

## Features

- **Fast & Optimized**: Written in C99 for performance.
- **FITS & ASCII Support**: Handles FITS images/cubes (via CFITSIO) and ASCII text files.
- **Customizable**: Tunable distance limits (`rlim`), probability rewards, and geometric matching parameters.
- **Test Generation**: Includes a utility to generate synthetic test sequences.

## Dependencies

- **Compiler**: GCC (C99 standard)
- **Build System**: CMake (>= 3.10)
- **Libraries**: [CFITSIO](https://heasarc.gsfc.nasa.gov/fitsio/) (required for FITS support)

## Build

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

```bash
./image_cluster <rlim> [options] <input_file>
```

### Arguments

- `<rlim>`: Distance limit (radius) for a frame to belong to a cluster.
  - **Float**: Fixed value (e.g., `10.5`).
  - **Auto**: `a<val>` (e.g., `a1.5`). Scans distances between consecutive frames to determine the median, then sets `rlim = val * median_distance`.
- `<input_file>`: Path to the input data.
  - **FITS**: 2D image or 3D cube (last axis = frame index).
  - **ASCII**: `.txt` file with one sample per line (space-separated coordinates).

### Options

**Clustering Parameters:**
- `-dprob <val>`: Incremental probability reward for the assigned cluster (default: `0.01`).
- `-maxcl <val>`: Maximum number of clusters allowed (default: `1000`).
- `-maxim <val>`: Maximum number of frames to process (default: `100000`).
- `-gprob`: Enable **Geometric Probability** (ranks clusters based on historical distance patterns).
- `-fmatcha <val>`: Parameter `a` for the geometric match function `fmatch` (default: `2.0`).
- `-fmatchb <val>`: Parameter `b` for the geometric match function `fmatch` (default: `0.5`).

**Output & Logging:**
- `-outdir <name>`: Specify output directory (default: `<basename>.clusterdat`).
- `-avg`: Compute and save the average frame for each cluster.
- `-distall`: Log all computed distances to `distall.txt`.
- `-fitsout`: Force FITS output format even if input is ASCII.
- `-progress`: Print real-time progress updates.
- `-verbose`: Enable verbose output.
- `-veryverbose`: Enable detailed output (includes `fmatch`/`gprob` details).

**Analysis Modes:**
- `-scandist`: Bypass clustering; scan consecutive frame distances and print statistics (Min, Median, Max).

### Outputs

Outputs are written to the output directory:

1.  **`frame_membership.txt`**: Mapping of Frame Index to Cluster Index.
2.  **`anchors.fits` / `anchors.txt`**: The anchor points (representative frames) for each cluster.
3.  **`cluster_<index>.fits` / `.txt`**: Frames allocated to each cluster.
4.  **`cluster_counts.txt`**: Number of frames in each cluster.
5.  **`dcc.txt`**: Matrix of distances between cluster anchors.
6.  **`average.fits` / `.txt`** (Optional): Average frame content per cluster.
7.  **`distall.txt`** (Optional): Log of all distance computations.
8.  **Standard Output**: Summary of clusters found, processing time, and total distance calculations.

## Algorithm Details

The algorithm groups frames based on Euclidean distance. Each cluster `cj` is defined by an **anchor frame**. A frame `fi` belongs to `cj` if `dist(fi, cj) < rlim`.

### Notations

- `dcc(ci, cj)`: Distance between anchors of cluster `ci` and `cj`.
- `dfc(fi, cj)`: Distance between frame `fi` and anchor of cluster `cj`.
- `Ncl`: Current number of clusters.
- `prob(cj)`: "Memory" probability of cluster `cj` (prioritizes recently used clusters).
- `gprob(fi, cj)`: Geometric probability derived from historical distance patterns.

### Steps

For each frame `fi`:

1.  **Initialization**: If `Ncl = 0`, create Cluster 0 using `fi` as anchor. Set `prob(c0) = 1.0`.
2.  **Normalize Probabilities**: Normalize `prob(cj)` so they sum to 1.0.
3.  **Rank Candidates**: Sort clusters by total probability.
    - If `-gprob` is used, Rank = `prob(cj) * gprob(fi, cj)`.
    - Otherwise, Rank = `prob(cj)`.
4.  **Check Candidates**: Iterate through ranked clusters:
    - Compute `dfc(fi, cj)`.
    - If `dfc < rlim`:
        - **Assign**: `fi` -> `cj`.
        - **Reward**: `prob(cj) += dprob`.
        - Update `gprob` history.
        - Proceed to next frame.
    - **Prune**: Use triangle inequality. If `|dcc(cj, cl) - dfc(fi, cj)| > rlim`, cluster `cl` cannot contain `fi`.
5.  **Create New Cluster**: If no existing cluster matches:
    - Create new cluster with `fi` as anchor.
    - Initialize `prob = 1.0`.
    - Compute `dcc` to all existing clusters.

## Identifying and Leveraging Data Patterns

### The `FrameInfo` Structure

The algorithm maintains a history of computations in the `FrameInfo` structure (implemented as `frame_infos[k]` in `src/cluster.c`). For a processed frame `k`, it stores:

- `cluster_indices`: Array of cluster indices `cj` for which a distance was computed.
- `distances`: Array of the corresponding measured distances.
- `num_dists`: Number of distance computations performed for this frame.
- `assignment`: The final cluster ID assigned to frame `k`.

### Probabilities Derived from `FrameInfo` (`gprob`)

When processing a new frame `m`, the algorithm compares its partial distance measurements against the history of previous frames `k`. This updates the **Geometrical Probability** `gprob(m, cl)`.

The logic is based on the **Geometrical Match Coefficient** `gmatch(m, k)`, which quantifies how similarly frames `m` and `k` relate to the known clusters.

### Computing `gmatch(m, k)`

For every cluster `c` that *both* frames `m` and `k` have measured a distance to:
1.  Calculate the normalized difference:
    ```
    dr = |dist(m, c) - dist(k, c)| / rlim
    ```
2.  Compute the match factor `fmatch(dr)`:
    - If `dr > 2.0`: `0.0` (Triangle inequality violation; they cannot belong to the same cluster).
    - If `dr <= 2.0`: `a - (a - b) * dr / 2`
      - `a` (default 2.0): Reward for exact match (`dr=0`).
      - `b` (default 0.5): Value at the pruning limit (`dr=2`).

The total `gmatch(m, k)` is the product of `fmatch(dr)` for all shared clusters.

### Updating `gprob(m, cl)`

1.  Initialize `gprob(m, cl) = 1.0` for all clusters.
2.  As distances are computed for `m`, update `gprob` using `gmatch`:
    - For each previous frame `k` that shares a distance measurement with `m`:
        - Identify the cluster `target = assignment[k]`.
        - Update: `gprob(m, target) *= gmatch(m, k)`.

Clusters with high `gprob` are prioritized in the candidate list, reducing the number of expensive distance calculations (`framedist` calls) needed to find the correct cluster.

## Test Data Generation

A utility is included to generate synthetic test sequences:

```bash
./image-cluster-mktxtseq <N> <filename> <pattern> [options]
```
Patterns: `2Drandom`, `2Dcircle`, `2Dwalk`, `2Dspiral`, `3Drandom`, `3Dwalk`, `3Dsphere`, etc.
Options:
- `-repeat <M>`: Repeat the pattern M times.
- `-noise <R>`: Add random noise of radius R.
- `-shuffle`: Shuffle the output order.

## Code Structure

- **`src/cluster.c`**: Main logic and entry point.
- **`src/framedistance.c`**: Euclidean distance computation.
- **`src/frameread.c`**: Input handling (FITS and ASCII).
- **`src/mktestseq.c`**: Test data generator.
- **`src/common.h`**: Common definitions (`Frame`, `Cluster`).
