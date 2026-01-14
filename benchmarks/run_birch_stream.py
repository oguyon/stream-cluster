import sys
import time
import numpy as np
from sklearn.cluster import Birch

def run_birch_stream(filepath, threshold=0.1, branching_factor=50, n_clusters=None):
    print(f"Running BIRCH (Streaming Mode) on {filepath}...")
    
    # Initialize BIRCH
    brc = Birch(threshold=threshold, branching_factor=branching_factor, n_clusters=n_clusters)
    
    start_time = time.time()
    count = 0
    
    # Stream processing
    with open(filepath, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if parts:
                # Parse single point
                point = np.array([float(x) for x in parts]).reshape(1, -1)
                
                # Update model incrementally
                brc.partial_fit(point)
                count += 1
                
    end_time = time.time()
    duration_ms = (end_time - start_time) * 1000
    
    # Determine number of clusters
    # If n_clusters is None, the subclusters are the clusters
    if n_clusters is None:
        n_clusters_found = len(brc.subcluster_centers_)
    else:
        # If a global clustering step is required, we normally run it on the subclusters
        # brc.subcluster_centers_ contains the reduced data
        n_clusters_found = n_clusters # It will force this many
        # Note: partial_fit doesn't run the global clustering automatically after every point
        # It updates the CF tree. The global clustering is usually done via brc.predict() or explicit access.
        # But for 'n_clusters_found' reporting, if we set a fixed N, we expect N.
        # However, checking how many we actually HAVE in the tree is more informative for 'None'.
        pass

    # If n_clusters was set, we might want to check if we actually have enough points
    if n_clusters is not None:
         # To actually finalize the model for 'labels_', one would call prediction or similar
         # But here we just report timing and the "subclusters" (the CF leaves)
         # which represent the 'fine' clusters before the global step.
         pass
         
    # Fallback for reporting: use subcluster count if n_clusters is None, else trust the param
    if n_clusters is None:
         n_clusters_found = len(brc.subcluster_centers_)

    print(f"BIRCH (Stream) Result: Time={duration_ms:.2f}ms, Clusters={n_clusters_found}")
    print(f"Processed {count} points.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python run_birch_stream.py <data_file> [threshold] [n_clusters]")
        sys.exit(1)

    filepath = sys.argv[1]
    threshold = float(sys.argv[2]) if len(sys.argv) > 2 else 0.1
    
    n_clusters_arg = None
    if len(sys.argv) > 3:
        if sys.argv[3].lower() != 'none':
            n_clusters_arg = int(sys.argv[3])

    run_birch_stream(filepath, threshold=threshold, n_clusters=n_clusters_arg)
