#!/bin/bash
set -euo pipefail

# Project Root
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build_release"
DIST_DIR="${PROJECT_ROOT}/dist"
STAGING_DIR="${PROJECT_ROOT}/staging"
VERSION="1.0.0"
SKIP_BUILD=0
RESOURCES_DIR="${PROJECT_ROOT}/installer/macos/resources"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Build unsigned macOS AU + VST3 + engine packages. Does not sign, notarize,
or embed vendor API keys.

Options:
  --build-dir PATH   CMake build directory (default: ${BUILD_DIR})
  --skip-build       Package existing artefacts; do not reconfigure/build
  -h, --help         Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

echo "=== Punch2Pen macOS Build & Package ==="
echo "Project Root: ${PROJECT_ROOT}"
echo "Build Dir: ${BUILD_DIR}"
echo "Version: ${VERSION} (unsigned)"

if [[ ! -d "$RESOURCES_DIR" ]]; then
  echo "Missing installer resources at ${RESOURCES_DIR}" >&2
  exit 1
fi

find_named_dir() {
  local name="$1"
  find "$BUILD_DIR" -name "$name" -type d -print -quit 2>/dev/null
}

find_engine() {
  local found
  if [[ -x "${BUILD_DIR}/bin/punch2penEngine" ]]; then
    printf '%s\n' "${BUILD_DIR}/bin/punch2penEngine"
    return 0
  fi
  found="$(find "$BUILD_DIR" -name punch2penEngine -type f -print -quit 2>/dev/null)"
  if [[ -n "$found" ]]; then
    printf '%s\n' "$found"
    return 0
  fi
  return 1
}

# 1. Clean & Prepare
echo -e "\n[1] Preparing output directories..."
rm -rf "$DIST_DIR" "$STAGING_DIR"
mkdir -p "$DIST_DIR" "$STAGING_DIR"

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  echo -e "\n[2] Configuring CMake (Release plugin + engine)..."
  rm -rf "$BUILD_DIR"
  cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DPUNCH2PEN_BUILD_PLUGIN=ON

  echo -e "\n[3] Building Project..."
  cmake --build "$BUILD_DIR" --config Release --parallel \
      --target punch2penEngine punch2pen_plugin_AU punch2pen_plugin_VST3
else
  echo -e "\n[2] Skipping configure/build (--skip-build)"
fi

# 4. Prepare Staging Directories
echo -e "\n[4] Staging Artifacts..."

STAGE_AU="${STAGING_DIR}/au/Library/Audio/Plug-Ins/Components"
STAGE_VST3="${STAGING_DIR}/vst3/Library/Audio/Plug-Ins/VST3"
STAGE_ENGINE="${STAGING_DIR}/engine/Applications/Punch2Pen"

mkdir -p "$STAGE_AU" "$STAGE_VST3" "$STAGE_ENGINE"

AU_BUNDLE="$(find_named_dir punch2pen.component)"
VST3_BUNDLE="$(find_named_dir punch2pen.vst3)"
ENGINE_BIN="$(find_engine)" || true

if [[ -z "${AU_BUNDLE}" || ! -d "${AU_BUNDLE}" ]]; then
  echo "AU bundle punch2pen.component not found under ${BUILD_DIR}" >&2
  exit 1
fi
if [[ -z "${VST3_BUNDLE}" || ! -d "${VST3_BUNDLE}" ]]; then
  echo "VST3 bundle punch2pen.vst3 not found under ${BUILD_DIR}" >&2
  exit 1
fi
if [[ -z "${ENGINE_BIN}" || ! -f "${ENGINE_BIN}" ]]; then
  echo "Engine binary punch2penEngine not found under ${BUILD_DIR}" >&2
  exit 1
fi

cp -R "${AU_BUNDLE}" "$STAGE_AU/"
cp -R "${VST3_BUNDLE}" "$STAGE_VST3/"
cp "${ENGINE_BIN}" "$STAGE_ENGINE/punch2penEngine"
chmod 755 "$STAGE_ENGINE/punch2penEngine"

echo "Staged AU: ${AU_BUNDLE}"
echo "Staged VST3: ${VST3_BUNDLE}"
echo "Staged engine: ${ENGINE_BIN}"

# 5. Build Component Packages
echo -e "\n[5] Building Component Packages..."

pkgbuild --root "${STAGING_DIR}/au" \
    --identifier "com.doctaaa.punch2pen.au" \
    --version "${VERSION}" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_au.pkg"

pkgbuild --root "${STAGING_DIR}/vst3" \
    --identifier "com.doctaaa.punch2pen.vst3" \
    --version "${VERSION}" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_vst3.pkg"

pkgbuild --root "${STAGING_DIR}/engine" \
    --identifier "com.doctaaa.punch2pen.engine" \
    --version "${VERSION}" \
    --install-location "/" \
    "${DIST_DIR}/punch2pen_engine.pkg"

# 6. Create Distribution Package
echo -e "\n[6] Creating Product Archive..."

productbuild --synthesize \
    --package "${DIST_DIR}/punch2pen_au.pkg" \
    --package "${DIST_DIR}/punch2pen_vst3.pkg" \
    --package "${DIST_DIR}/punch2pen_engine.pkg" \
    "${DIST_DIR}/distribution.xml"

python3 - "${DIST_DIR}/distribution.xml" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text()
marker = "<installer-gui-script"
start = text.find(marker)
if start < 0:
    raise SystemExit("distribution.xml missing installer-gui-script")
end = text.find(">", start) + 1
insert = """
    <title>Punch2Pen</title>
    <welcome file="welcome.html"/>
    <readme file="readme.txt"/>
"""
path.write_text(text[:end] + insert + text[end:])
PY

productbuild --distribution "${DIST_DIR}/distribution.xml" \
    --package-path "${DIST_DIR}" \
    --resources "${RESOURCES_DIR}" \
    "${DIST_DIR}/Punch2Pen_Installer.pkg"

echo -e "\nBuild complete (unsigned). Installer: ${DIST_DIR}/Punch2Pen_Installer.pkg"
echo "AU identity remains aufx / P2pn / Dcta. Notarization is not included."
