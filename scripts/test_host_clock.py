#!/usr/bin/env python3
"""Logic-free host-clock test for the Studio Receipt page.

Fails if setHostClock is missing (the page before the host clock landed).
setHostClock(96, 4, 4, 10) must replace the preview BPM of 120.

Sample 48000*4 is 192000 samples (4 seconds at 48 kHz), not one bar.
At 120 BPM in 4/4 a bar is 2 seconds (96000 samples), so that sample is
the downbeat of bar 3. At 96 BPM a bar is 2.5 seconds (120000 samples),
so the same sample is still inside bar 2.
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
  console.error('FAIL: setHostClock does not exist on the page');
  process.exit(1);
}

const sample = 48000 * 4;
const inner = byId('transcript-inner');

function lineBar(index) {
  const line = inner.children[index];
  return line && line.dataset ? String(line.dataset.bar) : '';
}

window.setHostClock(120, 4, 4, 0);
window.appendWord('at-120', sample, sample + 1000, 9);
const barAt120 = lineBar(0);
if (barAt120 !== '3') {
  console.error(
    'FAIL: sample ' + sample + ' at 120 BPM 4/4 should be bar 3, got ' + JSON.stringify(barAt120)
  );
  process.exit(1);
}

window.setHostClock(96, 4, 4, 10);
const bpmText = String(byId('host-bpm').textContent || '').trim();
if (bpmText === '120' || bpmText !== '96') {
  console.error('FAIL: displayed BPM is still 120 after setHostClock(96, 4, 4, 10), got ' + JSON.stringify(bpmText));
  process.exit(1);
}
const timeText = String(byId('host-time').textContent || '').trim();
if (timeText !== '00:00:10.000') {
  console.error('FAIL: host time was not HH:MM:SS.mmm for 10 seconds, got ' + JSON.stringify(timeText));
  process.exit(1);
}

window.appendWord('at-96', sample, sample + 1000, 9);
const barAt96 = lineBar(1);
if (barAt96 !== '2') {
  console.error(
    'FAIL: sample ' + sample + ' at 96 BPM 4/4 should be bar 2 (a bar is longer than at 120), got ' +
    JSON.stringify(barAt96)
  );
  process.exit(1);
}

console.log('host-clock: PASS bpm ' + bpmText + ' time ' + timeText + ' bar120 ' + barAt120 + ' bar96 ' + barAt96);
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
