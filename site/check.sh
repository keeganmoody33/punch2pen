#!/usr/bin/env bash
# Fail if published site copy invents prices or uses the banned
# product-confusion word. Scan public/ and src/ only — not this script.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

fail=0
for page in \
  public/index.html \
  public/product.html \
  public/pricing.html \
  public/for-studios.html \
  public/creators.html \
  public/docs.html \
  public/login.html \
  src/worker.js
do
  if ! test -f "$page"; then
    echo "missing $page" >&2
    fail=1
  fi
done

scan_copy() {
  # Vendor font bytes and the Paper shader bundle are not site copy.
  grep -Rni --exclude-dir=node_modules --exclude-dir=.wrangler \
    --exclude-dir=fonts --exclude-dir=shaders \
    "$@" public src
}

if scan_copy -E 'karaoke'; then
  echo "banned product-confusion word found in site copy" >&2
  fail=1
fi

if scan_copy -Ei 'lyrics[[:space:]]*\+[[:space:]]*timecode|portable lyrics|starting at|<video|demo video'; then
  echo "banned product-confusion phrase found in site copy" >&2
  fail=1
fi

if scan_copy -Ei 'type="password"|Sign in with Google|Sign in with Apple'; then
  echo "login page must not add a password or a non-working SSO button" >&2
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
if ! grep -q 'Download Free Utility' public/index.html; then
  echo "index.html primary CTA must say Download Free Utility" >&2
  fail=1
fi
if ! grep -q 'https://github.com/keeganmoody33/punch2pen/releases/latest' public/index.html; then
  echo "index.html must link the GitHub latest release" >&2
  fail=1
fi
if ! grep -q 'Not published yet' public/pricing.html; then
  echo "pricing must say the price is not published yet" >&2
  fail=1
fi
if ! grep -q 'Not shipped' public/product.html; then
  echo "product matrix must mark what is not shipped" >&2
  fail=1
fi
if ! grep -q 'Windows' public/product.html || ! grep -q 'iOS' public/product.html; then
  echo "product page must name Windows and iOS" >&2
  fail=1
fi
if ! grep -q 'not open yet' public/creators.html; then
  echo "creators page must say applications are not open yet" >&2
  fail=1
fi
if grep -Eq 'method=|action=|<form' public/creators.html; then
  echo "creators page must not submit a form" >&2
  fail=1
fi
if ! grep -q 'type="button"' public/creators.html; then
  echo "creators submit control must be a button, not a form submit" >&2
  fail=1
fi
if ! grep -qi 'not available' public/login.html; then
  echo "login page must say sign-in is not available" >&2
  fail=1
fi
if ! grep -q '"punch2pen.com"' wrangler.jsonc || ! grep -q '"www.punch2pen.com"' wrangler.jsonc; then
  echo "wrangler custom domains for apex and www must stay" >&2
  fail=1
fi

exit "$fail"
