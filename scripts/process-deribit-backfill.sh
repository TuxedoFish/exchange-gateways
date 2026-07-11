#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

# --- Options ---
HDD_ONLY=false
OVERWRITE=false
DRY_RUN=false
for arg in "$@"; do
    case "$arg" in
        --hdd-only) HDD_ONLY=true ;;
        --overwrite) OVERWRITE=true ;;
        --test) DRY_RUN=true ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# --- Paths ---
HDD_RAW_DIR="/mnt/data/Deribit/raw"
SSD_RAW_DIR="/home/markl/Crypto/Deribit/raw"
PROCESSED_DIR="/home/markl/Crypto/Deribit/processed"
HDD_PROCESSED_DIR="/mnt/data/Deribit/processed"
BINARY="./build/gateways"
CONFIG_DIR="$PROJECT_DIR/config"
BASE_CONFIG="$CONFIG_DIR/settings.md-process-deribit.txt"
TEMP_CONFIG_NAME="md-process-deribit-backfill"
TEMP_CONFIG="$CONFIG_DIR/settings.${TEMP_CONFIG_NAME}.txt"
LOG_FILE="$PROJECT_DIR/log/md-process-deribit-backfill.log"
LOCK_FILE="/tmp/deribit-backfill.lock"

# --- Logging ---
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*"
}

PERP_CURRENCIES=$(grep '^perp_currencies=' "$BASE_CONFIG" | cut -d= -f2-)

mkdir -p "$(dirname "$LOG_FILE")"
exec > >(tee -a "$LOG_FILE") 2>&1

# --- Lock file to prevent overlapping cron runs ---
if [ "$DRY_RUN" = false ]; then
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
fi

# --- Cutoff date: don't process yesterday until 3am ---
HOUR=$(date +%-H)
if [ "$HOUR" -lt 3 ]; then
    CUTOFF_DATE=$(date -d "2 days ago" +%Y/%m/%d)
else
    CUTOFF_DATE=$(date -d "yesterday" +%Y/%m/%d)
fi

# --- Build if needed ---
if [ "$DRY_RUN" = false ] && [ ! -f "$BINARY" ]; then
    log "Binary not found. Building in Release mode..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3" 2>&1 | tail -3
    cmake --build build -j"$(nproc)" 2>&1 | tail -3
fi

# --- Discover available raw files from both HDD and SSD ---
log "=== Deribit Backfill Processor ==="
log "Cutoff: $CUTOFF_DATE"
log ""

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

is_processed() {
    local date_path="$1"
    [ -f "$PROCESSED_DIR/$date_path" ] || [ -f "$HDD_PROCESSED_DIR/$date_path" ]
}

# --- Archive already-processed SSD files to HDD ---
if [ "$DRY_RUN" = true ]; then
    # Skip archiving in test mode — read-only
    :
else
for date_path in $(echo "${!DATE_SOURCE[@]}" | tr ' ' '\n' | sort); do
    if [ "${DATE_SOURCE[$date_path]}" != "ssd" ]; then
        continue
    fi
    if ! is_processed "$date_path"; then
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
fi

# --- Build sorted list of unprocessed dates within cutoff ---
DATES_TO_PROCESS=()
SOURCES=()

for date_path in $(echo "${!DATE_SOURCE[@]}" | tr ' ' '\n' | sort); do
    if [[ "$date_path" > "$CUTOFF_DATE" ]]; then
        continue
    fi
    if is_processed "$date_path" && [ "$OVERWRITE" = false ]; then
        continue
    fi
    DATES_TO_PROCESS+=("$date_path")
    SOURCES+=("${DATE_SOURCE[$date_path]}")
done

