# punch2pen — Design HANDOFF (Living Transcript WebView)

The plugin's visual face is one HTML document, `plugin/Source/ui/public/index.html`,
loaded into a JUCE `WebBrowserComponent`. It is the **Living Transcript**: it
highlights the word under the playhead, treats the active line as the title,
renders a Temporal Hierarchy (past / active / upcoming), scrolls in sync with
the DAW, and offers **Return to Live** when the user scrolls away.

Vibe: **Open** — dark, DAW-native, not a toy. Default window **400×600**
(C++ `setSize`), resizable; the CSS is fluid and follows the host.

The language is lyric highlighting, nothing else: Living Transcript, Temporal
Hierarchy, highlight the word, active line as title, synced scroll, Return to
Live. Do not borrow sing-along wording anywhere in copy or code.

---

## Files

```
plugin/
├── CMakeLists.txt                    (juce_add_binary_data embeds index.html)
└── Source/
    ├── PluginEditor.{h,cpp}          (thin shell; 400×600 default, resizable ≥400×400)
    ├── WebViewEditor.{h,cpp}         (C++ ↔ JS bridge, 30 Hz timer)
    └── ui/public/index.html          (entire plugin UI)
```

Identity is untouched: **Dcta / P2pn / aufx**.

---

## Layout (top → bottom)

| Region | ID | Height | Contents |
|---|---|---|---|
| Header | `#header-bar` | 44px | `[ PUNCH2PEN ]` wordmark (`#app-title`), **active-profile pill** (`#profile-pill`, placeholder), status badge (`#status-badge`: WAIT / IDLE / REC / PLAY) |
| Transport strip | `#position-display` | 30px | `BAR n · BEAT n` (`#position-text` with `.bar-num` / `.beat-num`), `#sync-indicator` (LIVE / SCROLLED) |
| Connection banner | `#connection-banner` | auto | “Waiting for engine connection…” — disconnected only |
| Transcript | `#transcript-container` | flex | `#transcript-scroll` (native scroller) → `#transcript-inner` → `.lyric-line` → `.lyric-word`; empty-state hints; fades; `#return-to-live`; docked `#correction-overlay` |
| Status bar | `#status-bar` | 30px | `#status-text` (state copy and the non-modal upgrade nudge), `#logo-mark` (placeholder) |

Inside `#transcript-inner` the bridge populates one line per bar (max 8 words):

```html
<div class="lyric-line line-active" data-bar="12">
  <span class="lyric-word past"   data-start="48000" data-end="52800">pen</span>
  <span class="lyric-word active" data-start="53000" data-end="57000">it</span>
  <span class="lyric-word upcoming" …>down</span>
</div>
```

---

## Temporal Hierarchy

| Tier | Line class | Treatment |
|---|---|---|
| Past | `.line-past` | sans, ~15px, opacity 0.34 |
| **Active (title)** | `.line-active` | sans **bold**, ~22px (scales with width via `cqi`), opacity 1 |
| Upcoming | `.line-upcoming` | sans, ~15px, opacity 0.66 |

