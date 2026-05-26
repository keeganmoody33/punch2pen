# punch2pen — Design HANDOFF (WebView UI)

This is the production hand-off for the **Studio Receipt** design direction
(Direction C, approved). The plugin's visual face is now a single HTML
document loaded into a JUCE `WebBrowserComponent`. All five canonical UI
states the design system explored are implemented and driven by a small
JS bridge.

---

## Files in this hand-off

```
plugin/
├── CMakeLists.txt                    (MODIFIED)
└── Source/
    ├── PluginEditor.h                (REWRITTEN — thin shell that hosts WebViewEditor)
    ├── PluginEditor.cpp              (REWRITTEN — same)
    ├── WebViewEditor.h               (NEW — C++ ↔ JS bridge)
    ├── WebViewEditor.cpp             (NEW — same)
    └── ui/
        └── public/
            └── index.html            (NEW — entire plugin UI)
```

The legacy native components (`TranscriptView`, `CorrectionEditor`,
`PositionDisplay`) are still listed in `target_sources` so unit tests
continue to link. They are no longer instantiated by the editor and can
be deleted in a follow-up PR once you confirm the WebView build is good
on every CI target.

---

## Element IDs (14, all bindable from C++)

| ID                       | Role                                                    |
|--------------------------|---------------------------------------------------------|
| `header-bar`             | 40px header strip                                       |
| `app-title`              | `[ PUNCH2PEN ]` wordmark                                |
| `status-badge`           | WAIT / IDLE / REC / PLAY                                |
| `position-display`       | 36px transport strip                                    |
| `position-text`          | "Bar X | Beat Y" — children `.bar-num`, `.beat-num`     |
| `connection-banner`      | Yellow "Waiting for engine connection…" (Disconnected)  |
| `transcript-container`   | Scroll viewport                                         |
| `correction-overlay`     | 260×90 popup                                            |
| `correction-original`    | The mis-transcribed word                                |
| `correction-input`       | `<input>` for the corrected text                        |
| `correction-submit`      | Green Apply button                                      |
| `correction-cancel`      | Red Cancel button                                       |
| `logo-mark`              | Fist+bolt placeholder, bottom-right                     |
| _(implicit)_ `body`      | Carries `data-state` for state-driven CSS               |

Inside `#transcript-container`, the bridge populates:

```html
<div class="lyric-line" data-bar="N">
  <span class="lyric-word" data-start="48000" data-end="52800">hello</span>
</div>
```

---

## C++ → JS API (called from `WebViewEditor`)

```js
window.appendWord(text, startSample, endSample, barNumber)
window.updatePlayhead(currentDAWSample)
window.setConnectionStatus(connected)        // boolean
window.updatePosition(bar, beat)             // ints
window.setState(stateName)                   // disconnected|idle|recording|playback|correction
window.resetTranscript()                     // clears all words
window.showCorrection(originalWord, x, y)
window.hideCorrection()
```

`updatePlayhead` iterates `.lyric-word`, classifies each as
`past`/`active`/`upcoming` per the spec's alpha system (1.0 / 0.6 / 0.4),
and updates `#transcript-inner.style.transform` via a `requestAnimationFrame`
spring loop tuned to the same `0.15` ease factor used by
`juce::VBlankAttachment` in `TranscriptView.cpp`.

## JS → C++ API (registered as native functions on `window.punch2pen`)

```js
window.punch2pen.onWordClicked(word, x, y)
window.punch2pen.submitCorrection(original, corrected)
window.punch2pen.onCorrectionCancelled()
window.punch2pen.onReady()
```

`onReady()` is the page handshake: the bridge buffers any C++→JS calls
made before it fires and flushes them once the DOM is alive.

---

## 5 canonical visual states

Driven by `body[data-state]`. The bridge sets it from the processor's
transport flags and IPC connection status (see
`WebViewEditor::timerCallback`):

1. **disconnected** — yellow `#FCD34D` "Waiting for engine" banner takes
   over the transcript pane; badge shows `WAIT`. Triggered by
   `setConnectionStatus(false)`.
