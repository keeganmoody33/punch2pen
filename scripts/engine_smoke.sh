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
REAP_SELFTEST_ONLY=0
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
  --reap-selftest    Kill a SIGTERM-ignoring dummy and exit (no engine)
  --build-dir PATH   CMake build directory (default: $BUILD_DIR)
  -h, --help
USAGE
}

log() { printf '%s\n' "$*"; }
die() { printf 'engine_smoke: %s\n' "$*" >&2; exit 1; }

# Descendants first, then root. Used so leftover whisper children are SIGKILL'd
# instead of becoming orphans the script would wait on.
pids_of_tree() {
  local pid="$1"
  local child
  for child in $(pgrep -P "$pid" 2>/dev/null || true); do
    pids_of_tree "$child"
  done
  printf '%s\n' "$pid"
}

# SIGTERM, brief poll, SIGKILL. Never `wait` — punch2penEngine's SIGTERM
# handler calls stop() from the signal path and can sit in whisper forever.
kill_engine_tree() {
  local root="${1:-}"
  local pids
  [[ -n "$root" ]] || return 0

  pids="$(pids_of_tree "$root" | tr '\n' ' ')"
  if [[ -n "${pids// /}" ]]; then
    # shellcheck disable=SC2086
    kill -TERM $pids >/dev/null 2>&1 || true
  fi

  local i=0
  while [[ $i -lt 5 ]]; do
    if ! kill -0 "$root" >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
    i=$((i + 1))
  done

  pids="$(pids_of_tree "$root" | tr '\n' ' ')"
  if [[ -n "${pids// /}" ]]; then
    # shellcheck disable=SC2086
    kill -KILL $pids >/dev/null 2>&1 || true
  fi
  # Reap a SIGKILL'd job only. Do not wait after SIGTERM — that is the CI hang.
  wait "$root" >/dev/null 2>&1 || true
}

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
  local pid="${ENGINE_PID:-}"
  ENGINE_PID=""
  kill_engine_tree "$pid"
}
trap cleanup EXIT

# Prove PASS-path teardown cannot hang on a SIGTERM-ignoring dummy + child.
reap_selftest() {
  local dummy="" childfile="" child="" start elapsed
  childfile="$(mktemp /tmp/punch2pen-reap.XXXXXX)"
  bash -c 'trap "" TERM INT; sleep 60 & echo $! >"$0"; wait' "$childfile" &
  dummy=$!
  local n=0
  while [[ $n -lt 50 && ! -s "$childfile" ]]; do
    sleep 0.02
    n=$((n + 1))
  done
  child="$(cat "$childfile" 2>/dev/null || true)"
  start="$(python3 -c 'import time; print(time.monotonic())')"
  kill_engine_tree "$dummy"
  elapsed="$(python3 -c "import time; print('{:.3f}'.format(time.monotonic() - float('$start')))")"
  rm -f "$childfile"
  if kill -0 "$dummy" >/dev/null 2>&1; then
    die "reap selftest: dummy engine $dummy still alive"
  fi
  if [[ -n "$child" ]] && kill -0 "$child" >/dev/null 2>&1; then
    kill -KILL "$child" >/dev/null 2>&1 || true
    die "reap selftest: dummy child $child still alive"
  fi
  python3 -c "import sys; sys.exit(0 if float('$elapsed') < 2.0 else 1)" \
    || die "reap selftest: took ${elapsed}s; must not wait on leftover processes"
  log "selftest: PASS engine reap after PASS (${elapsed}s)"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) RUN_BUILD=0; shift ;;
    --skip-vocals) RUN_VOCALS=0; shift ;;
    --require-vocals) REQUIRE_VOCALS=1; shift ;;
    --reap-selftest) REAP_SELFTEST_ONLY=1; shift ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

cd "$ROOT"
chmod +x "$ROOT/scripts/download_model.sh" "$ROOT/scripts/engine_smoke.sh" 2>/dev/null || true

python3 "$ROOT/scripts/verify_engine.py" selftest
reap_selftest
if [[ "$REAP_SELFTEST_ONLY" -eq 1 ]]; then
  exit 0
fi

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
kill_engine_tree "${ENGINE_PID:-}"
ENGINE_PID=""
exit 0
