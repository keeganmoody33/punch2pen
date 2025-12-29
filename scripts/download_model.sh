#!/bin/bash
set -e

# Default to "base" model if not specified
MODEL="${1:-base}"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-${MODEL}.bin"

DATA_DIR="$HOME/.punch2pen/models"
mkdir -p "$DATA_DIR"

OUTPUT_FILE="${DATA_DIR}/ggml-${MODEL}.bin"

if [ -f "$OUTPUT_FILE" ]; then
    echo "✅ Model $MODEL already exists at $OUTPUT_FILE"
else
    echo "⬇️  Downloading $MODEL model to $OUTPUT_FILE..."
    curl -L "$MODEL_URL" -o "$OUTPUT_FILE"
    echo "✅ Download complete."
fi
