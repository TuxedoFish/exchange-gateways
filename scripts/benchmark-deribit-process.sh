#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_DIR"

CONFIG="md-process-deribit-test"
PROCESSED_DIR="/home/markl/Crypto/Deribit/test/processed"
RAW_FILE="/home/markl/Crypto/Deribit/test/raw/2026/05/23.txt"
BINARY="./build/gateways"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found. Building in Release mode..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3" 2>&1 | tail -3
    cmake --build build -j"$(nproc)" 2>&1 | tail -3
fi

LINES=$(wc -l < "$RAW_FILE")
SIZE=$(stat --printf="%s" "$RAW_FILE")
SIZE_MB=$(echo "scale=2; $SIZE / 1048576" | bc)

echo "=== Deribit Processor Benchmark ==="
echo "Input: $RAW_FILE"
echo "Lines: $LINES"
echo "Size:  ${SIZE_MB} MB"
echo ""

# Clean previous output
rm -rf "$PROCESSED_DIR"/2026

echo "--- Running processor ---"
START=$(date +%s%N)
$BINARY --app md-process --config-override "$CONFIG" 2>&1
END=$(date +%s%N)

ELAPSED_MS=$(( (END - START) / 1000000 ))
ELAPSED_S=$(echo "scale=3; $ELAPSED_MS / 1000" | bc)
LINES_PER_SEC=$(echo "scale=0; $LINES * 1000 / $ELAPSED_MS" | bc 2>/dev/null || echo "N/A")

echo ""
echo "=== Results ==="
echo "Elapsed:       ${ELAPSED_S}s"
echo "Lines/sec:     $LINES_PER_SEC"
echo ""

# Check output
OUTPUT_FILE="$PROCESSED_DIR/2026/05/23"
if [ -f "$OUTPUT_FILE" ]; then
    OUT_SIZE=$(stat --printf="%s" "$OUTPUT_FILE")
    OUT_SIZE_KB=$(echo "scale=2; $OUT_SIZE / 1024" | bc)
    echo "Output file:   $OUTPUT_FILE"
    echo "Output size:   ${OUT_SIZE_KB} KB"

    IDX_FILE="${OUTPUT_FILE}.idx"
    if [ -f "$IDX_FILE" ]; then
        IDX_SIZE=$(stat --printf="%s" "$IDX_FILE")
        MSG_COUNT=$(( IDX_SIZE / 8 ))
        echo "Messages:      $MSG_COUNT"
    fi
else
    echo "ERROR: No output file found at $OUTPUT_FILE"
fi
