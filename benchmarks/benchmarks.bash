#!/bin/bash
set -e

# Default Configuration
NBSAMPLE=20000
RLIM="0.10"
RLIM_SET=0
MAXCL=1000
MAXIM=100000
SELECTED_PATTERNS=()
INPUT_TYPE="txt"   # txt, mp4
ALGORITHM="stream" # stream, birch
REUSE_MP4=0
ALL_PATTERNS=("2Dspiral" "2Dcircle-shuffle" "2Dspiral-shuffle" "2Drand" "3Drand" "2DcircleP10n")

# Video Generation Defaults
VID_SIZE=64
VID_ALPHA=0.1
VID_NOISE=0.0

print_help() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -h, --help            Show this help message"
    echo "  -n, --nsamples <N>    Set number of samples (default: 20000)"
    echo "  -r, --rlim <R>        Set radius limit (default: 0.10)"
    echo "  -p, --pattern <name>  Select pattern (can be used multiple times). If not set, runs all."
    echo "                        Available: ${ALL_PATTERNS[*]}"
    echo "  -t, --type <type>     Select input type: txt (default) or mp4"
    echo "  -a, --algo <algo>     Select algorithm: stream (default) or birch"
    echo "  -o, --options <str>   Pass additional options to image-cluster (e.g. \"-gprob\")."
    echo "                        Can be used multiple times to add more options."
    echo "  -mp4r                 Re-use mp4/txt files if they already exist"
    echo "  -maxcl <N>            Set max number of clusters (default: 1000)"
    echo "  -maxim <N>            Set max number of frames to process (default: 100000)"
    echo ""
    echo "Examples:"
    echo "  $0 -p 2Dspiral -t mp4 -a stream"
    echo "  $0 -n 1000 -a birch"
    echo "  $0 -p 2Dspiral-shuffle -o \"-gprob\" -o \"-fmatcha 1.0\""
    echo "  $0 -t mp4 -mp4r"
}

# Parse Arguments
EXTRA_OPTIONS=""
while [[ $# -gt 0 ]]; do
    key="$1"
    case $key in
        -h|--help)
            print_help
            exit 0
            ;;
        -n|--nsamples)
            NBSAMPLE="$2"
            shift; shift
            ;;
        -r|--rlim)
            RLIM="$2"
            RLIM_SET=1
            shift; shift
            ;;
        -p|--pattern)
            SELECTED_PATTERNS+=("$2")
            shift; shift
            ;;
        -t|--type)
            INPUT_TYPE="$2"
            shift; shift
            ;;
        -a|--algo)
            ALGORITHM="$2"
            shift; shift
            ;;
        -o|--options)
            EXTRA_OPTIONS="$EXTRA_OPTIONS $2"
            shift; shift
            ;;
        -mp4r)
            REUSE_MP4=1
            shift
            ;;
        -maxcl)
            MAXCL="$2"
            shift; shift
            ;;
        -maxim)
            MAXIM="$2"
            shift; shift
            ;;
        *)
            echo "Error: Unknown argument $1"
            print_help
            exit 1
            ;;
    esac
done

# Validate Input Type
if [[ "$INPUT_TYPE" != "txt" && "$INPUT_TYPE" != "mp4" ]]; then
    echo "Error: Invalid input type '$INPUT_TYPE'. Use 'txt' or 'mp4'."
    exit 1
fi

# Validate Algorithm
if [[ "$ALGORITHM" != "stream" && "$ALGORITHM" != "birch" ]]; then
    echo "Error: Invalid algorithm '$ALGORITHM'. Use 'stream' or 'birch'."
    exit 1
fi

