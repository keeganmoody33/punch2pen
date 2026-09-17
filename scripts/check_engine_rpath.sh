#!/bin/bash
set -euo pipefail

# Fail if punch2penEngine still has a CI/build-machine LC_RPATH, or if it
# links @rpath dylibs that are not sitting next to the binary.
# Usage: check_engine_rpath.sh <engine-binary-or-app> [more...]
# No-op on non-Darwin (exit 0).

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "check_engine_rpath.sh: skip (not Darwin)"
  exit 0
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <punch2penEngine|punch2penEngine.app> [...]" >&2
  exit 2
fi

resolve_bin() {
  local input="$1"
  if [[ -d "$input" && "$input" == *.app ]]; then
    printf '%s\n' "${input}/Contents/MacOS/punch2penEngine"
    return 0
  fi
  printf '%s\n' "$input"
}

fail=0

check_one() {
  local input="$1"
  local bin dir load rpath dep name
  bin="$(resolve_bin "$input")"
  if [[ ! -f "$bin" ]]; then
    echo "FAIL: engine binary not found: $input -> $bin" >&2
    fail=1
    return
  fi
  dir="$(cd "$(dirname "$bin")" && pwd)"

  echo "Checking ${bin}"

  while IFS= read -r rpath; do
    [[ -z "$rpath" ]] && continue
    case "$rpath" in
      @executable_path|@executable_path/*|@loader_path|@loader_path/*|@rpath|@rpath/*)
        ;;
      *)
        echo "FAIL: LC_RPATH is not relocatable: ${rpath}" >&2
        echo "      (must be @executable_path or @loader_path, not a CI/build path)" >&2
        fail=1
        ;;
    esac
    case "$rpath" in
      *"/Users/runner"*|*"/tmp/punch2pen"*|*"/build/lib"*)
        echo "FAIL: LC_RPATH looks like a build machine path: ${rpath}" >&2
        fail=1
        ;;
    esac
  done < <(otool -l "$bin" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { pending = 1 }
    pending && $1 == "path" { print $2; pending = 0 }
  ')

  while IFS= read -r dep; do
    [[ -z "$dep" ]] && continue
    case "$dep" in
      *"/Users/runner"*|*"/tmp/punch2pen"*)
        echo "FAIL: linked against a build-machine dylib: ${dep}" >&2
        fail=1
        ;;
      @rpath/*|@loader_path/*|@executable_path/*)
        name="${dep##*/}"
        if [[ ! -f "${dir}/${name}" ]]; then
          echo "FAIL: ${dep} is not next to $(basename "$bin") (${dir}/${name} missing)" >&2
          fail=1
        fi
        ;;
    esac
  done < <(otool -L "$bin" | awk 'NR > 1 { print $1 }')
}

for arg in "$@"; do
  check_one "$arg"
done

if [[ "$fail" -ne 0 ]]; then
  echo "Engine rpath/dylib check failed." >&2
  exit 1
fi

echo "Engine rpath/dylib check passed."
