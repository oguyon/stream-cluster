#!/bin/bash
set -e

# Build
echo "Building project..."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

# Prepare benchmark dir
mkdir -p benchmark
DATA_FILE="benchmark/benchmark_data.txt"

# Generate Data: 2000 points, repeated 5 times = 10000 frames
echo "Generating synthetic data (10,000 frames)..."
./build/image-cluster-mktxtseq 2000 $DATA_FILE 3Dwalk -repeat 5 -noise 0.5

# Function to run benchmark
run_bench() {
    MODE_NAME=$1
    ARGS=$2
    LOG_FILE="benchmark/result_${MODE_NAME}.txt"
    echo "Running $MODE_NAME benchmark..."

    # Use 1.5 radius limit as per benchmarks docs
    ./build/image-cluster 1.5 $DATA_FILE -dprob 0.05 $ARGS > $LOG_FILE

    TIME=$(grep "Processing time:" $LOG_FILE | awk '{print $3}')
    DISTS=$(grep "Framedist calls:" $LOG_FILE | awk '{print $3}')
    CLUSTERS=$(grep "Total clusters:" $LOG_FILE | awk '{print $3}')

    echo "$MODE_NAME: Time=${TIME}ms, Dists=${DISTS}, Clusters=${CLUSTERS}"
    echo "| $MODE_NAME | $TIME | $DISTS | $CLUSTERS |" >> benchmark/summary.md
}

# Initialize summary
echo "| Mode | Time (ms) | Dist Calls | Clusters |" > benchmark/summary.md
echo "|---|---|---|---| " >> benchmark/summary.md

# Run scenarios
run_bench "Standard" ""
run_bench "GProb" "-gprob"
run_bench "Prediction" "-pred[10,1000,2] -gprob"

echo "Benchmark complete. Summary:"
cat benchmark/summary.md
