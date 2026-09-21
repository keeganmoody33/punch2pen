#!/usr/bin/env bash
# Fail the site if copy invents prices or uses the banned "karaoke" word.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
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

if rg -n -i --glob '!check.sh' --glob '!node_modules/**' --glob '!.wrangler/**' 'karaoke' .; then
  echo "banned word karaoke found in site/" >&2
  fail=1
fi

# Dollar amounts look like invented SKUs. Survey copy must not name a price.
if rg -n --glob '!check.sh' --glob '!node_modules/**' --glob '!.wrangler/**' -e '\$[0-9]' -e '[0-9]+\s*/\s*mo' -e '\$X' .; then
  echo "site copy looks like it invents a price" >&2
  fail=1
fi

if ! rg -q 'Living Transcript' public/index.html; then
  echo "index.html must explain Living Transcript" >&2
  fail=1
fi
if ! rg -q 'download_click' public/; then
  echo "site must wire download_click" >&2
  fail=1
fi
if ! rg -q 'interest_would_pay' public/; then
  echo "site must wire interest_would_pay" >&2
  fail=1
fi

exit "$fail"