Word under the playhead: `.lyric-word.active` — accent fill (`--accent` #FCD34D)
with dark ink. Past words inside the active line sit at 0.55. Between two word
timestamps the highlight **holds** on the last sung word (no flicker in whisper's
gaps); after the final word it holds for ~0.75 s.

While **recording**, words are provisional: the newest line is the title, no word
is filled, and `#stream-cursor` blinks at the end of the line. Playback
re-evaluates everything against real `startTime` / `endTime` samples.

Synced scroll keeps the active line at a focal point (~40% down the pane; ~60%
while recording) with an eased RAF scroll. Wheel / touch / scrollbar-drag by the
user leaves follow mode: `body.scrolled-away`, `#sync-indicator` reads SCROLLED,
and **Return to Live** appears bottom-right. Clicking it re-centres and resumes.
Programmatic scrolls, host resizes, and content growth never trip it.

---

## States

Contract attribute `body[data-state]` ∈ `disconnected | idle | recording | playback | correction`.
`body[data-mode]` carries the underlying transport state while a correction is
open, so REC/PLAY styling survives the sheet.

1. **disconnected** — banner + WAIT; empty-state “Waiting for the local engine”.
2. **idle** — connected, stopped. Empty-state “Arm the track and record” until words exist; with words, the line at/before the playhead is the title.
3. **recording** — REC pulse, provisional words, newest line as title, stream cursor.
4. **playback** — Living Transcript: highlight the word, active line as title, Temporal Hierarchy, synced scroll, Return to Live.
5. **correction** — docked sheet at the bottom of the transcript (`#correction-overlay`), composited over the underlying mode. Clicked word gets `.correcting`. Apply replaces every exact match in the take (`.corrected`) and sends `submitCorrection`; Cancel / Esc sends `onCorrectionCancelled`.

---

## C++ → JS API (called from `WebViewEditor`)

```js
window.appendWord(text, startSample, endSample, barNumber)
window.updatePlayhead(currentDAWSample)
window.setConnectionStatus(connected)        // boolean
window.updatePosition(bar, beat)             // ints
window.setState(stateName)                   // disconnected|idle|recording|playback|correction
window.resetTranscript()                     // clears all words (new take)
window.showCorrection(originalWord, x, y)    // x,y accepted; the sheet is docked
window.hideCorrection()
window.setActiveProfile({ name, kind, detail })   // placeholder for the login/profile PR
```

`setActiveProfile` drives `#profile-pill`; `kind` ∈ `local | pro | seat`. Until
login ships the pill reads **Local · This Mac · no account** and its popover's
Sign in button is disabled. No prices anywhere.

## JS → C++ API (native functions on `window.punch2pen`)

```js
window.punch2pen.onWordClicked(word, x, y)
window.punch2pen.submitCorrection(original, corrected)
window.punch2pen.onCorrectionCancelled()
window.punch2pen.onReady()
```

`onReady()` is the page handshake: the bridge buffers C++→JS calls made before
it fires and flushes them once the DOM is alive.

---

## Status-bar copy (non-modal)

| Situation | `#status-text` |
|---|---|
| disconnected | Local engine offline · transcription runs on this Mac |
| idle, no words | Local transcription · no account needed |
| idle, words | N words in this take · click a word to correct it |
| recording | Transcribing the punch · words land as the engine hears them |
| playback, following | Following the playhead · click a word to correct it |
| scrolled away | Scrolled away from the playhead |
| after a correction | “x” → “y” (in N places) · session-only on this Mac. A profile carries your dictionary between rooms. |

The last row is the upgrade nudge: it appears only after the user actually
corrects a word, never blocks, and names no price.

---

## Tokens

| Token | Value | Job |
|---|---|---|
| `--bg-deep` | #0F1012 | window |
| `--bg-pane` | #15161A | transcript |
| `--bg-chrome` | #1B1D21 | header, transport strip, status bar |
| `--bg-raised` | #23262B | pill, popover, sheet, Return to Live |
| `--ink` | #F3F3F1 | text |
| `--accent` | #FCD34D | wordmark “2”, bar/beat, WAIT, active word, Apply |
| `--rec` | #FF4D57 | REC, stream cursor |
| `--play` | #5CCB8A | PLAY, LIVE, corrected marker |

Type: chrome in system mono (`ui-monospace`, SF Mono, Menlo…); lyrics in system
sans (SF Pro / system-ui) for legibility. No webfonts from the network.

---

## Browser preview (no C++ bridge)

`index.html?preview=disconnected|idle|recording|playback|scrolled|correction[&t=seconds]`
seeds an original demo verse and, without `t`, runs a fake playhead. It never
executes inside the plugin (`window.__JUCE__.backend` present).

## Open follow-ups

1. **Logo asset.** `#logo-mark` is still the placeholder. Drop the real asset into
   `Source/ui/public/assets/` and extend the resource provider.
2. **Profile switcher.** `setActiveProfile` is the hook; the fast switcher, login,
   and seat isolation land with the paid-profile PR.
3. **Bar labels per line.** `data-bar` is the arrival bar from the C++ timer, not
   the sung bar; do not surface it as a gutter number until real tempo mapping exists.
4. **Live DAW visual verification** in Logic's WKWebView is still a human step.
