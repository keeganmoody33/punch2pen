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
  --self-test        Prove EXIT teardown SIGKILLs a SIGTERM-ignoring child
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

# SIGTERM is handled by the engine (sets a flag). If accept() or whisper
# does not return, wait-forever after kill hangs GitHub's 10-minute step.
stop_pid() {
  local pid="${1:-}"
  [[ -n "$pid" ]] || return 0
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill "$pid" >/dev/null 2>&1 || true
    local n=0
    while kill -0 "$pid" >/dev/null 2>&1 && [[ "$n" -lt 15 ]]; do
      sleep 0.2
      n=$((n + 1))
    done
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill -9 "$pid" >/dev/null 2>&1 || true
    fi
  fi
  wait "$pid" >/dev/null 2>&1 || true
}

cleanup() {
  stop_pid "${ENGINE_PID:-}"
}
trap cleanup EXIT

selftest_stop_pid() {
  local ready
  ready="$(mktemp /tmp/punch2pen-stop-ready.XXXXXX)"
  python3 -c 'import signal, sys, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
open(sys.argv[1], "w").write("ready")
time.sleep(3600)' "$ready" &
  local pid=$!
  local i=0
  while [[ ! -s "$ready" ]] && [[ "$i" -lt 50 ]]; do
    sleep 0.05
    i=$((i + 1))
  done
  rm -f "$ready"
  kill -0 "$pid" >/dev/null 2>&1 \
    || die "self-test: SIGTERM-ignoring child died before stop_pid"
  kill "$pid" >/dev/null 2>&1 || true
  sleep 0.2
  kill -0 "$pid" >/dev/null 2>&1 \
    || die "self-test: child exited on SIGTERM; handler was not installed"
  local start elapsed
  start="$(python3 -c 'import time; print("%.3f" % time.monotonic())')"
  stop_pid "$pid"
  elapsed="$(python3 -c "import time; print('%.3f' % (time.monotonic() - ${start}))")"
  if kill -0 "$pid" >/dev/null 2>&1; then
    kill -9 "$pid" >/dev/null 2>&1 || true
    die "self-test: stop_pid left a SIGTERM-ignoring child alive"
  fi
  python3 -c "import sys; sys.exit(0 if float('${elapsed}') < 8.0 else 1)" \
    || die "self-test: stop_pid took ${elapsed}s (must SIGKILL, not wait forever)"
  log "engine_smoke: stop_pid self-test PASS (${elapsed}s)"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) RUN_BUILD=0; shift ;;
    --skip-vocals) RUN_VOCALS=0; shift ;;
    --require-vocals) REQUIRE_VOCALS=1; shift ;;
    --self-test) selftest_stop_pid; exit 0 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

cd "$ROOT"
chmod +x "$ROOT/scripts/download_model.sh" "$ROOT/scripts/engine_smoke.sh" 2>/dev/null || true

python3 "$ROOT/scripts/verify_engine.py" selftest
selftest_stop_pid

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

LOG="$ISOLATED/engine.stdout"
DATA_LOG="$ISOLATED/.punch2pen/engine.log"
"$ENGINE" >"$LOG" 2>&1 &
ENGINE_PID=$!
log "engine_smoke: pid $ENGINE_PID stdout=$LOG data-log=$DATA_LOG"

# Non-TTY CI: punch2penEngine dup2's stdout into ~/.punch2pen/engine.log.
log_has() {
  local needle="$1"
  grep -q "$needle" "$LOG" 2>/dev/null && return 0
  grep -q "$needle" "$DATA_LOG" 2>/dev/null && return 0
  return 1
}

dump_engine_logs() {
  log "----- $LOG -----" >&2
  tail -n 80 "$LOG" >&2 || true
  log "----- $DATA_LOG -----" >&2
  tail -n 80 "$DATA_LOG" >&2 || true
}

ready=0
for _ in $(seq 1 180); do
  if ! kill -0 "$ENGINE_PID" >/dev/null 2>&1; then
    dump_engine_logs
    die "engine exited before ready"
  fi
  if log_has 'Engine ready.' && log_has '127.0.0.1:7483' && port_busy; then
    ready=1
    break
  fi
  sleep 1
done
[[ "$ready" -eq 1 ]] || {
  dump_engine_logs
  die "timed out waiting for Engine ready. on 127.0.0.1:${PORT}"
}
log "engine_smoke: Engine ready."

python3 "$ROOT/scripts/verify_engine.py" smoke --port "$PORT"

found=0
for _ in $(seq 1 20); do
  if log_has "Received Correction:" && log_has "Applied correction."; then
    found=1
    break
  fi
  sleep 0.25
done
[[ "$found" -eq 1 ]] || {
  dump_engine_logs
  die "engine log missing Received/Applied correction"
}

CSV="$ISOLATED/.punch2pen/corrections.csv"
[[ -f "$CSV" ]] || die "missing $CSV after correction"
grep -qxF "punch 2 pen,Punch2Pen" "$CSV" || die "CSV missing punch 2 pen,Punch2Pen"
log "engine_smoke: correction CSV ok"

if [[ "$RUN_VOCALS" -eq 1 ]]; then
  VOCAL_ARGS=(
    vocals
    --port "$PORT"
    --stt-timeout "${PUNCH2PEN_STT_TIMEOUT:-60}"
  )
  if [[ "$REQUIRE_VOCALS" -eq 1 ]]; then
    VOCAL_ARGS+=(--require)
  fi
  python3 "$ROOT/scripts/verify_engine.py" "${VOCAL_ARGS[@]}"
else
  log "engine_smoke: vocals skipped"
fi

log "engine_smoke: PASS (no Logic)"
exit 0
