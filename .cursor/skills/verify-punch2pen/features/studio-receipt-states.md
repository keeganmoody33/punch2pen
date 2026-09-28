# Studio Receipt states

Studio Receipt is the plugin face: a 400×600 (resizable) WebView with five `body[data-state]` values, a `[ PUNCH2PEN ]` wordmark, and a correction overlay. This feature proves the HTML contract and documents the live DAW states. It does not restyle the UI.

## Sub-features

- `state-disconnected` shows `#connection-banner` “Waiting for engine connection…” and badge `WAIT`.
- `state-idle` is connected, transport stopped, empty pane “— no signal —”, badge `IDLE`.
- `state-recording` is connected + DAW record, badge `REC`, provisional words, `#stream-cursor`.
- `state-playback` is connected + playing, Living Transcript classes `past` / `active` / `upcoming`, badge `PLAY`.
- `state-correction` shows `#correction-overlay` over the saved state; Cancel or Apply hides it.
- `state-contract` asserts the bindable IDs and state names in `plugin/Source/ui/public/index.html`.

## How to get to it (user POV)

- Insert punch2pen on a Mac audio track and open the editor (AU or VST3).
- Disconnect or stop the engine to see WAIT / banner.
- Connect the engine, leave transport stopped: IDLE.
- Arm the track and record: REC.
- Play back a take with words: PLAY Living Transcript.
- Click a word: correction overlay.
- Open `plugin/Source/ui/public/index.html` in a browser for the **static preview** only (no C++ bridge).

## Driving it with control-punch2pen

Preconditions:

- Checkout contains `plugin/Source/ui/public/index.html`.
- You are not changing CSS, tokens, or copy.
- No DAW automation is available in this repo.

- **HTML contract.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive studio-receipt-contract`. Exit code `0`. Report lists IDs `app-title`, `status-badge`, `connection-banner`, `transcript-container`, `correction-overlay`, `correction-input`, `correction-submit`, `correction-cancel`, `logo-mark` and states `disconnected`, `idle`, `recording`, `playback`, `correction`. Wordmark source contains `[ PUNCH`.
- **Static preview (optional).** Open the same HTML file. Initial `body[data-state]` is `disconnected`, badge `WAIT`, banner visible. In the page console, `setState('idle')` shows IDLE and hides the banner. This is preview-only; do not file it as WKWebView-in-DAW proof.
- **Live DAW states (Mac, manual).** After `control-punch2pen cleanup` (free 7483), install the plugin, start one engine, insert punch2pen, then walk WAIT → IDLE → REC → PLAY → correction. Screenshot each `data-state` with `#app-title` visible. If no DAW is installed, record SKIP per state with the missing host path — do not use JUCE AudioPluginHost unless you are prepared to operate it by hand (no recipe exists).
- **Proof.** Save the contract report under `artifacts/<run-id>/studio-receipt-contract.txt`. Manual screenshots, if any, go in the same directory. Feature ID `studio-receipt-states`.

## Gotchas

- README architecture still draws native `TranscriptView` / `CorrectionEditor`. The live editor is WebView. Do not drive the legacy C++ widgets.
- `setState('correction')` keeps the previous badge (`—` in the JS map). Assert the overlay, not `REC`.
- Recording suppresses Living Transcript `.active` styling on purpose. Do not fail REC because words are not inverted.
- Default size is 400×600; the editor is resizable. Size is not a failure.
- Do not restyle Direction C tokens or replace the wordmark as part of verify.
