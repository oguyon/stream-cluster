#!/bin/bash
set -e

# Load environment variables
VID_SIZE=256
VID_ALPHA=0.1
VID_NOISE=0.0
VID_MAX_FRAMES=20000
# Recalculate RLIM
RATIO=$(echo "scale=4; $VID_SIZE / 64.0" | bc)
FACTOR=$(echo "scale=4; $RATIO * $RATIO" | bc)
VID_RLIM=$(echo "scale=4; 1000.0 * $FACTOR" | bc)

echo "Using VID_RLIM=$VID_RLIM for VID_SIZE=$VID_SIZE"

TXT2MP4="../build/gric-ascii-spot-2-video"
RNUCLEXEC="../build/gric-cluster"
OPTIONS="-maxim $VID_MAX_FRAMES -outdir clusteroutdir"

run_video_case() {
    TEST_NAME="$1"
    INPUT_TXT="$2"
    MP4_FILE="${TEST_NAME}.mp4"
    
    echo "------------------------------------------------"
    echo "Processing $TEST_NAME ..."
    
    # 1. Generate Video if needed (or ensure it exists)
    if [ ! -f "$MP4_FILE" ]; then
        echo "Generating $MP4_FILE..."
        $TXT2MP4 $VID_SIZE $VID_ALPHA $INPUT_TXT $MP4_FILE $VID_NOISE $VID_MAX_FRAMES
    fi
    
    # 2. Run Stream Cluster
    echo "Running gric-cluster..."
    LOG_FILE="benchmark_out/${TEST_NAME}_video.log"
    $RNUCLEXEC $VID_RLIM $OPTIONS $MP4_FILE > "$LOG_FILE"
    
    TIME=$(grep "Processing time:" "$LOG_FILE" | awk '{print $3}')
    DISTS=$(grep "Framedist calls:" "$LOG_FILE" | awk '{print $3}')
    CLUSTERS=$(grep "Total clusters:" "$LOG_FILE" | awk '{print $3}')
    echo "Stream Result: Time=${TIME}ms, Dists=${DISTS}, Clusters=${CLUSTERS}"
    echo "| $MP4_FILE (Stream) | $TIME | $DISTS | $CLUSTERS |" >> benchmark_summary.md

    # 3. Run BIRCH (Pixels)
    echo "Running BIRCH (Pixels)..."
    python3 run_birch_video.py $MP4_FILE $VID_SIZE $VID_RLIM
}

# Run the requested test
run_video_case $1 $2
