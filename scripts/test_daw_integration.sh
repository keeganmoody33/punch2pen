#!/usr/bin/env bash
set -uo pipefail

# punch2pen DAW integration readiness checker.
# This script validates the local machine, build output, engine, plugin bundles,
# model files, and DAW-specific next steps before manual Logic/Reaper/Ableton testing.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
CONFIG="Release"
MODEL="base"
RUN_BUILD=1
RUN_TESTS=1
RUN_ENGINE=1
RUN_MODEL_DOWNLOAD=0
INSTALL_PLUGIN=0
SETUP_ENGINE_LINK=0
OPEN_DAW=""
REPORT_FILE=""
ENGINE_MODE="local"
OPENAI_API_KEY_ARG=""
ENGINE_PID=""
ENGINE_LOG=""

PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0
CHECK_RESULTS=()

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Validates the punch2pen DAW/plugin setup end-to-end and prints exact next steps.
Run from anywhere inside the repository.

Options:
  --build-dir PATH       Build directory to use (default: $BUILD_DIR)
  --config NAME          CMake config/build type (default: $CONFIG)
  --model NAME           Whisper model name for local mode (default: $MODEL)
  --download-model       Download the selected Whisper model if it is missing
  --skip-build           Do not configure/build the project
  --skip-tests           Do not run unit/integration helper tests
  --skip-engine          Do not launch the engine readiness check
  --install-plugin       Copy built VST3/AU bundles into ~/Library/Audio/Plug-Ins
  --setup-engine-link    Copy the built engine into plugin auto-launch paths
  --open-daw NAME        Open a detected DAW after checks (logic|reaper|ableton)
  --cloud                Start engine in OpenAI cloud mode for the engine check
  --api-key KEY          API key to pass with --cloud (or use OPENAI_API_KEY)
  --report PATH          Also write the final report to PATH
  -h, --help             Show this help

Examples:
  ./scripts/test_daw_integration.sh --download-model
  ./scripts/test_daw_integration.sh --download-model --install-plugin --setup-engine-link
  ./scripts/test_daw_integration.sh --cloud --api-key "\$OPENAI_API_KEY" --open-daw logic
USAGE
}

log() { printf '%s\n' "$*"; }
section() { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
record() {
  local status="$1" name="$2" detail="${3:-}"
  local icon="[PASS]"
  case "$status" in
    pass) PASS_COUNT=$((PASS_COUNT + 1)); icon="[PASS]" ;;
    warn) WARN_COUNT=$((WARN_COUNT + 1)); icon="[WARN]" ;;
    fail) FAIL_COUNT=$((FAIL_COUNT + 1)); icon="[FAIL]" ;;
  esac
  CHECK_RESULTS+=("$icon $name${detail:+ - $detail}")
  printf '%s %s%s\n' "$icon" "$name" "${detail:+ - $detail}"
}

run_cmd() {
  local name="$1"; shift
  log "> $*"
  if "$@"; then
    record pass "$name"
    return 0
  fi
  local exit_code=$?
  record fail "$name" "command exited with $exit_code"
  return "$exit_code"
}

command_exists() { command -v "$1" >/dev/null 2>&1; }

version_ge() {
  # Returns true if $1 >= $2. Uses sort -V when available; falls back to Python.
  local have="$1" need="$2"
  if sort -V </dev/null >/dev/null 2>&1; then
    [[ "$(printf '%s\n%s\n' "$need" "$have" | sort -V | head -n1)" == "$need" ]]
  elif command_exists python3; then
    python3 - "$have" "$need" <<'PY'
import re
import sys

def parts(value):
    nums = [int(p) for p in re.findall(r"\d+", value)]
    return (nums + [0, 0, 0])[:3]

sys.exit(0 if parts(sys.argv[1]) >= parts(sys.argv[2]) else 1)
PY
  else
    [[ "$have" == "$need" || "$have" > "$need" ]]
  fi
}

