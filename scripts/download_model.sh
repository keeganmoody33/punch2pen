#!/bin/bash
set -euo pipefail

# Default to "base" model if not specified
MODEL="${1:-base}"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-${MODEL}.bin"

DATA_DIR="${HOME}/.punch2pen/models"
mkdir -p "$DATA_DIR"

OUTPUT_FILE="${DATA_DIR}/ggml-${MODEL}.bin"

if [ -f "$OUTPUT_FILE" ]; then
    echo "Model $MODEL already exists at $OUTPUT_FILE"
    exit 0
fi

echo "Downloading $MODEL model to $OUTPUT_FILE..."
PARTIAL="${OUTPUT_FILE}.part"
curl -fL --retry 3 --retry-delay 2 "$MODEL_URL" -o "$PARTIAL"
mv "$PARTIAL" "$OUTPUT_FILE"
echo "Download complete: $OUTPUT_FILE"
