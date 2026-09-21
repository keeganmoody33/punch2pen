# Correction IPC

Correction lets a user replace a mis-heard word. Studio Receipt Apply sends the same binary Correction message the engine feeds into the active dictionary and logs as applied. That wire path is what this feature proves without a DAW.

Free/lite (the default with no sign-in) keeps corrections in a **session dictionary**: they bias this engine run, map the raw word case-sensitively before it reaches the plugin, and are dropped on restart. Nothing is written to disk. Paid/pro (signed in with an active profile) writes the profile's dictionary to `~/.punch2pen/profiles/<id>.json` and syncs it to the profile API; that path is covered by `engine/tests/AccountManagerTest.cpp`, not by this feature.

## Sub-features

- `correction-connect` opens TCP to the running engine on `127.0.0.1:7483`.
- `correction-send` transmits original and corrected UTF-8 strings as message type 5.
- `correction-log` shows `Received Correction:` then `Applied correction.`
- `correction-status` requests a `ProfileCommand {"op":"status"}` (type 7) and reads back a `ProfileStatus` (type 8) with `tier: "free"`, `dictionary.scope: "session"`, `dictionary.entries >= 1`.
- `correction-no-disk` proves the isolated HOME has no `corrections.csv`, `account.json`, or `profiles/` after the correction.
- `correction-ui` is the Apply button `#correction-submit` in the plugin — same payload, Mac DAW only.

## How to get to it (user POV)

- In the plugin editor, click a `.lyric-word`, type the replacement in `#correction-input`, choose `Apply ↵` (`#correction-submit`).
- Press Enter in the correction overlay (same commit).
- Choose `Cancel` (`#correction-cancel`) to dismiss without sending.
- On Free, the first Apply also shows the non-modal `#status-line` nudge under the transcript. It is not a dialog and it names no price.

## Driving it with control-punch2pen

Preconditions:

- `control-punch2pen doctor` passes for this run's isolated engine.
- The engine log for this run does not already contain the fixture pair `punch 2 pen` -> `Punch2Pen`.
- You are not attaching to a user's studio engine.

- **Send fixture correction.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive correction`. Exit code `0`. Stdout includes `Sent Correction: 'punch 2 pen' -> 'Punch2Pen'`.
- **Engine received it.** Engine log contains `Client connected!`, `Received Correction: 'punch 2 pen' -> 'Punch2Pen'`, `Applied correction. Vocabulary terms:`, and `Profile: free — local, session-only corrections`.
- **Session dictionary.** `scripts/verify_engine.py profile --expect-tier free --min-entries 1` prints the ProfileStatus JSON and `profile: PASS`. The helper saves it to `artifacts/<run-id>/profile-status.json`.
- **Nothing persisted.** Isolated `$HOME/.punch2pen/` has no `corrections.csv`, `account.json`, or `profiles/`.
- **UI entry (Mac DAW, optional).** With the plugin editor open and a word on screen, click the word, enter `Punch2Pen`, choose Apply. Same log lines, and the `#profile-pill` still reads `LOCAL`. If the editor is not open, record SKIP for `correction-ui` — do not treat the IPC send as a click proof.
- **Proof.** Write `artifacts/<run-id>/feature-id.txt` with `correction-ipc` / `drive correction`. Keep the send transcript, log excerpt, and status JSON. Cleanup must leave those files.

## Gotchas

- `scripts/verify_correction.py` speaks the right protocol and **exits 1** on connection refused. Prefer `scripts/engine_smoke.sh` or `control-punch2pen drive correction`.
- Driving a user's real engine while they are signed in trains **their** profile dictionary. Doctor must show the isolated HOME.
- Empty or identical replacement is not sent by `WebViewEditor::nativeSubmitCorrection`. The helper sends a distinct pair on purpose.
- Multi-word originals (the fixture is one) feed vocabulary bias but never map a single transcript word; single-word originals do both.
- Unit tests under `engine/tests/` (`dictionaryTest`, `accountManagerTest`) cover the dictionary directly and are not this feature.
