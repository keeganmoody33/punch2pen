#!/bin/bash
set -euo pipefail

# Make punch2penEngine relocatable on macOS.
# Copies remaining non-system dylibs next to the binary and replaces LC_RPATH
# with @executable_path / @loader_path so dyld does not look in the CI build
# tree (/Users/runner/.../build/lib or /tmp/punch2pen-.../lib).
#
# Usage: bundle_engine_libs.sh <engine-binary> [extra-search-dir ...]
# No-op on non-Darwin.

if [[ "$(uname -s)" != "Darwin" ]]; then
  exit 0
fi

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <engine-binary> [extra-search-dir ...]" >&2
  exit 2
fi

BIN_ARG="$1"
shift

if [[ ! -f "$BIN_ARG" ]]; then
  echo "Engine binary not found: $BIN_ARG" >&2
  exit 1
fi

DEST="$(cd "$(dirname "$BIN_ARG")" && pwd)"
BIN="${DEST}/$(basename "$BIN_ARG")"

SEARCH_DIRS="${DEST}"
for extra in "$@"; do
  if [[ -d "$extra" ]]; then
    SEARCH_DIRS="${SEARCH_DIRS}
$(cd "$extra" && pwd)"
  fi
done

list_load_dylibs() {
  otool -L "$1" 2>/dev/null | awk 'NR > 1 { print $1 }'
}

list_rpaths() {
  otool -l "$1" 2>/dev/null | awk '
    $1 == "cmd" && $2 == "LC_RPATH" { pending = 1 }
    pending && $1 == "path" { print $2; pending = 0 }
  '
}

is_system_lib() {
  case "$1" in
    /usr/lib/*|/System/*|/Library/Apple/*) return 0 ;;
  esac
  return 1
}

# Absolute rpaths on the original binary are where CMake dropped dylibs.
while IFS= read -r rp; do
  [[ -z "$rp" ]] && continue
  case "$rp" in
    @*) continue ;;
  esac
  if [[ -d "$rp" ]]; then
    SEARCH_DIRS="${SEARCH_DIRS}
${rp}"
  fi
done < <(list_rpaths "$BIN")

find_lib() {
  local name="$1"
  local dir
  while IFS= read -r dir; do
    [[ -z "$dir" ]] && continue
    if [[ -f "${dir}/${name}" ]]; then
      printf '%s\n' "${dir}/${name}"
      return 0
    fi
  done <<< "$SEARCH_DIRS"
  return 1
}

QUEUED=$'\n'

queue_contains() {
  case "$QUEUED" in
    *$'\n'"$1"$'\n'*) return 0 ;;
  esac
  return 1
}

enqueue_name() {
  local name="$1"
  [[ -z "$name" ]] && return 0
  if queue_contains "$name"; then
    return 0
  fi
  QUEUED="${QUEUED}${name}"$'\n'
}

scan_macho() {
  local macho="$1"
  local dep
  while IFS= read -r dep; do
    [[ -z "$dep" ]] && continue
    case "$dep" in
      @rpath/*|@loader_path/*|@executable_path/*)
        enqueue_name "${dep##*/}"
        ;;
      /*)
        if ! is_system_lib "$dep"; then
          enqueue_name "${dep##*/}"
        fi
        ;;
    esac
  done < <(list_load_dylibs "$macho")
}

scan_macho "$BIN"
shopt -s nullglob
for lib in "${DEST}"/*.dylib; do
  enqueue_name "$(basename "$lib")"
  scan_macho "$lib"
done
shopt -u nullglob

COPIED=1
while [[ "$COPIED" -eq 1 ]]; do
  COPIED=0
  while IFS= read -r name; do
    [[ -z "$name" ]] && continue
    case "$name" in
      *.dylib) ;;
      *) continue ;;
    esac
    dest_lib="${DEST}/${name}"
    if [[ -f "$dest_lib" ]]; then
      continue
    fi
    src="$(find_lib "$name" || true)"
    if [[ -z "$src" ]]; then
      echo "warning: ${name} is linked from $(basename "$BIN") but was not found in search dirs" >&2
      continue
    fi
    cp "$src" "$dest_lib"
    chmod 755 "$dest_lib"
    COPIED=1
    scan_macho "$dest_lib"
  done <<< "$QUEUED"
done

rewrite_rpaths() {
  local macho="$1"
  local rp
  while IFS= read -r rp; do
    [[ -z "$rp" ]] && continue
    install_name_tool -delete_rpath "$rp" "$macho" 2>/dev/null || true
  done < <(list_rpaths "$macho")
  install_name_tool -add_rpath "@executable_path" "$macho" 2>/dev/null || true
  install_name_tool -add_rpath "@loader_path" "$macho" 2>/dev/null || true
}

shopt -s nullglob
for lib in "${DEST}"/*.dylib; do
  name="$(basename "$lib")"
  install_name_tool -id "@rpath/${name}" "$lib" 2>/dev/null || true
  rewrite_rpaths "$lib"
done
shopt -u nullglob

rewrite_rpaths "$BIN"

if command -v codesign >/dev/null 2>&1; then
  codesign --force -s - "$BIN" >/dev/null 2>&1 || true
  shopt -s nullglob
  for lib in "${DEST}"/*.dylib; do
    codesign --force -s - "$lib" >/dev/null 2>&1 || true
  done
  shopt -u nullglob
fi

echo "Relocatable engine: ${BIN} (rpath=@executable_path:@loader_path)"
