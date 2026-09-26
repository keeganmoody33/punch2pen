#!/usr/bin/env python3
"""Logic-free host-clock test for the Studio Receipt page.

The preview paints BPM 120 until the bridge speaks. setHostClock must replace
that readout, and a word's bar must come from its sample position plus the
host BPM and time signature — not from a bar number passed at arrival, and
not from the preview's 120.
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
  navigator: { userAgent: 'punch2pen-host-clock-test' },
};
window.window = window;

vm.createContext(sandbox);
try {
  vm.runInContext(script, sandbox, { filename: 'index.html' });
} catch (err) {
  console.error('FAIL: page script threw');
  console.error(err && err.stack ? err.stack : err);
  process.exit(1);
}

if (typeof window.setHostClock !== 'function' || typeof window.appendWord !== 'function') {
  console.error('FAIL: setHostClock or appendWord is not on the page');
  process.exit(1);
}

// 100000 samples at 48 kHz. 120 BPM 4/4 is bar 2; 90 BPM 4/4 is bar 1.
const sample = 100000;
const hostBpm = 90;
window.setHostClock(hostBpm, 4, 4, 12.5);

const bpmText = String(byId('host-bpm').textContent || '').trim();
if (bpmText === '120' || bpmText !== '90') {
  console.error('FAIL: host BPM stayed at the preview 120 after setHostClock(90), got ' + JSON.stringify(bpmText));
  process.exit(1);
}

const timeText = String(byId('host-time').textContent || '').trim();
if (timeText !== '00:00:12.500') {
  console.error('FAIL: host time was not HH:MM:SS.mmm from host seconds, got ' + JSON.stringify(timeText));
  process.exit(1);
}

window.appendWord('sung', sample, sample + 8000, 9);
const line = byId('transcript-inner').children[0];
const bar = line && line.dataset ? String(line.dataset.bar) : '';
if (bar !== '1') {
  console.error(
    'FAIL: word bar ignored host tempo ' + hostBpm +
    ' (expected 1, preview-120 would be 2, arrival bar was 9), got ' + JSON.stringify(bar)
  );
  process.exit(1);
}

console.log('host-clock: PASS bpm ' + bpmText + ' time ' + timeText + ' bar ' + bar);
process.exit(0);
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
