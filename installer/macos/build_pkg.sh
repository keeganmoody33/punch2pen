#!/bin/bash
set -e

# Project Root
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_release"
DIST_DIR="${PROJECT_ROOT}/dist"
STAGING_DIR="${PROJECT_ROOT}/staging"

echo "=== Punch2Pen MacOS Build & Package ==="
echo "Project Root: ${PROJECT_ROOT}"

# 1. Clean & Prepare
echo -e "\n[1] Cleaning previous builds..."
rm -rf "$BUILD_DIR"
rm -rf "$DIST_DIR"
rm -rf "$STAGING_DIR"
mkdir -p "$DIST_DIR"
mkdir -p "$STAGING_DIR"

# 2. Configure with CMake
echo -e "\n[2] Configuring CMake (Release)..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="/Applications/Punch2Pen"

# 3. Build
echo -e "\n[3] Building Project..."
cmake --build "$BUILD_DIR" --config Release --parallel

# 4. Prepare Staging Directories
echo -e "\n[4] Staging Artifacts..."

# Define Staging Paths
STAGE_AU="${STAGING_DIR}/au/Library/Audio/Plug-Ins/Components"
STAGE_VST3="${STAGING_DIR}/vst3/Library/Audio/Plug-Ins/VST3"
STAGE_ENGINE="${STAGING_DIR}/engine/Applications/Punch2Pen"

mkdir -p "$STAGE_AU"
mkdir -p "$STAGE_VST3"
mkdir -p "$STAGE_ENGINE"

# Copy Artifacts
# Note: Adjust paths if CMake output structure varies
cp -r "${BUILD_DIR}/plugin/punch2pen_plugin_artefacts/Release/AU/punch2pen.component" "$STAGE_AU/"
cp -r "${BUILD_DIR}/plugin/punch2pen_plugin_artefacts/Release/VST3/punch2pen.vst3" "$STAGE_VST3/"
cp "${BUILD_DIR}/bin/punch2penEngine" "$STAGE_ENGINE/"

# 5. Build Component Packages
echo -e "\n[5] Building Component Packages..."

pkgbuild --root "${STAGING_DIR}/au" \
    --identifier "com.doctaaa.punch2pen.au" \
    --version "1.0.0" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_au.pkg"

pkgbuild --root "${STAGING_DIR}/vst3" \
    --identifier "com.doctaaa.punch2pen.vst3" \
    --version "1.0.0" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_vst3.pkg"

pkgbuild --root "${STAGING_DIR}/engine" \
    --identifier "com.doctaaa.punch2pen.engine" \
    --version "1.0.0" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_engine.pkg"

# 6. Create Distribution Package
echo -e "\n[6] Creating Product Archive..."

# Synthesize distribution file
productbuild --synthesize \
    --package "${DIST_DIR}/punch2pen_au.pkg" \
    --package "${DIST_DIR}/punch2pen_vst3.pkg" \
    --package "${DIST_DIR}/punch2pen_engine.pkg" \
    "${DIST_DIR}/distribution.xml"

# Build final installer
productbuild --distribution "${DIST_DIR}/distribution.xml" \
    --package-path "${DIST_DIR}" \
    --resources "${PROJECT_ROOT}/installer/macos/resources" \
    "${DIST_DIR}/Punch2Pen_Installer.pkg"

echo -e "\n✅ Build Complete! Installer is at: ${DIST_DIR}/Punch2Pen_Installer.pkg"