cleanup() {
  if [[ -n "${ENGINE_PID:-}" ]] && kill -0 "$ENGINE_PID" >/dev/null 2>&1; then
    kill "$ENGINE_PID" >/dev/null 2>&1 || true
    wait "$ENGINE_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --model) MODEL="$2"; shift 2 ;;
    --download-model) RUN_MODEL_DOWNLOAD=1; shift ;;
    --skip-build) RUN_BUILD=0; shift ;;
    --skip-tests) RUN_TESTS=0; shift ;;
    --skip-engine) RUN_ENGINE=0; shift ;;
    --install-plugin) INSTALL_PLUGIN=1; shift ;;
    --setup-engine-link) SETUP_ENGINE_LINK=1; shift ;;
    --open-daw) OPEN_DAW="$(printf '%s' "$2" | tr '[:upper:]' '[:lower:]')"; shift 2 ;;
    --cloud) ENGINE_MODE="cloud"; shift ;;
    --api-key) OPENAI_API_KEY_ARG="$2"; shift 2 ;;
    --report) REPORT_FILE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) log "Unknown option: $1"; usage; exit 2 ;;
  esac
done

section "Repository context"
cd "$ROOT_DIR" || exit 1
record pass "Repository root" "$ROOT_DIR"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  record pass "Git branch" "$(git branch --show-current 2>/dev/null || echo unknown) @ $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  if [[ -n "$(git status --porcelain)" ]]; then
    record warn "Git working tree" "local changes are present"
  else
    record pass "Git working tree" "clean"
  fi
else
  record warn "Git metadata" "not running from a git checkout"
fi

section "Host environment"
if [[ "$(uname -s)" == "Darwin" ]]; then
  record pass "macOS host" "$(sw_vers -productVersion 2>/dev/null || uname -r)"
  if xcode-select -p >/dev/null 2>&1; then
    record pass "Xcode Command Line Tools" "$(xcode-select -p)"
  else
    record fail "Xcode Command Line Tools" "run: xcode-select --install"
  fi
  if [[ -d /System/Library/Frameworks/Accelerate.framework ]]; then
    record pass "Accelerate framework" "available"
  else
    record fail "Accelerate framework" "missing; local whisper build requires it on macOS"
  fi
else
  record warn "macOS host" "$(uname -s); plugin/DAW validation is macOS-focused"
fi

if command_exists cmake; then
  CMAKE_VERSION="$(cmake --version | awk 'NR==1 {print $3}')"
  if version_ge "$CMAKE_VERSION" "3.20"; then
    record pass "CMake >= 3.20" "$CMAKE_VERSION"
  else
    record fail "CMake >= 3.20" "found $CMAKE_VERSION"
  fi
else
  record fail "CMake" "install CMake 3.20+"
fi

if command_exists c++; then
  record pass "C++ compiler" "$(c++ --version | head -n1)"
else
  record fail "C++ compiler" "install Xcode Command Line Tools or another C++20 compiler"
fi

if command_exists curl; then
  record pass "curl" "available"
else
  record fail "curl" "required for model downloads"
fi

section "DAW detection"
daw_path() {
  case "$1" in
    logic) printf '%s\n' "/Applications/Logic Pro.app" ;;
    reaper) printf '%s\n' "/Applications/REAPER.app" ;;
    ableton) printf '%s\n' "/Applications/Ableton Live 12 Suite.app" ;;
    *) return 1 ;;
  esac
}

for name in logic reaper ableton; do
  path="$(daw_path "$name")"
  if [[ -d "$path" ]]; then
    record pass "Detected DAW: $name" "$path"
  else
    record warn "Detected DAW: $name" "not found at $path"
  fi
done

section "Data/model setup"
P2P_HOME="$HOME/.punch2pen"
MODEL_DIR="$P2P_HOME/models"
MODEL_FILE="$MODEL_DIR/ggml-${MODEL}.bin"
mkdir -p "$MODEL_DIR" || record fail "Create data directory" "$MODEL_DIR"
[[ -d "$MODEL_DIR" ]] && record pass "Data directory" "$MODEL_DIR"

if [[ "$RUN_MODEL_DOWNLOAD" -eq 1 ]]; then
  run_cmd "Download Whisper model: $MODEL" "$ROOT_DIR/scripts/download_model.sh" "$MODEL" || true
fi

if [[ -f "$MODEL_FILE" ]]; then
  MODEL_SIZE="$(wc -c < "$MODEL_FILE" | tr -d ' ')"
  if [[ "$MODEL_SIZE" -gt 1000000 ]]; then
    record pass "Whisper model" "$MODEL_FILE (${MODEL_SIZE} bytes)"
  else
    record fail "Whisper model" "$MODEL_FILE is unexpectedly small (${MODEL_SIZE} bytes)"
  fi
