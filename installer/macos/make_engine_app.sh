#!/bin/bash
set -euo pipefail

# Assemble a Launch-Services .app around the punch2penEngine binary.
# Logic's AU host sandboxes posix_spawn children; `open` of an APPL bundle
# starts an unsandboxed engine that can bind 127.0.0.1:7483 and read the
# real ~/.punch2pen model.

usage() {
  echo "Usage: $0 <engine-binary> <dest-punch2penEngine.app>" >&2
  exit 2
}

if [[ $# -ne 2 ]]; then
  usage
fi

BIN="$1"
DEST_APP="$2"

if [[ ! -f "$BIN" ]]; then
  echo "Engine binary not found: $BIN" >&2
  exit 1
fi

if [[ "${DEST_APP}" != *.app ]]; then
  echo "Destination must be an .app path: $DEST_APP" >&2
  exit 1
fi

rm -rf "$DEST_APP"
mkdir -p "${DEST_APP}/Contents/MacOS"
cp "$BIN" "${DEST_APP}/Contents/MacOS/punch2penEngine"
chmod 755 "${DEST_APP}/Contents/MacOS/punch2penEngine"

BIN_DIR="$(cd "$(dirname "$BIN")" && pwd)"
shopt -s nullglob
for lib in "${BIN_DIR}"/*.dylib; do
  cp "$lib" "${DEST_APP}/Contents/MacOS/"
done
shopt -u nullglob

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ -f "${SCRIPT_DIR}/bundle_engine_libs.sh" ]]; then
  bash "${SCRIPT_DIR}/bundle_engine_libs.sh" \
      "${DEST_APP}/Contents/MacOS/punch2penEngine" \
      "$BIN_DIR"
fi

cat > "${DEST_APP}/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>punch2penEngine</string>
  <key>CFBundleIdentifier</key>
  <string>com.doctaaa.punch2pen.engine</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>punch2penEngine</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1.0.0</string>
  <key>CFBundleVersion</key>
  <string>1.0.0</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
  <key>LSMultipleInstancesProhibited</key>
  <true/>
  <key>LSUIElement</key>
  <true/>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSSupportsAutomaticTermination</key>
  <false/>
  <key>NSSupportsSuddenTermination</key>
  <false/>
</dict>
</plist>
PLIST

echo "APPL????" > "${DEST_APP}/Contents/PkgInfo"
echo "Created ${DEST_APP}"