# If no patterns selected, use all
if [ ${#SELECTED_PATTERNS[@]} -eq 0 ]; then
    SELECTED_PATTERNS=("${ALL_PATTERNS[@]}")
fi

# Build Project
echo "Building project..."
mkdir -p ../build
cd ../build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ../benchmarks

# Prepare directories
mkdir -p benchmark_out
mkdir -p clusteroutdir

# Executables
MKSEQEXEC="../build/image-cluster-mktxtseq"
RNUCLEXEC="../build/image-cluster"
CLPLOT="../build/image-cluster-plot"
TXT2MP4="../build/txt2mp4"

# Base Options
# RLIM is set at the top
OPTIONS="-maxcl $MAXCL -maxim $MAXIM -outdir clusteroutdir"

# Initialize Summary
SUMMARY_FILE="benchmark_summary.md"
if [ ! -f "$SUMMARY_FILE" ]; then
    echo "| Pattern | Type | Algo | Time (ms) | Dist Calls | Clusters | Memory (KB) |" > "$SUMMARY_FILE"
    echo "|---|---|---|---|---|---|---|" >> "$SUMMARY_FILE"
fi

# Helper: Generate Data
generate_data() {
    local pat="$1"
    local txt_file="${pat}.txt"

    if [[ "$REUSE_MP4" == "1" && -f "$txt_file" ]]; then
        echo "Re-using existing data file: $txt_file"
        return
    fi

    echo "Generating data for pattern: $pat"
    
    case $pat in
        "2Dspiral")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 2Dspiral
            ;;
        "2Dcircle-shuffle")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 2Dcircle -shuffle
            ;;
        "2Dspiral-shuffle")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 2Dspiral -shuffle
            ;;
        "2Drand")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 2Drand
            ;;
        "3Drand")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 3Drand
            ;;
        "2DcircleP10n")
            $MKSEQEXEC $NBSAMPLE "$txt_file" 2Dcircle10 -noise 0.04
            ;;
        *)
            echo "Error: Unknown pattern '$pat'"
            exit 1
            ;;
    esac
}

# Helper: Get/Convert Input File
prepare_input() {
    local pat="$1"
    local type="$2"
    local txt_file="${pat}.txt"
    
    if [[ "$type" == "txt" ]]; then
        echo "$txt_file"
    elif [[ "$type" == "mp4" ]]; then
        local mp4_file="${pat}.mp4"
        if [[ "$REUSE_MP4" == "1" && -f "$mp4_file" ]]; then
            echo "Re-using existing video file: $mp4_file" >&2
        else
            echo "Converting $txt_file to $mp4_file..." >&2
            # Usage: txt2mp4 <size> <alpha> <input_file> <output_file> <noise> <max_frames>
            $TXT2MP4 $VID_SIZE $VID_ALPHA "$txt_file" "$mp4_file" $VID_NOISE $NBSAMPLE
        fi
        echo "$mp4_file"
    fi
}

# Helper: Calculate Video RLIM (if needed)
get_rlim() {
    local type="$1"
    if [[ "$type" == "mp4" && "$RLIM_SET" == "0" ]]; then
        # rlim = 1000 * (size/64)^2
        # For size=64 -> 1000
        # For size=256 -> 1000 * (4^2) = 16000
        local ratio=$(echo "scale=4; $VID_SIZE / 64.0" | bc)
        local factor=$(echo "scale=4; $ratio * $ratio" | bc)
        echo $(echo "scale=4; 1000.0 * $factor" | bc)
    else
        echo "$RLIM"
    fi
}