else
  if [[ "$ENGINE_MODE" == "cloud" ]]; then
    record warn "Whisper model" "missing, but cloud mode was selected"
  else
    record fail "Whisper model" "missing; rerun with --download-model"
  fi
fi

if [[ -f "$P2P_HOME/corrections.csv" ]]; then
  record pass "Corrections CSV" "$P2P_HOME/corrections.csv"
else
  record warn "Corrections CSV" "not present yet; it will be created after corrections are submitted"
fi
if compgen -G "$P2P_HOME/profile_*.json" >/dev/null; then
  record pass "Profile JSON" "$(compgen -G "$P2P_HOME/profile_*.json" | tr '\n' ' ')"
else
  record warn "Profile JSON" "not present yet; it will be created on engine shutdown after profile changes"
fi

section "Configure and build"
if [[ "$RUN_BUILD" -eq 1 ]]; then
  if command_exists cmake; then
    run_cmd "Configure plugin build" cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DPUNCH2PEN_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE="$CONFIG" || true
    run_cmd "Build all targets" cmake --build "$BUILD_DIR" --config "$CONFIG" -j4 || true
  else
    record fail "Configure/build" "cmake is unavailable"
  fi
else
  record warn "Configure/build" "skipped by --skip-build"
fi

section "Artifact validation"
ENGINE_BIN="$BUILD_DIR/bin/punch2penEngine"
[[ -x "$ENGINE_BIN" ]] && record pass "Engine binary" "$ENGINE_BIN" || record fail "Engine binary" "missing; build the punch2penEngine target"

find_first() {
  local pattern="$1"
  find "$BUILD_DIR" -path "$pattern" -print -quit 2>/dev/null
}
VST3_BUNDLE="$(find_first '*/punch2pen.vst3')"
AU_BUNDLE="$(find_first '*/punch2pen.component')"
[[ -n "$VST3_BUNDLE" && -d "$VST3_BUNDLE" ]] && record pass "Built VST3 bundle" "$VST3_BUNDLE" || record fail "Built VST3 bundle" "not found under $BUILD_DIR"
[[ -n "$AU_BUNDLE" && -d "$AU_BUNDLE" ]] && record pass "Built AU bundle" "$AU_BUNDLE" || record warn "Built AU bundle" "not found under $BUILD_DIR"

if command_exists codesign && [[ -n "$VST3_BUNDLE" ]]; then
  if codesign --verify --deep --strict "$VST3_BUNDLE" >/dev/null 2>&1; then
    record pass "VST3 code signature" "valid"
  else
    record warn "VST3 code signature" "ad-hoc/unsigned bundle may still load locally, but notarized distribution needs signing"
  fi
fi