2. **idle** — connected, transport stopped. Empty pane with "— no signal — /
   armed and ready · press ● REC on your DAW". Badge `IDLE`.
3. **recording** — RED pulse badge `REC`. All words render at the
   spec's uniform `0.6` provisional alpha — past/active styling is
   suppressed via `!important` overrides because incoming words are
   provisional until playback re-evaluates them. A `#stream-cursor`
   blinks at the end of the active line.
4. **playback** — full 3-tier karaoke (`.past` 0.4 / `.active` 1.0 /
   `.upcoming` 0.6) with spring-eased scroll. Badge `PLAY` (green).
5. **correction** — overlay only. Overlay layer is composited *on top*
   of whichever underlying state is current. `hideCorrection()` drops
   back to the saved state.

---

## Color tokens (from existing JUCE constants)

| Token              | Hex      | Source                                              |
|--------------------|----------|-----------------------------------------------------|
| `--bg-primary`     | #1C1917  | `TranscriptView.h` line 65                          |
| `--text-primary`   | #FAFAF9  | `TranscriptView.h` line 66                          |
| `--accent-primary` | #FCD34D  | `TranscriptView.h` line 67                          |
| `--window-bg`      | #1E1E1E  | `PluginEditor.cpp` paint() line 61                  |
| `--header-bg`      | #2D2D2D  | `CorrectionEditor.cpp` line 17                      |
| `--input-bg`       | #3A3A3A  | `CorrectionEditor.cpp` line 19                      |
| `--border-color`   | #505050  | `CorrectionEditor.cpp` line 18 (popup outline)      |
| `--submit-green`   | #4A9F4A  | `CorrectionEditor.cpp` line 22                      |
| `--cancel-red`     | #9F4A4A  | `CorrectionEditor.cpp` line 26                      |

---

## Layout sizing (locked)

- Plugin window: **400×600**
- Header bar: full width × **40px**, title font **16px**
- Position display: full width × **36px** (bumped from 30 to avoid clipping),
  font **18px bold** (dropped from 24 per sizing tweak)
- Transcript container: fills remaining space; `.lyric-line` is **40px** tall
- Correction overlay: **260×90px**, 6px inner padding
- Logo mark: fixed bottom-right corner, 56×56 at 0.45 opacity, all states

---

## Open follow-ups

1. **Logo asset.** The bottom-right mark is currently an inline SVG
   placeholder. Drop the real PNG into `Source/ui/public/assets/` and
   either inline it as a data URL or add it to `juce_add_binary_data`
   alongside `index.html`. The resource provider in `WebViewEditor.cpp`
   already covers a single URL — extend it to match by suffix once you
   add the asset.

2. **WebView2 / WKWebView availability.** `NEEDS_WEB_BROWSER TRUE` on
   `juce_add_plugin` triggers the system check JUCE requires. The
   provided `WebBrowserComponent::Options` request the modern WebView2
   backend on Windows; macOS uses WKWebView automatically. CEF is *not*
   pulled in — keeps the binary small.

3. **`getSampleRate()` on the processor.** The editor's `timerCallback`
   computes `currentSamplePosition` from `transport.ppq · bpm · sr`.
   `Punch2PenAudioProcessor` currently has no public `getSampleRate()` —
   add one (or use `AudioProcessor::getSampleRate()` directly, which is
   the same thing) before this compiles cleanly. One-liner.

4. **Streaming sample placement.** The bridge mirrors the existing
   heuristic (`streamCursorSample += 4800.0` per word) from
   `TranscriptView::onTranscriptionReceived`. If you change that on the
   engine side to send real timestamps, propagate them through the
   `appendWord` call and remove the heuristic here.

5. **Retire the legacy components.** Once the WebView build is green on
   every CI target, drop `TranscriptView.*`, `CorrectionEditor.*`, and
   `PositionDisplay.*` from `target_sources` and delete them. The unit
   tests in `tests/` that touch the old editor will need to be updated
   or scoped to processor-only behaviour.