if [ ${#DATES_TO_PROCESS[@]} -eq 0 ]; then
    log "All files already processed. Nothing to do."
    rm -f "$LOCK_FILE"
    exit 0
fi

log "To process: ${#DATES_TO_PROCESS[@]} files"
log ""

# --- Helpers ---
next_day() {
    date -d "${1:0:4}-${1:4:2}-${1:6:2} + 1 day" +%Y%m%d
}

prev_day() {
    date -d "${1:0:4}-${1:4:2}-${1:6:2} - 1 day" +%Y%m%d
}

date_path_to_yyyymmdd() {
    # "2026/05/23" -> "20260523"
    echo "${1//\//}"
}

yyyymmdd_to_date_path() {
    # "20260523" -> "2026/05/23"
    echo "${1:0:4}/${1:4:2}/${1:6:2}"
}

has_logon() {
    local file="$1"
    # Subshell disables pipefail: grep -q exits early on match, causing
    # SIGPIPE in tr/tail. With pipefail that non-zero overrides grep's 0.
    ( set +o pipefail
      tail -10000 "$file" 2>/dev/null | tr '\001' '|' | grep -q '35=A' )
}

# Check if a date's raw file has a logon (checks SSD first, then HDD).
raw_file_has_logon() {
    local date_path="$1"
    if [ -f "$SSD_RAW_DIR/${date_path}.txt" ]; then
        has_logon "$SSD_RAW_DIR/${date_path}.txt"
    elif [ -f "$HDD_RAW_DIR/${date_path}.txt" ]; then
        has_logon "$HDD_RAW_DIR/${date_path}.txt"
    else
        return 1
    fi
}

# Stage a raw file onto SSD. Prints the source ("hdd" if copied, "ssd" if already local).
stage_raw_file() {
    local date_path="$1"
    local hdd_file="$HDD_RAW_DIR/${date_path}.txt"
    local ssd_file="$SSD_RAW_DIR/${date_path}.txt"

    if [ -f "$ssd_file" ]; then
        echo "ssd"
        return 0
    fi
    if [ -f "$hdd_file" ]; then
        mkdir -p "$(dirname "$ssd_file")"
        cp "$hdd_file" "$ssd_file"
        echo "hdd"
        return 0
    fi
    echo "missing"
    return 1
}

# Remove an SSD raw file. Archive to HDD first if no HDD copy exists.
unstage_raw_file() {
    local date_path="$1"
    local ssd_file="$SSD_RAW_DIR/${date_path}.txt"
    local hdd_file="$HDD_RAW_DIR/${date_path}.txt"

    [ -f "$ssd_file" ] || return 0

    if [ ! -f "$hdd_file" ]; then
        mkdir -p "$(dirname "$hdd_file")"
        log "    Archiving to HDD: $hdd_file"
        mv "$ssd_file" "$hdd_file"
    else
        log "    Removing SSD copy: $ssd_file"
        rm -f "$ssd_file"
    fi
}

# --- Cleanup handler ---
STAGED_FILES=()   # list of date_paths currently staged on SSD
cleanup() {
    rm -f "$TEMP_CONFIG"
    rm -f "$LOCK_FILE"
    for dp in "${STAGED_FILES[@]:-}"; do
        [ -z "$dp" ] && continue
        local ssd_file="$SSD_RAW_DIR/${dp}.txt"
        local hdd_file="$HDD_RAW_DIR/${dp}.txt"
        if [ -f "$ssd_file" ] && [ -f "$hdd_file" ]; then
            log "Cleaning up staged copy: $ssd_file"
            rm -f "$ssd_file"
        fi
    done
}
trap cleanup EXIT

# --- Group dates into runs ---
# A new run starts after a day whose raw file contains a logon (35=A),
# or after a calendar gap. The day with the logon stays as the last day
# of the previous run, so the next run can prime from it (from_start=false).
# Each run is stored as "start_idx:end_idx" (inclusive indices into DATES_TO_PROCESS).
RUNS=()
if [ ${#DATES_TO_PROCESS[@]} -gt 0 ]; then
    run_start=0
    for ((i=1; i<${#DATES_TO_PROCESS[@]}; i++)); do
        prev_yyyymmdd=$(date_path_to_yyyymmdd "${DATES_TO_PROCESS[$((i-1))]}")
        curr_yyyymmdd=$(date_path_to_yyyymmdd "${DATES_TO_PROCESS[$i]}")
        expected=$(next_day "$prev_yyyymmdd")
        if [ "$curr_yyyymmdd" != "$expected" ] || raw_file_has_logon "${DATES_TO_PROCESS[$((i-1))]}"; then
            RUNS+=("$run_start:$((i-1))")
            run_start=$i
        fi
    done
    RUNS+=("$run_start:$((${#DATES_TO_PROCESS[@]}-1))")
fi

TOTAL=${#DATES_TO_PROCESS[@]}
log "Grouped into ${#RUNS[@]} run(s)"
log ""

# --- Test mode: print proposed splits and exit ---
if [ "$DRY_RUN" = true ]; then
    for run_idx in "${!RUNS[@]}"; do
        run_spec="${RUNS[$run_idx]}"
        run_start="${run_spec%%:*}"
        run_end="${run_spec#*:}"
        run_size=$(( run_end - run_start + 1 ))
        run_num=$(( run_idx + 1 ))

        first_yyyymmdd=$(date_path_to_yyyymmdd "${DATES_TO_PROCESS[$run_start]}")
        last_yyyymmdd=$(date_path_to_yyyymmdd "${DATES_TO_PROCESS[$run_end]}")

        log "Run $run_num/${#RUNS[@]}: $first_yyyymmdd–$last_yyyymmdd ($run_size day(s))"
        for ((i=run_start; i<=run_end; i++)); do
            dp="${DATES_TO_PROCESS[$i]}"
            src="${SOURCES[$i]}"
            logon_mark=""
            if raw_file_has_logon "$dp"; then
                logon_mark=" [logon]"
            fi
            log "    $dp ($src)$logon_mark"
        done
    done
    exit 0
fi

# --- Process each run ---
OVERALL_START=$(date +%s)
FILES_DONE=0

for run_idx in "${!RUNS[@]}"; do
    run_spec="${RUNS[$run_idx]}"
    run_start="${run_spec%%:*}"
    run_end="${run_spec#*:}"
    run_size=$(( run_end - run_start + 1 ))
    run_num=$(( run_idx + 1 ))

    first_date_path="${DATES_TO_PROCESS[$run_start]}"
    last_date_path="${DATES_TO_PROCESS[$run_end]}"
    first_yyyymmdd=$(date_path_to_yyyymmdd "$first_date_path")
    last_yyyymmdd=$(date_path_to_yyyymmdd "$last_date_path")
    end_yyyymmdd=$(next_day "$last_yyyymmdd")

    log "=== Run $run_num/${#RUNS[@]}: $first_yyyymmdd–$last_yyyymmdd ($run_size day(s)) ==="

    # Per-run tracking of staged files and their origins
    RUN_STAGED_PATHS=()
    RUN_STAGED_ORIGINS=()
    PRIMING_DATE_PATH=""
    PRIMING_ORIGIN=""

    # --- 1. Search backwards for a file with a logon to prime from ---
    FROM_START="true"
    PRIMING_DATE_PATH=""
    PRIMING_ORIGIN=""
    config_start_yyyymmdd="$first_yyyymmdd"
    backfill_paths=()
    backfill_origins=()

    search_yyyymmdd="$first_yyyymmdd"
    while true; do
        search_yyyymmdd=$(prev_day "$search_yyyymmdd")
        search_date_path=$(yyyymmdd_to_date_path "$search_yyyymmdd")

        search_origin=$(stage_raw_file "$search_date_path" 2>/dev/null || true)
        if [ "$search_origin" = "missing" ] || [ -z "$search_origin" ]; then
            log "  No file for $search_yyyymmdd — from_start=true"
            # Unstage any intermediate files we staged during search
            for ((k=0; k<${#backfill_paths[@]}; k++)); do
                if [ "${backfill_origins[$k]}" = "hdd" ]; then
                    rm -f "$SSD_RAW_DIR/${backfill_paths[$k]}.txt"
                fi
            done
            backfill_paths=()
            backfill_origins=()
            break
        fi

        ssd_file="$SSD_RAW_DIR/${search_date_path}.txt"
        if has_logon "$ssd_file"; then
            FROM_START="false"
            PRIMING_DATE_PATH="$search_date_path"
            PRIMING_ORIGIN="$search_origin"
            STAGED_FILES+=("$search_date_path")
            config_start_yyyymmdd=$(next_day "$search_yyyymmdd")
            log "  Found logon in $search_yyyymmdd — priming from it (from $search_origin)"
            break
        fi

        log "  $search_yyyymmdd has no logon, searching further back..."
        backfill_paths+=("$search_date_path")
        backfill_origins+=("$search_origin")
        STAGED_FILES+=("$search_date_path")
    done

    # Prepend backfill files to run tracking (oldest first)
    for ((k=${#backfill_paths[@]}-1; k>=0; k--)); do
        RUN_STAGED_PATHS+=("${backfill_paths[$k]}")
        RUN_STAGED_ORIGINS+=("${backfill_origins[$k]}")
    done

    # --- 2. Stage all raw files in the run ---
    TOTAL_LINES=0
    # Count lines for backfill files (already staged)
    for ((k=0; k<${#backfill_paths[@]}; k++)); do
        bf_lines=$(wc -l < "$SSD_RAW_DIR/${backfill_paths[$k]}.txt")
        TOTAL_LINES=$((TOTAL_LINES + bf_lines))
    done
    for ((i=run_start; i<=run_end; i++)); do
        date_path="${DATES_TO_PROCESS[$i]}"
        source="${SOURCES[$i]}"

        # Remove existing processed output if --overwrite
        if [ "$OVERWRITE" = true ] && [ -f "$PROCESSED_DIR/$date_path" ]; then
            log "  Removing existing processed file: $PROCESSED_DIR/$date_path"
            rm -f "$PROCESSED_DIR/$date_path" "$PROCESSED_DIR/${date_path}.idx"
        fi

        if [ "$source" = "hdd" ]; then
            hdd_file="$HDD_RAW_DIR/${date_path}.txt"
            FILE_SIZE=$(stat --printf="%s" "$hdd_file")
            FILE_SIZE_GB=$(echo "scale=2; $FILE_SIZE / 1073741824" | bc)
            log "  Staging $date_path (${FILE_SIZE_GB} GB from HDD)..."
            cur_origin=$(stage_raw_file "$date_path")
        else
            ssd_file="$SSD_RAW_DIR/${date_path}.txt"
            FILE_SIZE=$(stat --printf="%s" "$ssd_file")
            FILE_SIZE_GB=$(echo "scale=2; $FILE_SIZE / 1073741824" | bc)
            log "  Using local $date_path (${FILE_SIZE_GB} GB)"
            cur_origin="ssd"
        fi

        RUN_STAGED_PATHS+=("$date_path")
        RUN_STAGED_ORIGINS+=("$cur_origin")
        STAGED_FILES+=("$date_path")

        ssd_file="$SSD_RAW_DIR/${date_path}.txt"
        lines=$(wc -l < "$ssd_file")
        TOTAL_LINES=$((TOTAL_LINES + lines))
    done

    # --- 3. Generate config ---
    cat > "$TEMP_CONFIG" <<EOF
exchange_name=deribit
md_raw_file_path=$SSD_RAW_DIR
md_processed_file_path=$PROCESSED_DIR
data_dictionary_file_path=config/Deribit_FIX44.xml
start_date=$config_start_yyyymmdd
end_date=$end_yyyymmdd
from_start=$FROM_START
log_file_path=$LOG_FILE
perp_currencies=$PERP_CURRENCIES
EOF

    # --- 4. Run processor ---
    log "  Processing $run_size day(s), $TOTAL_LINES total lines, from_start=$FROM_START..."
    PROC_START=$(date +%s%N)

    PROC_FAILED=false
    $BINARY --app md-process --config-override "$TEMP_CONFIG_NAME" 2>&1 \
        | sed 's/^/  /' || PROC_FAILED=true
    if [ "$PROC_FAILED" = true ]; then
        log "  ERROR: Processor failed for run $first_yyyymmdd–$last_yyyymmdd"
        exit 1
    fi

    PROC_END=$(date +%s%N)
    PROC_MS=$(( (PROC_END - PROC_START) / 1000000 ))
    PROC_S=$(echo "scale=1; $PROC_MS / 1000" | bc)
    LINES_PER_SEC=$(echo "scale=0; $TOTAL_LINES * 1000 / $PROC_MS" | bc 2>/dev/null || echo "N/A")

    # --- 5. Verify outputs ---
    ALL_OK=true
    for ((i=run_start; i<=run_end; i++)); do
        date_path="${DATES_TO_PROCESS[$i]}"
        processed_file="$PROCESSED_DIR/$date_path"
        if [ ! -f "$processed_file" ]; then
            log "  ERROR: No output file at $processed_file"
            ALL_OK=false
        fi
    done
    if [ "$ALL_OK" = false ]; then
        exit 1
    fi

    # --- 6. Cleanup ---
    # The run splitter guarantees correct grouping. After a successful run,
    # unstage everything except the last file (next run may prime from it).
    if [ -n "$PRIMING_DATE_PATH" ]; then
        unstage_raw_file "$PRIMING_DATE_PATH"
    fi
    for ((j=0; j<${#RUN_STAGED_PATHS[@]}-1; j++)); do
        unstage_raw_file "${RUN_STAGED_PATHS[$j]}"
    done

    FILES_DONE=$(( FILES_DONE + run_size ))

    # --- Progress ---
    ELAPSED_TOTAL=$(( $(date +%s) - OVERALL_START ))
    if [ $FILES_DONE -lt $TOTAL ] && [ $ELAPSED_TOTAL -gt 0 ]; then
        AVG_PER_FILE=$(( ELAPSED_TOTAL / FILES_DONE ))
        ETA_S=$(( AVG_PER_FILE * (TOTAL - FILES_DONE) ))
        ETA_MIN=$(( ETA_S / 60 ))
        ETA_STR="${ETA_MIN}m remaining"
    else
        ETA_STR=""
    fi

    log "  Done: ${PROC_S}s, ${LINES_PER_SEC} lines/s"
    if [ -n "$ETA_STR" ]; then
        log "  Progress: $FILES_DONE/$TOTAL files ($ETA_STR)"
    fi
    log ""
done

# Normal exit — logon-aware cleanup already handled files per-run.
# Clear STAGED_FILES so the trap handler doesn't remove intentionally-kept files.
STAGED_FILES=()

TOTAL_ELAPSED=$(( $(date +%s) - OVERALL_START ))
TOTAL_MIN=$(( TOTAL_ELAPSED / 60 ))
TOTAL_SEC=$(( TOTAL_ELAPSED % 60 ))
log "=== Complete ==="
log "Processed $TOTAL files in ${#RUNS[@]} run(s), ${TOTAL_MIN}m ${TOTAL_SEC}s"
