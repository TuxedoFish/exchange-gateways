#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

# --- Paths ---
HDD_RAW_DIR="/mnt/data/Hyperliquid"
SSD_RAW_DIR="/home/markl/Crypto/Hyperliquid/raw"
PROCESSED_DIR="/home/markl/Crypto/Hyperliquid/processed"
BINARY="./build/gateways"
CONFIG_DIR="$PROJECT_DIR/config"
TEMPLATE_CONFIG="$CONFIG_DIR/settings.md-process-hyperliquid.txt"
TEMP_CONFIG_NAME="md-process-hyperliquid-backfill"
TEMP_CONFIG="$CONFIG_DIR/settings.${TEMP_CONFIG_NAME}.txt"
LOG_FILE="$PROJECT_DIR/log/md-process-hyperliquid-backfill.log"

# --- Build if needed ---
if [ ! -f "$BINARY" ]; then
    echo "Binary not found. Building in Release mode..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3" 2>&1 | tail -3
    cmake --build build -j"$(nproc)" 2>&1 | tail -3
fi

# --- Discover available raw files on HDD ---
echo "=== Hyperliquid Backfill Processor ==="
echo ""
echo "Scanning HDD for raw captures..."

mapfile -t HDD_FILES < <(find "$HDD_RAW_DIR" -name "*.txt" -type f | sort)

