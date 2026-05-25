#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

# --- Options ---
HDD_ONLY=false
OVERWRITE=false
for arg in "$@"; do
    case "$arg" in
        --hdd-only) HDD_ONLY=true ;;
        --overwrite) OVERWRITE=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# --- Paths ---
HDD_RAW_DIR="/mnt/data/Hyperliquid"
SSD_RAW_DIR="/home/markl/Crypto/Hyperliquid/raw"
PROCESSED_DIR="/home/markl/Crypto/Hyperliquid/processed"
BINARY="./build/gateways"
CONFIG_DIR="$PROJECT_DIR/config"
BASE_CONFIG="$CONFIG_DIR/settings.md-process-hyperliquid.txt"
TEMP_CONFIG_NAME="md-process-hyperliquid-backfill"
TEMP_CONFIG="$CONFIG_DIR/settings.${TEMP_CONFIG_NAME}.txt"
LOG_FILE="$PROJECT_DIR/log/md-process-hyperliquid-backfill.log"
LOCK_FILE="/tmp/hyperliquid-backfill.lock"

# --- Logging ---
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

mkdir -p "$(dirname "$LOG_FILE")"
exec > >(tee -a "$LOG_FILE") 2>&1

# Read coins and outcomes from the main config
COINS=$(grep '^coins=' "$BASE_CONFIG" | cut -d= -f2-)
OUTCOMES=$(grep '^outcomes=' "$BASE_CONFIG" | cut -d= -f2-)

# --- Lock file to prevent overlapping cron runs ---
if [ -f "$LOCK_FILE" ]; then
    LOCK_PID=$(cat "$LOCK_FILE" 2>/dev/null || true)
    if [ -n "$LOCK_PID" ] && kill -0 "$LOCK_PID" 2>/dev/null; then
        log "Already running (pid $LOCK_PID). Exiting."
        exit 0
    fi
    log "Stale lock file found (pid $LOCK_PID not running). Removing."
    rm -f "$LOCK_FILE"
fi
echo $$ > "$LOCK_FILE"

# --- Cutoff date: don't process yesterday until 3am ---
# Before 3am: safe up to 2 days ago. After 3am: safe up to yesterday.
HOUR=$(date +%-H)
if [ "$HOUR" -lt 3 ]; then
    CUTOFF_DATE=$(date -d "2 days ago" +%Y/%m/%d)
else
    CUTOFF_DATE=$(date -d "yesterday" +%Y/%m/%d)
fi

# --- Build if needed ---
if [ ! -f "$BINARY" ]; then
    log "Binary not found. Building in Release mode..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3" 2>&1 | tail -3
    cmake --build build -j"$(nproc)" 2>&1 | tail -3
fi

# --- Discover available raw files from both HDD and SSD ---
log "=== Hyperliquid Backfill Processor ==="
log "Cutoff: $CUTOFF_DATE"
log ""

# Collect all unique date paths and their source (HDD or SSD)
# Format: "source:date_path" — HDD checked first, SSD fills in gaps
declare -A DATE_SOURCE

# Scan HDD
HDD_COUNT=0
while IFS= read -r -d '' f; do
    rel="${f#$HDD_RAW_DIR/}"
    date_path="${rel%.txt}"
    DATE_SOURCE["$date_path"]="hdd"
    (( ++HDD_COUNT ))
done < <(find "$HDD_RAW_DIR" -name "*.txt" -type f -print0 2>/dev/null | sort -z)

# Scan SSD raw — only add dates not already on HDD
SSD_COUNT=0
if [ "$HDD_ONLY" = false ]; then
    while IFS= read -r -d '' f; do
        rel="${f#$SSD_RAW_DIR/}"
        date_path="${rel%.txt}"
        if [ -z "${DATE_SOURCE[$date_path]+x}" ]; then
            DATE_SOURCE["$date_path"]="ssd"
            (( ++SSD_COUNT ))
        fi
    done < <(find "$SSD_RAW_DIR" -name "*.txt" -type f -print0 2>/dev/null | sort -z)
fi

log "Found $HDD_COUNT files on HDD, $SSD_COUNT additional on SSD"

