# Stream Cluster

**Fast Image Clustering**

A high-speed image clustering tool written in C, optimized for performance. It groups frames (images) into clusters based on Euclidean distance.

## Features

- **Fast & Optimized**: Written in C for speed.
- **FITS Support**: Uses CFITSIO to read and write FITS files (2D images or 3D cubes).
- **Customizable**: Adjustable distance limits, probability rewards, and cluster/frame limits.

## Dependencies

- **Compiler**: GCC (C99 standard)
- **Build System**: CMake (>= 3.10)
- **Libraries**: [CFITSIO](https://heasarc.gsfc.nasa.gov/fitsio/)

## Build

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

```bash
./image_cluster <rlim> [options] <fits_file>
```

### Arguments

- `<rlim>`: Distance limit for a frame to belong to a cluster (Euclidean distance).
  - Can be a standard floating-point value (e.g., `10.5`).
  - Can be in the format `a<val>` (e.g., `a1.5`). In this mode, the program scans the distance between consecutive frames, calculates the median distance, and sets `rlim = <val> * median_distance`.
- `<fits_file>`: Path to the FITS file containing frames to be clustered.
  - Can be a 2D or 3D image.
  - The last axis is treated as the frame number (e.g., a 100x200x500 cube contains 500 frames of 100x200).
  - Data format can be integers or floats.

### Options

- `-dprob <val>`: Incremental probability reward (default: `0.01`).
  - A higher value indicates that the next image is more likely to belong to the same cluster as the previous one.
- `-maxcl <val>`: Maximum number of clusters (default: `1000`).
  - The program stops if this limit is reached.
- `-maxim <val>`: Maximum number of frames to cluster (default: `100000`).
  - The program stops if this limit is reached.
- `-outdir <name>`: Specify output directory name.
  - Defaults to `<basename>.clusterdat`.

### Outputs

The program generates the following outputs:

1.  **`output.txt`**: ASCII file mapping frames to clusters.
    - Column 1: Frame Index
    - Column 2: Cluster Index
2.  **`anchors.fits`**: A FITS cube containing the anchor point (representative frame) for each cluster.
3.  **`cluster_<index>.fits`**: Separate FITS cubes for each cluster, containing all frames allocated to that cluster.
4.  **Standard Output**:
    - Number of clusters found.
    - Processing time.
    - Number of distance calculations (`framedist` calls).

## Algorithm Details

The algorithm groups frames into clusters based on Euclidean distance. Each cluster is defined by an **anchor point** (a specific frame). A frame is allocated to a cluster if it is within `rlim` distance of the anchor point.

### Notations

- `dcc(ci, cj)`: Cluster-to-cluster distance (Euclidean distance between anchor points).
- `dfc(fi, cj)`: Frame-to-cluster distance (distance between frame `fi` and anchor of cluster `cj`).
- `Ncl`: Current number of clusters (initially 0).
- `prob(ci)`: Probability that a new frame belongs to cluster `ci`.

### Steps

For each frame `fi` in the sequence:

1.  **Initialization**: If no clusters exist (`Ncl = 0`), create the first cluster (`c0`) using the current frame as the anchor. Set `prob(c0) = 1.0` and `Ncl = 1`. Proceed to next frame.
2.  **Normalize Probabilities**: Normalize `prob(cj)` so that the sum over all clusters is 1.0.
3.  **Sort Clusters**: Sort clusters by `prob(cj)` in descending order.
4.  **Check Candidates**: Iterate through sorted clusters:
    - Compute distance `dfc(fi, cj)`.
    - If `dfc < rlim`:
        - Allocate frame `fi` to cluster `cj`.
        - Increase `prob(cj)` by `deltaprob`.
        - Stop and proceed to next frame.
    - Use triangle inequality to prune impossible candidates (updates `clmembflag`):
        - If `|dcc(cj, cl) - dfc(fi, cj)| > rlim`, cluster `cl` cannot contain the frame.
5.  **Create New Cluster**: If the frame matches no existing cluster:
    - Create a new cluster with the frame as the anchor.
    - Set its probability to 1.0.
    - Compute distances from the new cluster to all existing clusters.
    - Increment `Ncl`.

## Code Structure

- **`src/cluster.c`**: Main logic and entry point. Implements the clustering algorithm and manages the loop over frames.
- **`src/framedistance.c`**: Contains `framedist()`, which computes Euclidean distance between frames.
- **`src/frameread.c`**: Handles FITS file input using CFITSIO. Provides `getframe()` to load images.
- **`src/common.h`**: Common data structures (`Frame`, `Cluster`) and definitions.
