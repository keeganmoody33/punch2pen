# punch2pen — Design HANDOFF (Living Transcript WebView)

The plugin's visual face is one HTML document, `plugin/Source/ui/public/index.html`,
loaded into a JUCE `WebBrowserComponent`. It is the **Living Transcript**: it
highlights the word under the playhead, treats the active line as the title,
renders a Temporal Hierarchy (past / active / upcoming), scrolls in sync with
the DAW, and offers **Return to Live** when the user scrolls away.

Vibe: **Open** — dark, DAW-native, not a toy. **Booth paper:** mono chrome
(the punch), serif lyrics (the pen), stamp vermillion. Not the teal #21 restyle.
Default window **400×600** (C++ `setSize`), resizable; the CSS is fluid and
follows the host.

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
| Header | `#header-bar` | 44px | `[ PUNCH2PEN ]` wordmark (`#app-title`), **active-profile pill** (`#profile-pill`; opens the sign-in / seats popover), status badge (`#status-badge`: WAIT / IDLE / REC / PLAY) |
| Transport strip | `#position-display` | 30px | `BAR n · BEAT n` (`#position-text` with `.bar-num` / `.beat-num`), host BPM and `HH:MM:SS.mmm` (`#host-clock`, `#host-bpm`, `#host-time`), `#sync-indicator` (LIVE / SCROLLED) |
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
| Past | `.line-past` | serif, ~16px, opacity 0.38 |
| **Active (title)** | `.line-active` | serif **bold**, ~24px (scales with width via `cqi`), opacity 1 |
| Upcoming | `.line-upcoming` | serif, ~16px, opacity 0.68 |

Word under the playhead: `.lyric-word.active` — invert plate (paper fill, booth
ink). Past words inside the active line sit at 0.52. Between two word
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
window.setHostClock(bpm, numerator, denominator, seconds)  // host clock; time is HH:MM:SS.mmm
window.setState(stateName)                   // disconnected|idle|recording|playback|correction
window.resetTranscript()                     // clears all words (new take)
window.showCorrection(originalWord, x, y)    // x,y accepted; the sheet is docked
window.hideCorrection()
window.setActiveProfile({ name, kind, detail })   // pill; kind ∈ local | pro | seat
window.setProfileStatus(json)                     // engine ProfileStatus, string or object
```

`WebViewEditor` folds every engine `ProfileStatus` into `setActiveProfile` (pill)
and forwards the raw JSON to `setProfileStatus` (popover). The pill reads
**Local · This Mac · no account** on the free tier, the seat name with a green
dot when signed in with an active seat, and **No seat** when signed in without one.

## JS → C++ API (native functions on `window.punch2pen`)

```js
window.punch2pen.onWordClicked(word, x, y)
window.punch2pen.submitCorrection(original, corrected)
window.punch2pen.onCorrectionCancelled()
window.punch2pen.onReady()
window.punch2pen.profileCommand(json)        // one JSON string, see below
```

`onReady()` is the page handshake: the bridge buffers C++→JS calls made before
it fires and flushes them once the DOM is alive.

---

## Profile popover (sign-in, sign-out, seats)

The popover renders the engine's `ProfileStatus` and sends ops back. It holds
no account state of its own beyond "which form is showing".

| Status | Popover shows |
|---|---|
| `signedIn:false`, `cloudAvailable:false` | Local row + "Sign-in isn't available in this build" (no form) |
| `signedIn:false`, `login.stage` `''` / `sending` | Local row, copy, **email form** → `Send code` (disabled + "Sending…" while `sending`) |
| `login.stage` `code_sent` / `verifying` | **six-digit code form** → `Sign in` (auto-submits at 6 digits), "Code sent to *email* · Change email", `login.echoedCode` when a dev deployment echoes it |
| `login.stage` `error` | same form as before, `login.message` in red |
| `signedIn:true` | account line (`email` · Refresh · Sign out) and a **Seats** list from `profiles[]`: name, workspace, Owner/Seat, and for the active seat `N words in dictionary · sync` (or `N pending`). Click a seat → `set_active`. Suspended seats dim. `message` shows under the list. |

Ops sent through `profileCommand`:

```json
{"op":"status"}
{"op":"login_start","email":"you@studio.com"}
{"op":"login_verify","email":"you@studio.com","code":"482910"}
{"op":"set_active","profileId":"…"}
{"op":"logout"}
{"op":"refresh"}
```

While an op is in flight the relevant control is disabled until the next status
lands. Opening the popover with no status yet sends `{"op":"status"}` once.
No prices, plan names, or billing copy anywhere.

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
| after a correction, free | “x” → “y” (in N places) · session-only on this Mac. A profile carries your dictionary between rooms. |
| after a correction, paid | “x” → “y” (in N places) · saved to *Name* dictionary · synced / N pending sync |

The free row is the upgrade nudge: it appears only after the user actually
corrects a word, never blocks, and names no price.

---

## Tokens

Booth paper. Same tokens as `site/public/site.css`. Not the teal #21 restyle.

| Token | Value | Job |
|---|---|---|
| `--bg-deep` | #100E0C | window / editor chrome behind the WebView |
| `--bg-pane` | #171411 | transcript |
| `--bg-chrome` | #1E1A16 | header, transport strip, status bar |
| `--bg-raised` | #2A241E | pill, popover, sheet, Return to Live |
| `--ink` | #F3EBDA | paper text |
| `--accent` | #E4452F | stamp vermillion: wordmark “2”, Apply, checks |
| `--wait` | #C9A36A | WAIT / engine banner (not REC, not the old gold chip) |
| `--rec` | #E4452F | REC, stream cursor |
| `--play` | #6FBF8A | PLAY, LIVE, corrected marker |

Type: chrome in system mono (`ui-monospace`, SF Mono, Menlo…); lyrics and status
copy in system serif (Iowan / Palatino / Georgia) — the pen. No webfonts from
the network. Active word is an invert plate, not a colored fill.

---

## Browser preview (no C++ bridge)

`index.html?preview=disconnected|idle|recording|playback|scrolled|correction[&t=seconds]`
seeds an original demo verse and, without `t`, runs a fake playhead.
`index.html?preview=signin[&stage=email|code|seats]` adds a fake engine that
answers `profileCommand` with the real `ProfileStatus` shape (echo code `482910`).
Neither runs inside the plugin (`window.__JUCE__.backend` present).

## Open follow-ups

1. **Logo asset.** `#logo-mark` is still the placeholder. Drop the real asset into
   `Source/ui/public/assets/` and extend the resource provider.
2. **Bar labels per line.** `data-bar` is the sung bar: the word's sample
   position, the host BPM, and the time signature from `setHostClock`. It is
   not a gutter number. The readout is not a typed BPM and does not invent a frame rate.
3. **Live DAW visual verification** in Logic's WKWebView is still a human step.
