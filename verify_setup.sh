#!/bin/bash

echo "=== Punch2Pen Verification Script ==="

# 1. Check Dependencies
echo "[1] Checking Dependencies..."
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake could not be found. Please install it (e.g., 'brew install cmake')."
else
    echo "✅ CMake found: $(cmake --version | head -n 1)"
fi

# 2. Check Directory Structure
echo -e "\n[2] Checking Project Structure..."
REQUIRED_DIRS=("plugin" "engine" "shared")
for dir in "${REQUIRED_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        echo "✅ Directory '$dir' exists."
    else
        echo "❌ Directory '$dir' MISSING."
    fi
done

# 3. Check Critical Files
echo -e "\n[3] Checking Critical Source Files..."
REQUIRED_FILES=(
    "plugin/Source/PluginProcessor.cpp"
    "plugin/Source/TranscriptView.cpp"
    "engine/src/main.cpp"
    "shared/Protocol.h"
)
for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "✅ File '$file' exists."
    else
        echo "❌ File '$file' MISSING."
    fi
done

# 4. Check Data Directory
echo -e "\n[4] Checking Data Directory..."
DATA_DIR="$HOME/.punch2pen"
if [ -d "$DATA_DIR" ]; then
    echo "✅ Data directory exists at $DATA_DIR"
else
    echo "⚠️  Data directory $DATA_DIR does not exist yet (Will be created by Engine)."
fi

# 5. Build Attempt
echo -e "\n[5] Attempting Dry-Run Build Configuration..."
if command -v cmake &> /dev/null; then
    if cmake -B build_verify -S . > /dev/null 2>&1; then
        echo "✅ CMake configuration SUCCESS."
        echo "ℹ️  Run 'cmake -B build && cmake --build build' to compile correctly."
    else
        echo "❌ CMake configuration FAILED. Please check CMakeLists.txt or missing dependencies."
    fi
else
    echo "⚠️  Skipping build check (CMake missing)."
fi

echo -e "\n=== Verification Complete ==="