# --- Archive already-processed SSD files to HDD ---
for date_path in $(echo "${!DATE_SOURCE[@]}" | tr ' ' '\n' | sort); do
    if [ "${DATE_SOURCE[$date_path]}" != "ssd" ]; then
        continue
    fi
    # Only archive if already processed
    if [ ! -f "$PROCESSED_DIR/$date_path" ]; then
        continue
    fi
    ssd_file="$SSD_RAW_DIR/${date_path}.txt"
    hdd_file="$HDD_RAW_DIR/${date_path}.txt"
    if [ -f "$ssd_file" ] && [ ! -f "$hdd_file" ]; then
        hdd_dir="$(dirname "$hdd_file")"
        mkdir -p "$hdd_dir"
        log "  Archiving already-processed SSD file to HDD: $hdd_file"
        mv "$ssd_file" "$hdd_file"
    fi
done

# --- Build sorted list of unprocessed dates within cutoff ---
DATES_TO_PROCESS=()
SOURCES=()

for date_path in $(echo "${!DATE_SOURCE[@]}" | tr ' ' '\n' | sort); do
    # Skip dates newer than the cutoff
    if [[ "$date_path" > "$CUTOFF_DATE" ]]; then
        continue
    fi

    # Skip already processed (unless --overwrite)
    if [ -f "$PROCESSED_DIR/$date_path" ] && [ "$OVERWRITE" = false ]; then
        continue
    fi

    DATES_TO_PROCESS+=("$date_path")
    SOURCES+=("${DATE_SOURCE[$date_path]}")
done

