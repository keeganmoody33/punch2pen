#!/usr/bin/env bash
# Isolated punch2penEngine smoke: handshake + correction, optional vocal fixture.
# Does not open Logic, auval, or a DAW. Refuses if 127.0.0.1:7483 is already taken.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PUNCH2PEN_BUILD_DIR:-$ROOT/build}"
PORT=7483
RUN_BUILD=1
RUN_VOCALS=1
REQUIRE_VOCALS=0
ENGINE_PID=""
ISOLATED=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Start punch2penEngine with an isolated PUNCH2PEN_HOME, prove handshake +
correction IPC, then run the vocal golden-file check (skip if no WAV).

Options:
  --skip-build       Use an existing \$PUNCH2PEN_BUILD_DIR/bin/punch2penEngine
  --skip-vocals      Do not run scripts/verify_engine.py vocals
  --require-vocals   Fail if fixtures/vocals/dry-vocal.wav is missing
  --build-dir PATH   CMake build directory (default: $BUILD_DIR)
  -h, --help
USAGE
}

log() { printf '%s\n' "$*"; }
die() { printf 'engine_smoke: %s\n' "$*" >&2; exit 1; }

port_busy() {
  python3 - "$PORT" <<'PY'
import socket, sys
s = socket.socket()
s.settimeout(0.4)
try:
    sys.exit(0 if s.connect_ex(("127.0.0.1", int(sys.argv[1]))) == 0 else 1)
finally:
    s.close()
PY
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
    --skip-build) RUN_BUILD=0; shift ;;
    --skip-vocals) RUN_VOCALS=0; shift ;;
    --require-vocals) REQUIRE_VOCALS=1; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

cd "$ROOT"
chmod +x "$ROOT/scripts/download_model.sh" "$ROOT/scripts/engine_smoke.sh" 2>/dev/null || true

python3 "$ROOT/scripts/verify_engine.py" selftest

if port_busy; then
  die "127.0.0.1:${PORT} is already listening. Stop that engine; this script will not hijack it."
fi

if [[ "$RUN_BUILD" -eq 1 ]]; then
  if [[ "$(uname -s)" != "Darwin" && -z "${CXX:-}" ]] && command -v g++ >/dev/null 2>&1; then
    export CXX=g++
    export CC="${CC:-gcc}"
  fi
  cmake -S "$ROOT" -B "$BUILD_DIR" -DPUNCH2PEN_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" --config Release --target punch2penEngine -j"${PUNCH2PEN_JOBS:-4}"
fi

ENGINE="$BUILD_DIR/bin/punch2penEngine"
[[ -x "$ENGINE" ]] || die "missing $ENGINE (build punch2penEngine or drop --skip-build)"

INVOKER_HOME="${PUNCH2PEN_INVOKER_HOME:-${HOME}}"
ISOLATED="$(mktemp -d /tmp/punch2pen-smoke.XXXXXX)"
export PUNCH2PEN_HOME="$ISOLATED"
export HOME="$ISOLATED"
mkdir -p "$ISOLATED/.punch2pen/models"
log "engine_smoke: isolated PUNCH2PEN_HOME=$ISOLATED"

MODEL_DST="$ISOLATED/.punch2pen/models/ggml-base.bin"
CACHE_MODEL="${PUNCH2PEN_MODEL_FILE:-}"
if [[ -z "$CACHE_MODEL" && -n "${PUNCH2PEN_MODEL_CACHE:-}" ]]; then
  CACHE_MODEL="$PUNCH2PEN_MODEL_CACHE/ggml-base.bin"
fi

copy_model() {
  local src="$1"
  [[ -f "$src" ]] || return 1
  cp "$src" "$MODEL_DST"
}

if ! copy_model "$CACHE_MODEL" \
  && ! copy_model "$INVOKER_HOME/.punch2pen/models/ggml-base.bin"; then
  if [[ -n "$CACHE_MODEL" ]]; then
    mkdir -p "$(dirname "$CACHE_MODEL")"
    log "engine_smoke: downloading ggml-base.bin into $CACHE_MODEL"
    TMP_HOME="$(mktemp -d /tmp/punch2pen-model-dl.XXXXXX)"
    HOME="$TMP_HOME" "$ROOT/scripts/download_model.sh" base
    cp "$TMP_HOME/.punch2pen/models/ggml-base.bin" "$CACHE_MODEL"
    rm -rf "$TMP_HOME"
    copy_model "$CACHE_MODEL" || true
  else
    log "engine_smoke: downloading ggml-base.bin into isolated HOME"
    HOME="$ISOLATED" PUNCH2PEN_HOME="$ISOLATED" "$ROOT/scripts/download_model.sh" base
  fi
fi

[[ -f "$MODEL_DST" ]] || die "whisper model missing at $MODEL_DST"
MODEL_SIZE="$(wc -c < "$MODEL_DST" | tr -d ' ')"
[[ "$MODEL_SIZE" -gt 1000000 ]] || die "model at $MODEL_DST is too small ($MODEL_SIZE bytes)"

LOG="$ISOLATED/engine.log"
"$ENGINE" >"$LOG" 2>&1 &
ENGINE_PID=$!
log "engine_smoke: pid $ENGINE_PID log $LOG"

ready=0
for _ in $(seq 1 90); do
  if ! kill -0 "$ENGINE_PID" >/dev/null 2>&1; then
    tail -n 40 "$LOG" >&2 || true
    die "engine exited before ready"
  fi
  if grep -q 'Engine ready.' "$LOG" 2>/dev/null && grep -q '127.0.0.1:7483' "$LOG" 2>/dev/null && port_busy; then
    ready=1
    break
  fi
  sleep 1
done
[[ "$ready" -eq 1 ]] || {
  tail -n 40 "$LOG" >&2 || true
  die "timed out waiting for Engine ready. on 127.0.0.1:${PORT}"
}
log "engine_smoke: Engine ready."

python3 "$ROOT/scripts/verify_engine.py" smoke --port "$PORT"

found=0
for _ in $(seq 1 20); do
  if grep -q "Received Correction:" "$LOG" && grep -q "Applied correction." "$LOG"; then
    found=1
    break
  fi
  sleep 0.25
done
[[ "$found" -eq 1 ]] || {
  tail -n 50 "$LOG" >&2 || true
  die "engine log missing Received/Applied correction"
}

CSV="$ISOLATED/.punch2pen/corrections.csv"
[[ -f "$CSV" ]] || die "missing $CSV after correction"
grep -qxF "punch 2 pen,Punch2Pen" "$CSV" || die "CSV missing punch 2 pen,Punch2Pen"
log "engine_smoke: correction CSV ok"

if [[ "$RUN_VOCALS" -eq 1 ]]; then
  VOCAL_ARGS=(vocals --port "$PORT")
  if [[ "$REQUIRE_VOCALS" -eq 1 ]]; then
    VOCAL_ARGS+=(--require)
  fi
  python3 "$ROOT/scripts/verify_engine.py" "${VOCAL_ARGS[@]}"
else
  log "engine_smoke: vocals skipped"
fi

log "engine_smoke: PASS (no Logic)"
exit 0
