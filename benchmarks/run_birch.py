import sys
import time
import numpy as np
from sklearn.cluster import Birch

def load_data(filepath):
    """Loads space-separated data from a file."""
    data = []
    with open(filepath, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if parts:
                data.append([float(x) for x in parts])
    return np.array(data)

def run_birch(filepath, threshold=0.1, branching_factor=50, n_clusters=None):
    """Runs BIRCH clustering and prints stats."""
    try:
        X = load_data(filepath)
    except Exception as e:
        print(f"Error loading {filepath}: {e}")
        return

    print(f"Running BIRCH on {filepath} with {len(X)} points...")
    
    # BIRCH parameters:
    # threshold: The radius of the subcluster obtained by merging a new sample and the closest subcluster should be lesser than the threshold.
    # branching_factor: Maximum number of CF subclusters in each node.
    # n_clusters: Number of clusters after the final clustering step, which treats the subclusters from the leaves as new samples. None means no final clustering step.
    
    start_time = time.time()
    
    # Using n_clusters=None to let BIRCH discover the number of clusters based on the threshold
    # This aligns better with the stream-cluster behavior (radius limit)
    # Note: sklearn.cluster.Birch does not support custom distance metrics or
    # counting distance calls. It fundamentally relies on Euclidean distance properties
    # (N, Linear Sum, Squared Sum) to efficiently update Clustering Features (CF).
    brc = Birch(threshold=threshold, branching_factor=branching_factor, n_clusters=n_clusters)
    
    brc.fit(X)
    end_time = time.time()
    
    labels = brc.labels_
    n_clusters_found = len(np.unique(labels))
    duration_ms = (end_time - start_time) * 1000

    print(f"BIRCH Result: Time={duration_ms:.2f}ms, Clusters={n_clusters_found}")
    
    # Append to summary file if it exists, matching the shell script format
    summary_line = f"| {filepath} (BIRCH) | {duration_ms:.2f} | N/A | {n_clusters_found} |"
    try:
        with open("benchmark_summary.md", "a") as f:
            f.write(summary_line + "\n")
    except:
        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python run_birch.py <data_file> [threshold] [n_clusters]")
        sys.exit(1)

    filepath = sys.argv[1]
    threshold = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
    
    # Handle n_clusters argument (can be 'None' or an integer)
    n_clusters_arg = None
    if len(sys.argv) > 3:
        if sys.argv[3].lower() != 'none':
            n_clusters_arg = int(sys.argv[3])

    run_birch(filepath, threshold=threshold, n_clusters=n_clusters_arg)
