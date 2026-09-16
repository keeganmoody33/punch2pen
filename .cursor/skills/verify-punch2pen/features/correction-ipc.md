# Correction IPC

Correction lets a user replace a mis-heard word. Studio Receipt Apply sends the same binary Correction message the engine stores in `corrections.csv` and logs as applied. That wire path is what this feature proves without a DAW.

## Sub-features

- `correction-connect` opens TCP to the running engine on `127.0.0.1:7483`.
- `correction-send` transmits original and corrected UTF-8 strings as message type 5.
- `correction-log` shows `Received Correction:` then `Applied correction.`
- `correction-csv` appends `original,corrected` in the engine HOME data dir.
- `correction-ui` is the Apply button `#correction-submit` in the plugin — same payload, Mac DAW only.

## How to get to it (user POV)

- In the Studio Receipt editor, click a `.lyric-word`, type the replacement in `#correction-input`, choose `Apply ↵` (`#correction-submit`).
- Press Enter in the correction overlay (same commit).
- Choose `Cancel` (`#correction-cancel`) to dismiss without sending.

## Driving it with control-punch2pen

Preconditions:

- `control-punch2pen doctor` passes for this run's isolated engine.
- Isolated HOME `corrections.csv` does not already contain the fixture pair `punch 2 pen,Punch2Pen`.
- You are not attaching to a user's studio engine.

- **Send fixture correction.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive correction`. Exit code `0`. Stdout includes `Sent Correction: 'punch 2 pen' -> 'Punch2Pen'`.
- **Engine received it.** Engine log contains `Client connected!`, `Received Correction: 'punch 2 pen' -> 'Punch2Pen'`, and `Applied correction. Vocabulary terms:`.
- **Persistence.** Isolated `$HOME/.punch2pen/corrections.csv` contains a line `punch 2 pen,Punch2Pen`. Copy that file to `artifacts/<run-id>/corrections.csv`.
- **UI entry (Mac DAW, optional).** With the plugin editor open and a word on screen, click the word, enter `Punch2Pen`, choose Apply. Same CSV line and log. If the editor is not open, record SKIP for `correction-ui` — do not treat the IPC send as a click proof.
- **Proof.** Write `artifacts/<run-id>/feature-id.txt` with `correction-ipc` / `drive correction`. Keep the send transcript, log excerpt, and CSV copy. Cleanup must leave those files.

## Gotchas

- `scripts/verify_correction.py` speaks the right protocol but **exits 0 on connection refused**. Use `control-punch2pen drive correction`.
- Driving the user's `~/.punch2pen/corrections.csv` contaminates studio vocab. Doctor must show the isolated HOME.
- Empty or identical replacement is not sent by `WebViewEditor::nativeSubmitCorrection`. The helper sends a distinct pair on purpose.
- Profile JSON is written on **engine shutdown**, not on each correction. CSV is the live side effect.
- Unit tests under `engine/tests/` that call `DatabaseManager` directly are not this feature.
