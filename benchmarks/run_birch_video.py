import sys
import subprocess
import numpy as np
import time
from sklearn.cluster import Birch

def read_video_frames_pixels(filename, width, height):
    """
    Generator that reads video frames from ffmpeg pipe and returns flattened pixel arrays.
    Yields numpy array of shape (width * height * 3, )
    """
    cmd = [
        'ffmpeg',
        '-i', filename,
        '-f', 'image2pipe',
        '-pix_fmt', 'rgb24',
        '-vcodec', 'rawvideo',
        '-'
    ]
    
    pipe = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=10**8)
    frame_size = width * height * 3
    
    while True:
        raw_image = pipe.stdout.read(frame_size)
        if len(raw_image) != frame_size:
            break
            
        # Convert to numpy array of floats (0-255)
        # Flattened RGB
        pixels = np.frombuffer(raw_image, dtype='uint8').astype(float)
        yield pixels

    pipe.terminate()

def run_birch_on_video_pixels(filename, width, height, threshold=0.1):
    print(f"Reading frames from {filename}...")
    frames = []
    start_read = time.time()
    for pixels in read_video_frames_pixels(filename, width, height):
        frames.append(pixels)
    end_read = time.time()
    
    X = np.array(frames)
    print(f"Loaded {len(X)} frames of dim {X.shape[1]} in {(end_read - start_read):.2f}s.")
    
    print(f"Running BIRCH on {len(X)} frames (threshold={threshold})...")
    start_time = time.time()
    
    # Standard BIRCH
    # Note: branching_factor might need adjustment for high-dim data, 
    # but sklearn BIRCH uses CF tree which handles vectors.
    brc = Birch(threshold=threshold, branching_factor=50, n_clusters=None)
    brc.fit(X)
    
    end_time = time.time()
    duration_ms = (end_time - start_time) * 1000
    n_clusters = len(np.unique(brc.labels_))
    
    print(f"BIRCH (Pixels) Result: Time={duration_ms:.2f}ms, Clusters={n_clusters}")
    
    # Append to summary
    summary_line = f"| {filename} (VideoPixels->BIRCH) | {duration_ms:.2f} | N/A | {n_clusters} |"
    try:
        with open("benchmark_summary.md", "a") as f:
            f.write(summary_line + "\n")
    except:
        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python run_birch_video.py <mp4_file> [size] [threshold]")
        sys.exit(1)

    filename = sys.argv[1]
    size = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    threshold = float(sys.argv[3]) if len(sys.argv) > 3 else 1000.0
    
    run_birch_on_video_pixels(filename, size, size, threshold)
