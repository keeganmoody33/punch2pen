#!/usr/bin/env bash
# Fail if published site copy invents prices or uses the banned
# product-confusion word. Scan public/ and src/ only — not this script.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

fail=0
if ! test -f public/index.html; then
  echo "missing public/index.html" >&2
  fail=1
fi
if ! test -f src/worker.js; then
  echo "missing src/worker.js" >&2
  fail=1
fi

scan_copy() {
  grep -Rni --exclude-dir=node_modules --exclude-dir=.wrangler "$@" public src
}

if scan_copy -E 'karaoke'; then
  echo "banned product-confusion word found in site copy" >&2
  fail=1
fi

# Dollar amounts look like invented SKUs. Survey copy must not name a price.
if scan_copy -E '\$[0-9]|[0-9]+[[:space:]]*/[[:space:]]*mo|\$X'; then
  echo "site copy looks like it invents a price" >&2
  fail=1
fi

if ! grep -q 'Living Transcript' public/index.html; then
  echo "index.html must explain Living Transcript" >&2
  fail=1
fi
if ! grep -Rq 'download_click' public/; then
  echo "site must wire download_click" >&2
  fail=1
fi
if ! grep -Rq 'interest_would_pay' public/; then
  echo "site must wire interest_would_pay" >&2
  fail=1
fi

exit "$fail"