if [ ${#HDD_FILES[@]} -eq 0 ]; then
    echo "No raw files found on HDD at $HDD_RAW_DIR"
    exit 0
fi

echo "Found ${#HDD_FILES[@]} raw files on HDD"

# --- Build list of dates to process ---
# Extract YYYY/MM/DD from each HDD file path and check if already processed
DATES_TO_PROCESS=()

for hdd_file in "${HDD_FILES[@]}"; do
    # Extract relative path: 2026/02/18.txt -> date components
    rel_path="${hdd_file#$HDD_RAW_DIR/}"  # e.g. 2026/02/18.txt
    date_path="${rel_path%.txt}"            # e.g. 2026/02/18

    # Check if processed output exists (processed file has no extension)
    processed_file="$PROCESSED_DIR/$date_path"
    if [ -f "$processed_file" ]; then
        continue
    fi

    DATES_TO_PROCESS+=("$date_path")
done

if [ ${#DATES_TO_PROCESS[@]} -eq 0 ]; then
    echo "All files already processed. Nothing to do."
    exit 0
fi

echo "Already processed: $(( ${#HDD_FILES[@]} - ${#DATES_TO_PROCESS[@]} )) files"
echo "Remaining to process: ${#DATES_TO_PROCESS[@]} files"
echo ""

# --- Helper: compute next day in YYYYMMDD format ---
next_day() {
    date -d "${1:0:4}-${1:4:2}-${1:6:2} + 1 day" +%Y%m%d
}

# --- Cleanup handler ---
cleanup() {
    rm -f "$TEMP_CONFIG"
    # If we were in the middle of staging a file, clean it up
    if [ -n "${STAGED_FILE:-}" ] && [ -f "$STAGED_FILE" ]; then
        echo "Cleaning up staged file: $STAGED_FILE"
        rm -f "$STAGED_FILE"
    fi
}
trap cleanup EXIT

# --- Process each date ---
TOTAL=${#DATES_TO_PROCESS[@]}
OVERALL_START=$(date +%s)
STAGED_FILE=""

for i in "${!DATES_TO_PROCESS[@]}"; do
    date_path="${DATES_TO_PROCESS[$i]}"  # e.g. 2026/02/18
    n=$(( i + 1 ))

    # Parse date components
    YEAR="${date_path%%/*}"                          # 2026
    rest="${date_path#*/}"                            # 02/18
    MONTH="${rest%%/*}"                               # 02
    DAY="${rest#*/}"                                  # 18
    DATE_YYYYMMDD="${YEAR}${MONTH}${DAY}"
    END_YYYYMMDD=$(next_day "$DATE_YYYYMMDD")

    hdd_file="$HDD_RAW_DIR/${date_path}.txt"
    ssd_file="$SSD_RAW_DIR/${date_path}.txt"
    ssd_dir="$(dirname "$ssd_file")"

    echo "=== [$n/$TOTAL] Processing $DATE_YYYYMMDD ==="

    # --- Stage: copy from HDD to SSD ---
    FILE_SIZE=$(stat --printf="%s" "$hdd_file")
    FILE_SIZE_GB=$(echo "scale=2; $FILE_SIZE / 1073741824" | bc)
    echo "  Staging ${FILE_SIZE_GB} GB from HDD to SSD..."

    mkdir -p "$ssd_dir"
    COPY_START=$(date +%s%N)
    cp "$hdd_file" "$ssd_file"
    COPY_END=$(date +%s%N)
    STAGED_FILE="$ssd_file"

    COPY_MS=$(( (COPY_END - COPY_START) / 1000000 ))
    COPY_S=$(echo "scale=1; $COPY_MS / 1000" | bc)
    COPY_SPEED=$(echo "scale=0; $FILE_SIZE / 1048576 * 1000 / $COPY_MS" | bc 2>/dev/null || echo "N/A")
    echo "  Copied in ${COPY_S}s (${COPY_SPEED} MB/s)"

    # --- Generate temp config ---
    cat > "$TEMP_CONFIG" <<EOF
exchange_name=hyperliquid
md_raw_file_path=$SSD_RAW_DIR
md_processed_file_path=$PROCESSED_DIR
start_date=$DATE_YYYYMMDD
end_date=$END_YYYYMMDD
from_start=true
coins=BTC,ETH,SOL,HYPE,XRP,ZEC,PUMP,WLFI,TAO,DOGE,VVV,JTO,FARTCOIN,LIT,AAVE
outcomes=BTC:1d
log_file_path=$LOG_FILE
EOF

    # --- Count lines for throughput ---
    LINES=$(wc -l < "$ssd_file")

    # --- Run processor ---
    echo "  Processing ($LINES lines)..."
    PROC_START=$(date +%s%N)

    if ! $BINARY --app md-process --config-override "$TEMP_CONFIG_NAME" 2>&1 | tail -5; then
        echo "  ERROR: Processor failed for $DATE_YYYYMMDD"
        echo "  Staged file left at: $ssd_file"
        STAGED_FILE=""
        exit 1
    fi

    PROC_END=$(date +%s%N)
    PROC_MS=$(( (PROC_END - PROC_START) / 1000000 ))
    PROC_S=$(echo "scale=1; $PROC_MS / 1000" | bc)
    LINES_PER_SEC=$(echo "scale=0; $LINES * 1000 / $PROC_MS" | bc 2>/dev/null || echo "N/A")

    # --- Verify output ---
    processed_file="$PROCESSED_DIR/$date_path"
    if [ ! -f "$processed_file" ]; then
        echo "  ERROR: No output file at $processed_file"
        STAGED_FILE=""
        exit 1
    fi

    OUT_SIZE=$(stat --printf="%s" "$processed_file")
    OUT_SIZE_KB=$(echo "scale=1; $OUT_SIZE / 1024" | bc)
    MSG_COUNT="N/A"
    if [ -f "${processed_file}.idx" ]; then
        IDX_SIZE=$(stat --printf="%s" "${processed_file}.idx")
        MSG_COUNT=$(( IDX_SIZE / 8 ))
    fi

    # --- Clean up staged file ---
    rm -f "$ssd_file"
    STAGED_FILE=""

    # --- Progress ---
    ELAPSED_TOTAL=$(( $(date +%s) - OVERALL_START ))
    if [ $n -lt $TOTAL ] && [ $ELAPSED_TOTAL -gt 0 ]; then
        AVG_PER_FILE=$(( ELAPSED_TOTAL / n ))
        ETA_S=$(( AVG_PER_FILE * (TOTAL - n) ))
        ETA_MIN=$(( ETA_S / 60 ))
        ETA_STR="${ETA_MIN}m remaining"
    else
        ETA_STR=""
    fi

    echo "  Done: ${PROC_S}s, ${LINES_PER_SEC} lines/s, ${OUT_SIZE_KB} KB output, ${MSG_COUNT} msgs"
    if [ -n "$ETA_STR" ]; then
        echo "  Progress: $n/$TOTAL ($ETA_STR)"
    fi
    echo ""
done

TOTAL_ELAPSED=$(( $(date +%s) - OVERALL_START ))
TOTAL_MIN=$(( TOTAL_ELAPSED / 60 ))
TOTAL_SEC=$(( TOTAL_ELAPSED % 60 ))
echo "=== Complete ==="
echo "Processed $TOTAL files in ${TOTAL_MIN}m ${TOTAL_SEC}s"
