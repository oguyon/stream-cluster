import numpy as np
from astropy.io import fits
import os

def generate_test_data(filename='test.fits', width=10, height=10, num_frames=100, num_clusters=5):
    data = np.zeros((num_frames, height, width), dtype=np.float32)

    # Generate random base images for clusters
    bases = [np.random.rand(height, width) * 100 for _ in range(num_clusters)]

    labels = []

    for i in range(num_frames):
        cluster_idx = i % num_clusters
        # Add some noise to the base image
        noise = np.random.randn(height, width) * 2
        frame = bases[cluster_idx] + noise
        data[i] = frame
        labels.append(cluster_idx)

    hdu = fits.PrimaryHDU(data)
    hdu.writeto(filename, overwrite=True)
    print(f"Generated {filename} with {num_frames} frames, {num_clusters} clusters.")

    # Write labels for verification
    with open('expected_labels.txt', 'w') as f:
        for i, label in enumerate(labels):
            f.write(f"{i} {label}\n")

if __name__ == "__main__":
    generate_test_data()