if [ ${#DATES_TO_PROCESS[@]} -eq 0 ]; then
    log "All files already processed. Nothing to do."
    exit 0
fi

log "To process: ${#DATES_TO_PROCESS[@]} files"
log ""

# --- Helper: compute next day in YYYYMMDD format ---
next_day() {
    date -d "${1:0:4}-${1:4:2}-${1:6:2} + 1 day" +%Y%m%d
}

# --- Cleanup handler ---
# STAGED_FROM tracks whether the SSD file is a copy ("hdd") or the original ("ssd")
STAGED_FILE=""
STAGED_FROM=""

cleanup() {
    rm -f "$TEMP_CONFIG"
    rm -f "$LOCK_FILE"
    if [ -n "${STAGED_FILE:-}" ] && [ -f "$STAGED_FILE" ]; then
        if [ "$STAGED_FROM" = "hdd" ]; then
            # SSD file is a copy from HDD — safe to delete
            log "Cleaning up staged copy: $STAGED_FILE"
            rm -f "$STAGED_FILE"
        else
            # SSD file is the original — leave it alone
            log "Leaving original raw file in place: $STAGED_FILE"
        fi
    fi
}
trap cleanup EXIT

# --- Process each date ---
TOTAL=${#DATES_TO_PROCESS[@]}
OVERALL_START=$(date +%s)

for i in "${!DATES_TO_PROCESS[@]}"; do
    date_path="${DATES_TO_PROCESS[$i]}"  # e.g. 2026/02/18
    source="${SOURCES[$i]}"              # "hdd" or "ssd"
    n=$(( i + 1 ))

    # Parse date components
    YEAR="${date_path%%/*}"
    rest="${date_path#*/}"
    MONTH="${rest%%/*}"
    DAY="${rest#*/}"
    DATE_YYYYMMDD="${YEAR}${MONTH}${DAY}"
    END_YYYYMMDD=$(next_day "$DATE_YYYYMMDD")

    hdd_file="$HDD_RAW_DIR/${date_path}.txt"
    ssd_file="$SSD_RAW_DIR/${date_path}.txt"

    log "=== [$n/$TOTAL] Processing $DATE_YYYYMMDD ==="

    # Remove existing processed output if --overwrite
    if [ "$OVERWRITE" = true ] && [ -f "$PROCESSED_DIR/$date_path" ]; then
        log "  Removing existing processed file: $PROCESSED_DIR/$date_path"
        rm -f "$PROCESSED_DIR/$date_path" "$PROCESSED_DIR/${date_path}.idx"
    fi

    if [ "$source" = "hdd" ]; then
        # --- Source is HDD: copy to SSD for processing ---
        FILE_SIZE=$(stat --printf="%s" "$hdd_file")
        FILE_SIZE_GB=$(echo "scale=2; $FILE_SIZE / 1073741824" | bc)
        log "  Staging ${FILE_SIZE_GB} GB from HDD to SSD..."

        mkdir -p "$(dirname "$ssd_file")"
        COPY_START=$(date +%s%N)
        cp "$hdd_file" "$ssd_file"
        COPY_END=$(date +%s%N)
        STAGED_FILE="$ssd_file"
        STAGED_FROM="hdd"

        COPY_MS=$(( (COPY_END - COPY_START) / 1000000 ))
        COPY_S=$(echo "scale=1; $COPY_MS / 1000" | bc)
        COPY_SPEED=$(echo "scale=0; $FILE_SIZE / 1048576 * 1000 / $COPY_MS" | bc 2>/dev/null || echo "N/A")
        log "  Copied in ${COPY_S}s (${COPY_SPEED} MB/s)"
    else
        # --- Source is SSD: already local ---
        FILE_SIZE=$(stat --printf="%s" "$ssd_file")
        FILE_SIZE_GB=$(echo "scale=2; $FILE_SIZE / 1073741824" | bc)
        log "  Using local raw file (${FILE_SIZE_GB} GB)"
        STAGED_FILE="$ssd_file"
        STAGED_FROM="ssd"
    fi

    # --- Generate temp config ---
    cat > "$TEMP_CONFIG" <<EOF
exchange_name=hyperliquid
md_raw_file_path=$SSD_RAW_DIR
md_processed_file_path=$PROCESSED_DIR
start_date=$DATE_YYYYMMDD
end_date=$END_YYYYMMDD
from_start=true
coins=$COINS
outcomes=$OUTCOMES
log_file_path=$LOG_FILE
EOF

    # --- Count lines for throughput ---
    LINES=$(wc -l < "$ssd_file")

    # --- Run processor ---
    log "  Processing ($LINES lines)..."
    PROC_START=$(date +%s%N)

    $BINARY --app md-process --config-override "$TEMP_CONFIG_NAME" 2>&1 \
        | sed 's/^/  /' || PROC_FAILED=true
    if [ "${PROC_FAILED:-false}" = true ]; then
        log "  ERROR: Processor failed for $DATE_YYYYMMDD"
        log "  Raw file at: $ssd_file"
        STAGED_FILE=""
        STAGED_FROM=""
        exit 1
    fi

    PROC_END=$(date +%s%N)
    PROC_MS=$(( (PROC_END - PROC_START) / 1000000 ))
    PROC_S=$(echo "scale=1; $PROC_MS / 1000" | bc)
    LINES_PER_SEC=$(echo "scale=0; $LINES * 1000 / $PROC_MS" | bc 2>/dev/null || echo "N/A")

    # --- Verify output ---
    processed_file="$PROCESSED_DIR/$date_path"
    if [ ! -f "$processed_file" ]; then
        log "  ERROR: No output file at $processed_file"
        STAGED_FILE=""
        STAGED_FROM=""
        exit 1
    fi

    OUT_SIZE=$(stat --printf="%s" "$processed_file")
    OUT_SIZE_KB=$(echo "scale=1; $OUT_SIZE / 1024" | bc)
    MSG_COUNT="N/A"
    if [ -f "${processed_file}.idx" ]; then
        IDX_SIZE=$(stat --printf="%s" "${processed_file}.idx")
        MSG_COUNT=$(( IDX_SIZE / 8 ))
    fi

    # --- Post-processing: clean up raw file ---
    if [ "$source" = "hdd" ]; then
        # File was copied from HDD — just delete the SSD copy
        rm -f "$ssd_file"
    else
        # File was local on SSD — archive to HDD
        hdd_dir="$(dirname "$hdd_file")"
        mkdir -p "$hdd_dir"
        log "  Archiving raw file to HDD: $hdd_file"
        mv "$ssd_file" "$hdd_file"
    fi
    STAGED_FILE=""
    STAGED_FROM=""

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

    log "  Done: ${PROC_S}s, ${LINES_PER_SEC} lines/s, ${OUT_SIZE_KB} KB output, ${MSG_COUNT} msgs"
    if [ -n "$ETA_STR" ]; then
        log "  Progress: $n/$TOTAL ($ETA_STR)"
    fi
    log ""
done

TOTAL_ELAPSED=$(( $(date +%s) - OVERALL_START ))
TOTAL_MIN=$(( TOTAL_ELAPSED / 60 ))
TOTAL_SEC=$(( TOTAL_ELAPSED % 60 ))
log "=== Complete ==="
log "Processed $TOTAL files in ${TOTAL_MIN}m ${TOTAL_SEC}s"
