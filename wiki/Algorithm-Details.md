# Algorithm Details

The algorithm groups frames based on Euclidean distance. Each cluster `cj` is defined by an **anchor frame**. A frame `fi` belongs to `cj` if `dist(fi, cj) < rlim`.

## Notations

- `dcc(ci, cj)`: Distance between anchors of cluster `ci` and `cj`.
- `dfc(fi, cj)`: Distance between frame `fi` and anchor of cluster `cj`.
- `Ncl`: Current number of clusters.
- `prob(cj)`: "Memory" probability of cluster `cj` (prioritizes recently used clusters).
- `gprob(fi, cj)`: Geometric probability derived from historical distance patterns.

## Steps

**Initialization**: Set `Ncl = 1`, create Cluster 0 using `f0` as anchor. Set `prob(c0) = 1.0`.

Then loop over frame index `fi` until all frames clustered:
1.  **Normalize Probabilities**: Normalize `prob(cj)` so they sum to 1.0.
2.  **Rank Candidates**: Sort clusters by total probability.
    - If `-gprob` is used, rank derived from sorting `prob(cj) * gprob(fi, cj)`
    - Otherwise,  rank derived from sorting `prob(cj)`.
3.  **Check Candidates**: Iterate through ranked clusters:
    - Compute `dfc(fi, cj)`.
    - If `dfc < rlim` (in cluster):
        - **Assign**: `fi` -> `cj`.
        - **Reward**: `prob(cj) += dprob`.
        - Update `gprob` history.
        - Proceed to next frame.
    - If `dfc > rlim` (not in cluster):
        - **Prune**: Use triangle inequality. If `|dcc(cj, cl) - dfc(fi, cj)| > rlim`, cluster `cl` cannot contain `fi`.
        - If number of possible members is strictly greater than zero, go to step 2
        - Otherwise, go to step 4
4.  **Create New Cluster**: If no existing cluster matches:
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

## Prediction with Pattern Detection (Time Series)

For time-series data or video streams where cluster transitions often follow a pattern, the `-pred[len,h,n]` option allows the algorithm to "predict" the next cluster based on recent history.

### Mechanism

The algorithm uses the sequence of recent cluster assignments to find similar contexts in the past:

1.  **Pattern Identification**: It identifies the sequence of cluster indices assigned to the last `len` frames (default: 10).
2.  **History Scan**: It scans the assignments of the last `h` frames (default: 1000) to find previous occurrences of this sequence.
3.  **Prediction**: For every match found, it looks at the *following* cluster assignment. These "next clusters" are collected and ranked by frequency.
4.  **Priority Check**: The top `n` (default: 2) most frequent next clusters are computed and checked **first**, bypassing the standard probability ranking.

If one of these predicted clusters is a match (`dist < rlim`), the frame is assigned immediately. If not, the algorithm falls back to the standard probability-based ranking (excluding the candidates that were already checked and rejected).

### Parameters

- `len`: Length of the sequence pattern.
- `h`: History horizon (how far back to search).
- `n`: Number of predicted candidates to test.

### Example

Assume 16 frames have been clustered with indices:
`0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 2, 2, 0, 1, 2, 0`

We are processing **Frame #16** (the 17th frame) with option `-pred[2,12,1]`.
- **Current Pattern (`len=2`)**: The last two assignments are `2, 0`.
- **History Scan (`h=12`)**: We look at the last 12 frames (indices 4 to 15). The sequence `2, 0` occurred at:
    - Frames 5, 6 (followed by **1** at frame 7).
    - Frames 8, 9 (followed by **2** at frame 10).
    - Frames 11, 12 (followed by **1** at frame 13).
- **Prediction**: Cluster **1** occurred twice. Cluster **2** occurred once.
- **Action**: The algorithm tests Cluster **1** first.
    - If Frame #16 belongs to Cluster 1, it is assigned immediately.
    - If not, it proceeds to standard ranking (checking Cluster 2 next if probability suggests, or others).