section "Unit and helper tests"
if [[ "$RUN_TESTS" -eq 1 ]]; then
  ENGINE_TESTS=(databaseManagerTest profileManagerTest protocolSerializationTest openAIJsonTest transcriptionCoordinatorTest)
  for test_name in "${ENGINE_TESTS[@]}"; do
    test_bin="$BUILD_DIR/bin/$test_name"
    if [[ -x "$test_bin" ]]; then
      run_cmd "$test_name" "$test_bin" || true
    else
      record fail "$test_name" "missing binary at $test_bin"
    fi
  done

  PLUGIN_TEST_PATTERNS=(
    '*/ringBufferTest_artefacts/*/ringBufferTest'
    '*/ipcClientTest_artefacts/*/ipcClientTest'
    '*/pluginProcessorStateTest_artefacts/*/pluginProcessorStateTest'
  )
  for pattern in "${PLUGIN_TEST_PATTERNS[@]}"; do
    test_bin="$(find_first "$pattern")"
    test_name="$(basename "${pattern##*/}")"
    if [[ -x "$test_bin" ]]; then
      run_cmd "$test_name" "$test_bin" || true
    else
      record fail "$test_name" "missing binary under $BUILD_DIR"
    fi
  done
else
  record warn "Unit/helper tests" "skipped by --skip-tests"
fi

section "Engine smoke test"
if [[ "$RUN_ENGINE" -eq 1 ]]; then
  if [[ ! -x "$ENGINE_BIN" ]]; then
    record fail "Engine launch" "missing binary"
  elif [[ "$ENGINE_MODE" == "local" && ! -f "$MODEL_FILE" ]]; then
    record fail "Engine launch" "local mode requires $MODEL_FILE; rerun with --download-model"
  else
    if command_exists nc && nc -z 127.0.0.1 7483 >/dev/null 2>&1; then
      record warn "Engine launch" "127.0.0.1:7483 is already listening; using the existing engine for IPC checks"
    else
      ENGINE_LOG="$(mktemp -t punch2pen-engine.XXXXXX)"
      ENGINE_ARGS=()
      if [[ "$ENGINE_MODE" == "cloud" ]]; then
        ENGINE_ARGS+=(--cloud)
        if [[ -n "$OPENAI_API_KEY_ARG" ]]; then
          ENGINE_ARGS+=("--api-key=$OPENAI_API_KEY_ARG")
        fi
      fi
      if [[ "${#ENGINE_ARGS[@]}" -gt 0 ]]; then
        "$ENGINE_BIN" "${ENGINE_ARGS[@]}" >"$ENGINE_LOG" 2>&1 &
      else
        "$ENGINE_BIN" >"$ENGINE_LOG" 2>&1 &
      fi
      ENGINE_PID=$!
      sleep 2
      if kill -0 "$ENGINE_PID" >/dev/null 2>&1; then
        record pass "Engine process" "pid $ENGINE_PID, log $ENGINE_LOG"
      else
        record fail "Engine process" "exited early; log follows: $(tail -n 20 "$ENGINE_LOG" | tr '\n' ' ')"
      fi
    fi

    if command_exists nc; then
      if nc -z 127.0.0.1 7483 >/dev/null 2>&1; then
        record pass "Engine TCP port" "127.0.0.1:7483 is listening"
      else
        record fail "Engine TCP port" "127.0.0.1:7483 is not listening"
      fi
    else
      record warn "Engine TCP port" "nc unavailable; unable to probe"
    fi

    if [[ "$RUN_TESTS" -eq 1 && -f "$ROOT_DIR/scripts/verify_correction.py" ]] && command_exists python3; then
      run_cmd "Correction IPC helper" python3 "$ROOT_DIR/scripts/verify_correction.py" || true
    fi
  fi
else
  record warn "Engine smoke test" "skipped by --skip-engine"
fi

section "Optional install/setup"
USER_VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
USER_AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
if [[ "$INSTALL_PLUGIN" -eq 1 ]]; then
  mkdir -p "$USER_VST3_DIR" "$USER_AU_DIR"
  if [[ -n "$VST3_BUNDLE" && -d "$VST3_BUNDLE" ]]; then
    rm -rf "$USER_VST3_DIR/$(basename "$VST3_BUNDLE")"
    cp -R "$VST3_BUNDLE" "$USER_VST3_DIR/"
    record pass "Install VST3" "$USER_VST3_DIR/$(basename "$VST3_BUNDLE")"
  else
    record fail "Install VST3" "built VST3 bundle unavailable"
  fi
  if [[ -n "$AU_BUNDLE" && -d "$AU_BUNDLE" ]]; then
    rm -rf "$USER_AU_DIR/$(basename "$AU_BUNDLE")"
    cp -R "$AU_BUNDLE" "$USER_AU_DIR/"
    record pass "Install AU" "$USER_AU_DIR/$(basename "$AU_BUNDLE")"
  else
    record warn "Install AU" "built AU bundle unavailable"
  fi
else
  record warn "Plugin install" "skipped; use --install-plugin to copy into ~/Library/Audio/Plug-Ins"
fi

SYSTEM_ENGINE_DIR="/Applications/Punch2Pen"
USER_ENGINE_DIR="$HOME/punch2pen/bin"
if [[ "$SETUP_ENGINE_LINK" -eq 1 ]]; then
  if [[ -x "$ENGINE_BIN" ]]; then
    ENGINE_INSTALL_COUNT=0
    if mkdir -p "$SYSTEM_ENGINE_DIR" 2>/dev/null && cp "$ENGINE_BIN" "$SYSTEM_ENGINE_DIR/punch2penEngine" 2>/dev/null; then
      record pass "Install engine helper" "$SYSTEM_ENGINE_DIR/punch2penEngine"
      ENGINE_INSTALL_COUNT=$((ENGINE_INSTALL_COUNT + 1))
    else
      record warn "Install engine helper" "could not write $SYSTEM_ENGINE_DIR; install manually if plugin auto-launch prefers the system path"
    fi

    if mkdir -p "$USER_ENGINE_DIR" && cp "$ENGINE_BIN" "$USER_ENGINE_DIR/punch2penEngine"; then
      record pass "Install engine helper" "$USER_ENGINE_DIR/punch2penEngine"
      ENGINE_INSTALL_COUNT=$((ENGINE_INSTALL_COUNT + 1))
    else
      record warn "Install engine helper" "could not write $USER_ENGINE_DIR"
    fi

    if [[ "$ENGINE_INSTALL_COUNT" -eq 0 ]]; then
      record fail "Install engine helper" "no plugin auto-launch path was updated"
    fi
  else
    record fail "Install engine helper" "engine binary unavailable"
  fi
else
  record warn "Engine helper install" "skipped; use --setup-engine-link to update plugin auto-launch paths"
fi

if command_exists auval && [[ -d "$USER_AU_DIR/punch2pen.component" ]]; then
  AUVAL_LOG="$(mktemp -t punch2pen-auval.XXXXXX)"
  if auval -v aufx P2pn Dcta >"$AUVAL_LOG" 2>&1; then
    record pass "AU validation" "aufx/P2pn/Dcta, log $AUVAL_LOG"
  else
    record fail "AU validation" "aufx/P2pn/Dcta failed; log $AUVAL_LOG"
  fi
elif command_exists auval; then
  record warn "AU validation" "punch2pen.component is not installed in $USER_AU_DIR"
else
  record warn "AU validation" "auval unavailable"
fi

section "Manual DAW checklist"
cat <<CHECKLIST
1. Start with Logic Pro if that is your target DAW:
   - Install the AU with: $0 --install-plugin
   - Quit Logic, then run: auval -v aufx P2pn Dcta
   - If needed, reset Audio Unit cache:
     killall -9 AudioComponentRegistrar 2>/dev/null || true
     rm -f ~/Library/Caches/AudioUnitCache/com.apple.audiounits.cache
2. Start the engine before opening the session:
   $ENGINE_BIN
3. Insert punch2pen on an audio track, arm the track, press record, and speak for 5-10 seconds.
4. If no transcript appears, keep the engine terminal visible and check:
   - Did the plugin connect to 127.0.0.1:7483?
   - Is Logic actually in record, not just playback?
   - Is the model present at $MODEL_FILE?
5. If transcript appears but timing is wrong, test a simple 4/4 session first, then your compound-meter session.
CHECKLIST

if [[ -n "$OPEN_DAW" ]]; then
  case "$OPEN_DAW" in
    logic|reaper|ableton)
      REQUESTED_DAW_PATH="$(daw_path "$OPEN_DAW")"
      if [[ -d "$REQUESTED_DAW_PATH" ]]; then
        open "$REQUESTED_DAW_PATH"
        record pass "Open DAW" "$REQUESTED_DAW_PATH"
      else
        record fail "Open DAW" "$OPEN_DAW was requested but not detected"
      fi
      ;;
    *) record fail "Open DAW" "unknown DAW '$OPEN_DAW'" ;;
  esac
fi

section "Final report"
for line in "${CHECK_RESULTS[@]}"; do
  log "$line"
done
log ""
log "Pass: $PASS_COUNT  Warn: $WARN_COUNT  Fail: $FAIL_COUNT"

if [[ -n "$REPORT_FILE" ]]; then
  {
    printf 'punch2pen DAW integration report\n'
    printf 'Generated: %s\n\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    for line in "${CHECK_RESULTS[@]}"; do printf '%s\n' "$line"; done
    printf '\nPass: %s  Warn: %s  Fail: %s\n' "$PASS_COUNT" "$WARN_COUNT" "$FAIL_COUNT"
  } > "$REPORT_FILE"
  log "Report written to $REPORT_FILE"
fi

if [[ "$FAIL_COUNT" -gt 0 ]]; then
  exit 1
fi
exit 0
