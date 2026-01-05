# High-speed distance-based clustering

**Fast Image Clustering**

A high-speed image clustering tool written in C, optimized for performance. It groups frames (images) into clusters based on Euclidean distance.
High speed is achieved through:
- **Fixed cluster anchor points**. A cluster is defined by an immovable anchor point, created when a frame cannot be allocated to existing clusters. This is different from BIRCH, where cluster features evolve as frames are added, requiring many recomputations in the input high-dimension space.
- **Use of inter-cluster distances to quickly eliminate cluster membership options**. Each distance measurement defines a hypercircle, and clusters that do not intersect with this hypercircle are quickly discarded as possible members.
- **USe of previous distance computations to prioritize which distance to compute next**. The algorithm learns about the underlying geometry as frames are clustered. Recurring patterns (combinations of frame-to-cluster distances are identified and leveraged to update which frame-to-cluster distance should be computed next).
- **Short-term memory** Clusters to which recent frames have been allocated are given higher probability (referred to a `memprob` for memory probabilities). This is particularly useful when clustering "video" streams where a frame is most likely belonging to the same cluster as the last frame. 

The algorithm is optimized to minimize the number of distance computations, so it is very efficient in high-dimension space where the underlying data geometry/pattern is unknown.



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
- `<input_file>`: Path to the input file containing frames to be clustered.
  - **FITS file**:
    - Can be a 2D or 3D image.
    - The last axis is treated as the frame number (e.g., a 100x200x500 cube contains 500 frames of 100x200).
    - Data format can be integers or floats.
  - **ASCII text file (.txt)**:
    - Reads one sample per line.
    - Each line contains space-separated numbers representing coordinate values.
    - The number of columns corresponds to the dimension of the input space.

### Options

- `-dprob <val>`: Incremental probability reward (default: `0.01`).
  - A higher value indicates that the next image is more likely to belong to the same cluster as the previous one.
- `-maxcl <val>`: Maximum number of clusters (default: `1000`).
  - The program stops if this limit is reached.
- `-maxim <val>`: Maximum number of frames to cluster (default: `100000`).
  - The program stops if this limit is reached.
- `-outdir <name>`: Specify output directory name.
  - Defaults to `<basename>.clusterdat`.
- `-progress`: Print real-time progress updates.

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


## Identifying and leveraging data patterns

### The `distinfo` structure

As the algorithn works on a frame `k`, it computes distances to pre-existing clusters, and writes a distance information structure named `distinfo[k]` in source code. 

Its fields are:
- `distinfo[k].cl[i]` array of clusters indices to which distances have been computed. The index `i` is the sequential numbering for distance computations.
- `distinfo[k].fr[i]` corresponding frame index. Since each cluster is defined by its anchor point, which is itself a frame, clusters can be referred to by a frame index.
- `distinfo[k].dist[i]` distance value.
- `distinfo[k].ndist` number of distance computed until cluster memership resolved. The index `i` above will range from `0` to `distinfo[k].ndist-1`
- `distinfo[x].cluster` cluster to which the frame belongs. 

### Probabilities derived from `distinfo`

When processing a new frame `m`, after each distance computation, the `distinfo[m]` strucrure is compared to all previous `distinfo[k]` structures (`k = 0 ... m-1`) to update two quantities:
- The geometrical probability `gprob(m,cl)` that frame `m` belongs to cluster `cl`. 
- The geometrical match coefficient `gmatch(m,k)` that captures how well `distinfo[m]` and `distinfo[k]` match. A match value of zero indicates that `m` cannot possibly belong to the same cluster as frame `k`. A match value of 1.0 indicates no knowledge, and a match value above 1.0 indicates a good match.

### Computing the geometrical match coefficient `gmatch(m,k)`

Match values `gmatch(m,k)` between `distinfo[m]` and `distinfo[k]` are computed by comparing distances to the same cluster, using a function `fmatch(dr)`. 

For each pair of matching `distinfo[m].cl[i]` and `distinfo[k].cl[j]`, the normalized distance difference `dr=fabs(distinfo[m].dist[i]-distinfo[k].dist[j])/rlim` is computed. 
If `dr > 2`, per the triangle inequality, it is impossible for frames `m` and `k` to belong to the same cluster, so `fmatch(dr)=0`.  For values `dr<2`, we adopt `fmatch(dr) = a - (a-b) dr/2 `. with `a` (value at `dr=0`, has to be greater than 1) and `b` (value at `dr=2`, has to be between 0 and 1) are user-settable as options to the command. The default values are `a = 2` and `b = 0.5`. 
The geometrical match coefficient `gmatch(m,k)` is the product of all values of `fmatch(dr)` (one per pair of matching `distinfo[m].cl[i]` and `distinfo[k].cl[j]`).

If there are no matching pairs, `gmatch(m,k) = 1`.

### Computing the geometrical probabilityg `prob(m,cl)`

1. Initially, when starting to resolve a new frame `m`, all values `gprob(m,cl)` are the same, so that the sum of probabilities over `cl` is unity, and all `gmatch(m,k)` are set to 1.0.
2. For each new distance `distinfo[m].dist[i]` to cluster `distinfo[m].cl[i]` computed, all `gmatch(m,k)` values (`k= 0 ... m-1`) are updated as described in the previous section (multiplication by `fmatch(dr)` value(s)).
3. For each frame `k = 0 ... m-1`, set `cl = distinfo[x].cluster` (cluster to which frame `k` belongs), and multiply `gprob(m,cl)` by `gmatch(m,k)`.

The `prob(m,cl)` values will be upded as more distances ar computed, until the frame membership is resolved. As the computations proceed, `prob(m,cl)` will take zero values for clusters `cl` to which the frame `m` cannot belong, and will take large values for clusters `cl` that are likely hosts of frame `m`. 

### Using the geometrical probabilityg `prob(m,cl)` to improve clustering performance

The geometrical probabilities are used if the option `-gprob` is specified.  Additionally, options `fmatcha` and `-fmatchb` may be used to set the `a` and `b` parameters of the `fmatch` function.

The `prob(m,cl)` are then multiplied to the memory probabilitiies `memprob[cl]` to establish a ranking from the most to least probably cluster memberships. The next distance to be computed will be to the cluster with the highest membership probability.



## Code Structure

- **`src/cluster.c`**: Main logic and entry point. Implements the clustering algorithm and manages the loop over frames.
- **`src/framedistance.c`**: Contains `framedist()`, which computes Euclidean distance between frames.
- **`src/frameread.c`**: Handles FITS file input using CFITSIO. Provides `getframe()` to load images.
- **`src/common.h`**: Common data structures (`Frame`, `Cluster`) and definitions.
