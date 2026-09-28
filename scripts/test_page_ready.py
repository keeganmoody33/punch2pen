#!/usr/bin/env python3
"""Logic-free Studio Receipt page-ready test.

The page script is evaluated with window.__JUCE__.backend absent. The backend
is installed afterwards. onReady must be retried and deliver
setConnectionStatus(true). data-mode must not stay disconnected (badge WAIT).

TCP accept is not a success condition. HandshakeResponse is still required by
scripts/verify_engine.py selftest.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / "plugin" / "Source" / "ui" / "public" / "index.html"

NODE = r"""
const fs = require('fs');
const vm = require('vm');

const html = fs.readFileSync(process.env.PAGE_HTML, 'utf8');
const start = html.lastIndexOf('<script>');
const end = html.lastIndexOf('</script>');
if (start < 0 || end < start) {
  console.error('FAIL: page script not found');
  process.exit(1);
}
const script = html.slice(start + '<script>'.length, end);

function makeEl() {
  const classList = {
    add() {},
    remove() {},
    contains() { return false; },
    toggle(_name, force) { return force !== false; },
  };
  return {
    dataset: {},
    classList,
    style: { setProperty() {} },
    textContent: '',
    className: '',
    hidden: false,
    value: '',
    title: '',
    disabled: false,
    clientHeight: 400,
    scrollHeight: 0,
    scrollTop: 0,
    offsetTop: 0,
    offsetHeight: 0,
    children: [],
    appendChild(child) { this.children.push(child); return child; },
    append() {},
    replaceChildren() { this.children = []; },
    addEventListener() {},
    removeEventListener() {},
    setAttribute() {},
    getAttribute() { return null; },
    focus() {},
    select() {},
    click() {},
    closest() { return null; },
    getBoundingClientRect() { return { left: 0, top: 0, bottom: 0, width: 0, height: 0 }; },
    querySelector() { return makeEl(); },
    querySelectorAll() { return []; },
  };
}

const elements = new Map();
function byId(id) {
  if (!elements.has(id)) elements.set(id, makeEl());
  return elements.get(id);
}

const document = {
  body: makeEl(),
  documentElement: makeEl(),
  getElementById(id) { return byId(id); },
  querySelector(sel) { return byId(String(sel)); },
  querySelectorAll() { return []; },
  createElement() { return makeEl(); },
  addEventListener() {},
  removeEventListener() {},
};

const location = { search: '', href: 'https://juce.backend/' };
const window = {
  document,
  location,
  addEventListener() {},
  removeEventListener() {},
};

const sandbox = {
  window,
  document,
  location,
  console,
  setTimeout,
  clearTimeout,
  setInterval,
  clearInterval,
  requestAnimationFrame(fn) { return setTimeout(() => fn(Date.now()), 0); },
  cancelAnimationFrame(id) { clearTimeout(id); },
  performance: { now: () => Date.now() },
  URLSearchParams,
  navigator: { userAgent: 'punch2pen-page-ready-test' },
};
window.window = window;

vm.createContext(sandbox);
try {
  vm.runInContext(script, sandbox, { filename: 'index.html' });
} catch (err) {
  console.error('FAIL: page script threw before the backend existed');
  console.error(err && err.stack ? err.stack : err);
  process.exit(1);
}

const modeBefore = document.body.dataset.mode;
if (modeBefore !== 'disconnected') {
  console.error('FAIL: expected data-mode=disconnected before the backend appeared, got ' + modeBefore);
  process.exit(1);
}

window.__JUCE__ = {
  backend: {
    onReady() { window.setConnectionStatus(true); },
    onWordClicked() {},
    submitCorrection() {},
    onCorrectionCancelled() {},
    profileCommand() {},
  },
};

setTimeout(() => {
  const mode = document.body.dataset.mode;
  const state = document.body.dataset.state;
  const badge = (byId('status-badge').textContent || '').trim();
  if (mode === 'disconnected' || state === 'disconnected' || badge === 'WAIT') {
    console.error(
      'FAIL: page stayed on disconnected/WAIT after setConnectionStatus(true) ' +
      '(data-mode=' + mode + ' data-state=' + state + ' badge=' + badge + ')'
    );
    process.exit(1);
  }
  console.log('page-ready: PASS left data-mode ' + mode + ' badge ' + badge);
  process.exit(0);
}, 500);
"""


def main() -> int:
    if not HTML.is_file():
        print(f"FAIL: missing {HTML}", file=sys.stderr)
        return 1
    node = shutil.which("node")
    if not node:
        print("FAIL: node is required to evaluate the Studio Receipt page", file=sys.stderr)
        return 1
    completed = subprocess.run(
        [node, "-e", NODE],
        env={**os.environ, "PAGE_HTML": str(HTML)},
        check=False,
    )
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
