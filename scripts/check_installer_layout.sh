#!/bin/bash
set -euo pipefail

# Gate the unsigned pkg layout so a GitHub Release cannot ship without
# the nested helper, Launch Services app, LaunchAgent, or postinstall.
# Usage: check_installer_layout.sh <staging-dir>
# staging-dir is installer/macos/build_pkg.sh's STAGING_DIR.

if [[ "${1:-}" == "--self-test" ]]; then
  set -euo pipefail
  tmp="$(mktemp -d "${TMPDIR:-/tmp}/p2p-layout.XXXXXX")"
  cleanup() { rm -rf "$tmp"; }
  trap cleanup EXIT
  repo="$(cd "$(dirname "$0")/.." && pwd)"
  staging="${tmp}/staging"
  plist_body='<?xml version="1.0"?>
<plist version="1.0"><dict>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/>
</dict></plist>'
  for app in \
      "${staging}/au/Library/Audio/Plug-Ins/Components/punch2pen.component/Contents/Helpers/punch2penEngine.app" \
      "${staging}/vst3/Library/Audio/Plug-Ins/VST3/punch2pen.vst3/Contents/Helpers/punch2penEngine.app" \
      "${staging}/engine/Applications/Punch2Pen/punch2penEngine.app"; do
    mkdir -p "${app}/Contents/MacOS"
    printf 'bin\n' > "${app}/Contents/MacOS/punch2penEngine"
    printf '%s\n' "$plist_body" > "${app}/Contents/Info.plist"
  done
  mkdir -p "${staging}/engine/Library/LaunchAgents" \
           "${staging}/engine/Applications/Punch2Pen"
  printf 'cli\n' > "${staging}/engine/Applications/Punch2Pen/punch2penEngine"
  printf 'dl\n' > "${staging}/engine/Applications/Punch2Pen/download_model.sh"
  cp "${repo}/installer/macos/launchagent.plist" \
     "${staging}/engine/Library/LaunchAgents/com.doctaaa.punch2pen.engine.plist"
  chmod +x "${repo}/installer/macos/scripts/postinstall"
  "$0" "$staging"
  echo "[PASS] check_installer_layout.sh --self-test"
  exit 0
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <staging-dir>" >&2
  echo "       $0 --self-test" >&2
  exit 2
fi

STAGING="$1"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
fail=0

need() {
  local path="$1"
  local label="$2"
  if [[ ! -e "$path" ]]; then
    echo "FAIL: missing ${label}: ${path}" >&2
    fail=1
    return 0
  fi
  echo "OK ${label}: ${path}"
}

need_dir() {
  local path="$1"
  local label="$2"
  if [[ ! -d "$path" ]]; then
    echo "FAIL: missing dir ${label}: ${path}" >&2
    fail=1
    return 0
  fi
  echo "OK ${label}: ${path}"
}

AU="${STAGING}/au/Library/Audio/Plug-Ins/Components/punch2pen.component"
VST3="${STAGING}/vst3/Library/Audio/Plug-Ins/VST3/punch2pen.vst3"
ENGINE="${STAGING}/engine/Applications/Punch2Pen"
AGENT="${STAGING}/engine/Library/LaunchAgents/com.doctaaa.punch2pen.engine.plist"
POSTINSTALL="${REPO}/installer/macos/scripts/postinstall"

need_dir "$AU" "AU bundle"
need_dir "$VST3" "VST3 bundle"
need_dir "${ENGINE}/punch2penEngine.app" "Applications engine app"
need "${ENGINE}/punch2penEngine" "Applications engine CLI"
need "${ENGINE}/download_model.sh" "staged download_model.sh"
need "$AGENT" "LaunchAgent plist"
need "$POSTINSTALL" "postinstall script"

for helper in \
    "${AU}/Contents/Helpers/punch2penEngine.app" \
    "${VST3}/Contents/Helpers/punch2penEngine.app" \
    "${ENGINE}/punch2penEngine.app"; do
  need_dir "$helper" "engine .app"
  need "${helper}/Contents/MacOS/punch2penEngine" "engine inner binary"
  need "${helper}/Contents/Info.plist" "engine Info.plist"
  if ! grep -q "<string>APPL</string>" "${helper}/Contents/Info.plist"; then
    echo "FAIL: ${helper} Info.plist missing CFBundlePackageType APPL" >&2
    fail=1
  fi
  if ! grep -q "<key>LSUIElement</key>" "${helper}/Contents/Info.plist"; then
    echo "FAIL: ${helper} Info.plist missing LSUIElement" >&2
    fail=1
  fi
done

if ! grep -q "com.doctaaa.punch2pen.engine" "$AGENT"; then
  echo "FAIL: LaunchAgent label mismatch" >&2
  fail=1
fi
if ! grep -q "RunAtLoad" "$AGENT"; then
  echo "FAIL: LaunchAgent missing RunAtLoad" >&2
  fail=1
fi
if grep -q "KeepAlive" "$AGENT"; then
  echo "FAIL: LaunchAgent must not KeepAlive (missing model would crash-loop)" >&2
  fail=1
fi
if ! grep -q "/Applications/Punch2Pen/punch2penEngine.app/Contents/MacOS/punch2penEngine" "$AGENT"; then
  echo "FAIL: LaunchAgent ProgramArguments must be the inner engine binary" >&2
  fail=1
fi

if [[ ! -x "$POSTINSTALL" ]]; then
  echo "FAIL: postinstall is not executable" >&2
  fail=1
fi
if ! grep -q "xattr" "$POSTINSTALL"; then
  echo "FAIL: postinstall does not clear quarantine" >&2
  fail=1
fi
if ! grep -q "mirror_user_plugins" "$POSTINSTALL"; then
  echo "FAIL: postinstall does not mirror AU/VST3 into ~/Library" >&2
  fail=1
fi

if [[ "$fail" -ne 0 ]]; then
  echo "check_installer_layout.sh failed" >&2
  exit 1
fi

echo "Installer layout OK (nested helper, LaunchAgent, postinstall)."