# Main Loop
for pattern in "${SELECTED_PATTERNS[@]}"; do
    echo "========================================================"
    echo "Benchmark: Pattern=$pattern Type=$INPUT_TYPE Algo=$ALGORITHM"
    
    # 1. Generate TXT Data
    generate_data "$pattern"
    
    # 2. Prepare Input (TXT or MP4)
    INPUT_FILE=$(prepare_input "$pattern" "$INPUT_TYPE")
    
    LOG_FILE="benchmark_out/${pattern}_${INPUT_TYPE}_${ALGORITHM}.log"
    CUR_RLIM=$(get_rlim "$INPUT_TYPE")
    
    TIME="N/A"
    DISTS="N/A"
    CLUSTERS="N/A"
    MEM="N/A"
    
    if [[ "$ALGORITHM" == "stream" ]]; then
        echo "Running image-cluster on $INPUT_FILE (rlim=$CUR_RLIM)..."
        # Use /usr/bin/time -v to capture memory usage. Redirect stderr to stdout to capture it in LOG_FILE.
        CMD="/usr/bin/time -v $RNUCLEXEC $CUR_RLIM $OPTIONS $EXTRA_OPTIONS $INPUT_FILE"
        echo "CMD: $CMD"
        $CMD > "$LOG_FILE" 2>&1
        
        # Extract Metrics
        TIME=$(grep "Processing time:" "$LOG_FILE" | awk '{print $3}')
        DISTS=$(grep "Framedist calls:" "$LOG_FILE" | awk '{print $3}')
        CLUSTERS=$(grep "Total clusters:" "$LOG_FILE" | awk '{print $3}')
        MEM=$(grep "Maximum resident set size" "$LOG_FILE" | awk '{print $6}')
        
        # Plot if TXT and Stream (Optional)
        if [[ "$INPUT_TYPE" == "txt" ]]; then
            $CLPLOT "${INPUT_FILE}.clustered.txt" -png > /dev/null 2>&1 || true
        fi

    elif [[ "$ALGORITHM" == "birch" ]]; then
        echo "Running BIRCH on $INPUT_FILE..."
        
        if ! python3 -c "import sklearn" >/dev/null 2>&1; then
            echo "Error: scikit-learn not found. Cannot run BIRCH."
            continue
        fi

        if [[ "$INPUT_TYPE" == "txt" ]]; then
             # run_birch.py <filename> <rlim>
             # Capturing output is tricky because python scripts print to stdout
             # We assume they print "Time: ... ms" etc or we parse logic
             # The existing python scripts print results. Let's redirect to log.
             /usr/bin/time -v python3 run_birch.py "$INPUT_FILE" "$CUR_RLIM" > "$LOG_FILE" 2>&1
        else
             # run_birch_video.py <filename> <size> <rlim>
             /usr/bin/time -v python3 run_birch_video.py "$INPUT_FILE" "$VID_SIZE" "$CUR_RLIM" > "$LOG_FILE" 2>&1
        fi
        
        # Extract Metrics (Adapting to actual Python output)
        # Expected format: "BIRCH Result: Time=123.45ms, Clusters=10"
        # or "BIRCH (Pixels) Result: Time=123.45ms, Clusters=10"
        
        RESULT_LINE=$(grep "Result: Time=" "$LOG_FILE" | tail -n 1)
        if [ ! -z "$RESULT_LINE" ]; then
            TIME=$(echo "$RESULT_LINE" | sed -n 's/.*Time=\([0-9.]*\)ms.*/\1/p')
            CLUSTERS=$(echo "$RESULT_LINE" | sed -n 's/.*Clusters=\([0-9]*\).*/\1/p')
        fi
        
        DISTS="N/A"
        MEM=$(grep "Maximum resident set size" "$LOG_FILE" | awk '{print $6}')
    fi

    # Defaults
    TIME=${TIME:-"N/A"}
    DISTS=${DISTS:-"N/A"}
    CLUSTERS=${CLUSTERS:-"N/A"}
    MEM=${MEM:-"N/A"}

    echo "Result: Time=${TIME}ms, Dists=${DISTS}, Clusters=${CLUSTERS}, Mem=${MEM}KB"
    echo "| $pattern | $INPUT_TYPE | $ALGORITHM | $TIME | $DISTS | $CLUSTERS | $MEM |" >> "$SUMMARY_FILE"
done

echo "========================================================"
echo "Benchmarks Complete. Summary appended to $SUMMARY_FILE"
