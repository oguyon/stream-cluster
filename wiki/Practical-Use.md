# Practical Use Cases

This tool is designed for high-speed clustering of sequential image data or high-dimensional vectors. Below are some practical applications.

## 1. Astronomical Imaging

**Scenario**: You have a "cube" of FITS images (e.g., a time-series observation of a star field) and want to group frames based on seeing conditions or shifting alignment.

**Workflow**:
1.  **Input**: A 3D FITS cube where each slice is a frame.
2.  **Run**: Use `image_cluster` to group similar frames.
    ```bash
    ./image_cluster a1.5 input.fits -outdir results -avg
    ```
    *   `a1.5`: Auto-sets the radius to 1.5x the median frame-to-frame distance.
    *   `-avg`: Computes the average image (stack) for each cluster, which can improve Signal-to-Noise Ratio (SNR).
3.  **Result**: You get `average.fits` containing the "lucky imaging" stacks for each cluster.

## 2. Video Stream Analysis (Data Reduction)

**Scenario**: You have a stream of video frames (converted to ASCII feature vectors or raw pixels) and want to identify unique scenes or remove near-duplicates.

**Workflow**:
1.  **Preprocessing**: Convert frames to downscaled feature vectors (e.g., 64x64 pixels flattened).
2.  **Run**:
    ```bash
    ./image_cluster 500.0 video_feats.txt -maxcl 100 -tm 0.8
    ```
    *   `-tm 0.8`: Uses the transition matrix to predict the next scene based on history (80% weight), optimizing speed for structured video.
3.  **Result**: The tool identifies unique "anchor" frames. Frames within distance `500.0` of an anchor are grouped. You can use the `cluster_counts.txt` to find the most common scenes.

## 3. Noisy Data Categorization

**Scenario**: You have experimental sensor data (1D vectors) that drift over time.

**Workflow**:
1.  **Generate Test Data** (to tune parameters):
    ```bash
    ./image-cluster-mktxtseq 100 test.txt 2Dwalk -repeat 10 -noise 0.1
    ```
2.  **Tune Radius**:
    ```bash
    ./image_cluster -scandist test.txt
    ```
    Use the "Median" or "20%" percentile output to choose a tight `rlim`.
3.  **Cluster**:
    ```bash
    ./image_cluster <chosen_rlim> sensor_data.txt -gprob
    ```
    `-gprob` is useful here if the drift is continuous, as it learns the trajectory of the sensor data.

## 4. High-Dimensional / Expensive Metric Clustering

**Scenario**: You are clustering vectors where the distance metric is extremely expensive to compute (or the dimensionality is very high, e.g., >1000).

**Workflow**:
1.  **Run**:
    ```bash
    ./image_cluster <rlim> vectors.txt -te5
    ```
    *   `-te5`: Enables 5-point pruning. While this adds CPU overhead per candidate check, it significantly reduces the number of distance calculations (by ~45% in some cases). This is a net win if calculating the distance itself is the bottleneck.

## Tips for Best Results

*   **Auto-Tuning**: Always start with `-scandist` to understand the scale of distances in your dataset.
*   **Geometric Probability**: Use `-gprob` for time-series data where the signal evolves smoothly (e.g., drifting sensors, planetary rotation).
*   **Transition Matrix**: Use `-tm` for data with repeating, predictable sequences (e.g. video loops, cyclic processes).
*   **Memory**: For extremely large datasets (>1M frames), ensure you have enough RAM as `FrameInfo` stores history for all frames.
