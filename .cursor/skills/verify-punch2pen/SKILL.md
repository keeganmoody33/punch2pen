---
name: verify-punch2pen
description: Drive punch2pen's Mac Studio Receipt AU/VST3 plugin and punch2penEngine over loopback TCP 7483. Use when proving engine ready, correction IPC, plugin identity, or Studio Receipt states — not Windows, not DAW GUI automation.
---

# Verify punch2pen

Punch2Pen is a **Mac DAW plugin** (JUCE AU + VST3, Studio Receipt WebView) plus a **local engine daemon**. They talk over **TCP `127.0.0.1:7483`**. There is **no scripted DAW host** in this tree: do not invent Logic/REAPER/Ableton automation. Windows is out of scope.

This skill is for the next agent. Drive only what the repo can actually run.

## Surface (interview)

| What a user touches | In this repo | Automatable here? |
|---|---|---|
| Studio Receipt editor inside a DAW (`plugin/Source/ui/public/index.html` in WKWebView) | Primary UI. Wordmark `[ PUNCH2PEN ]`. States `disconnected` / `idle` / `recording` / `playback` / `correction`. | **No DAW driver.** JUCE `AudioPluginHost` is fetched when `PUNCH2PEN_BUILD_PLUGIN=ON` but has no session script. |
| `punch2penEngine` CLI | `./build/bin/punch2penEngine` (optional `--cloud` / `--api-key=`). Ready line: `Engine ready.` | **Yes** — launch, log, port. |
| Correction (click a word → Apply) | WebView `submitCorrection` → `IPCClient::sendCorrection` → engine `Applied correction.` → `~/.punch2pen/corrections.csv` | **Yes on the wire** (same Correction message the plugin sends). Click-in-DAW is manual. |
| Living Transcript while recording | `processBlock` captures only when `isRecording`; results come back as `TranscriptionResult`. | **Human DAW only.** `scripts/verify_transcription.py` is a sine-wave protocol probe, not STT. Use `scripts/verify_engine.py vocals` with `fixtures/vocals/dry-vocal.wav`. |
| AU identity | `auval -v aufx P2pn Dcta`. CMake `PLUGIN_MANUFACTURER_CODE "Dcta"`, `PLUGIN_CODE "P2pn"`, `FORMATS VST3 AU`. | Read-only checks. **Never rename Dcta / P2pn / aufx.** |
| Windows VST3 | Mentioned in docs, not this skill | Skip. |

Existing harnesses (use these; do not add a fake DAW): `scripts/engine_smoke.sh`, `scripts/verify_engine.py`, `scripts/test_daw_integration.sh`, `scripts/verify_correction.py` (now exits 1 on a dead engine), `scripts/download_model.sh`, engine/plugin unit tests, `auval` on macOS.

## Launch

From the repo root. The helper is the launch path.

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen launch
```

What that does:

1. Refuses if **`127.0.0.1:7483` is already listening**. Port is hardcoded in `engine/src/main.cpp` / `IPCServer(7483)`. **Two engines cannot run side by side.** Do not hijack a user's engine.
2. Configures CMake into `build/` (gitignored). On Darwin: `-DPUNCH2PEN_BUILD_PLUGIN=ON`. On Linux/CI: engine only (`OFF`) — AU/VST3 proof is skipped, not faked. If `c++` cannot find a standard library (common when `c++` is Clang without libc++), the helper sets `CXX=g++` when `g++` exists.
3. Builds `punch2penEngine` if missing.
4. Starts the engine with an **isolated `HOME`** under `/tmp/punch2pen-verify-<run-id>/home` so `~/.punch2pen` is not the user's studio data. Copies or downloads `ggml-base.bin` into that HOME (`scripts/download_model.sh`).
5. Ready when the engine log contains **`Engine ready.`** and **`IPC Server started on 127.0.0.1:7483`**, and port 7483 accepts a TCP connect. First whisper load can take tens of seconds.

Local mode only. Do not pass `--cloud` unless the user asked and `OPENAI_API_KEY` is present. Cloud is a different transcriber, not the default verify path.

Mac plugin-in-DAW (manual, after engine-only verify is torn down — same port):

```bash
./scripts/test_daw_integration.sh --download-model --install-plugin --setup-engine-link --skip-engine
```

Then start **this skill's** engine **or** the packaged helper at `/Applications/Punch2Pen/punch2penEngine.app` — not both. Insert **punch2pen** on an audio track. Plugin auto-launch (`IPCClient::launchEngine`) looks at `PUNCH2PEN_ENGINE`, then `/Applications/Punch2Pen/punch2penEngine.app`, then `Contents/Helpers/punch2penEngine.app` inside the AU/VST3 bundle, via `/usr/bin/open -g -n`. If `open` fails it posix_spawns `Contents/MacOS/punch2penEngine`. A bare `/Applications/Punch2Pen/punch2penEngine` is the last fallback. Leftover `~/punch2pen/bin/punch2penEngine` is ignored. A GitHub Release pkg postinstall also starts the engine so Logic does not sit on WAIT.

Teardown: `.cursor/skills/verify-punch2pen/scripts/control-punch2pen cleanup` (kills **the pid this run started** only).

## Doctor

Run first whenever anything looks off:

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen doctor
```

Pass means all of:

- This run's pid file exists, the process is alive, and **that pid owns `127.0.0.1:7483`** (lsof/python). If the port is owned by someone else → **not worth driving**.
- Engine log has `Engine ready.` and `127.0.0.1:7483`.
- Isolated HOME is the run dir, not the user's `~/.punch2pen`.
- `plugin/CMakeLists.txt` still has `PLUGIN_MANUFACTURER_CODE "Dcta"` and `PLUGIN_CODE "P2pn"` and `FORMATS VST3 AU`.
- On Darwin: `punch2pen.vst3` exists under `build/`; AU `punch2pen.component` is warned if missing.
- On non-Darwin: doctor **passes engine checks** and **records `plugin-au-vst3: skipped (not Darwin)`**. Do not claim a Mac plugin was loaded.

## Drive

Harness: `control-punch2pen`. Read `features/README.md`, then the feature file. Prefer the helper over ad-hoc python.

Stable handles (do not use click coordinates):

- Engine stdout: `Engine ready.`, `Client connected!`, `Received Correction:`, `Applied correction. Vocabulary terms:`
- Listening port: `lsof` on `127.0.0.1:7483` (connect probes are real engine clients; avoid them for doctor)
- Wire: Correction message type **5**, little-endian headers matching `shared/Protocol.h`
- Files: `$VERIFY_HOME/.punch2pen/corrections.csv` (`original,corrected` per line), `profile_default.json` on engine shutdown
- Studio Receipt IDs: `#app-title`, `#status-badge`, `#connection-banner`, `#transcript-container`, `#correction-overlay`, `#correction-input`, `#correction-submit`, `#correction-cancel`; `body[data-state]`
- Badge copy: `WAIT` / `IDLE` / `REC` / `PLAY`
- AU: `auval -v aufx P2pn Dcta`

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive correction
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive identity
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive studio-receipt-contract
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive daw-transcript
```

`drive daw-transcript` **prints SKIP** with the unmet DAW precondition. That is success of the helper, not proof of the Living Transcript.

## Evidence

Named location (survives cleanup):

`.cursor/skills/verify-punch2pen/artifacts/<run-id>/`

Minimum for a proof:

- `doctor.txt` — doctor stdout
- The **action** (command + exit code), not only the end state
- The **result** (engine log excerpt + `corrections.csv` copy, or auval log, or HTML contract report)
- `feature-id.txt` naming the mapped feature and entry point

Standards:

- Exercise the user path. Correction proof is the **plugin's Correction IPC**, not a DatabaseManager unit test.
- Capture action and resulting state.
- Side effects: CSV row, log line, listening port. Do not trust a script named verify if it always exits 0 (`scripts/verify_correction.py` prints ❌ and still exits 0).
- Mocks only where tests already isolate (engine unit tests). They are not product proof.
- `scripts/verify_transcription.py` is not evidence of STT: protocol mismatch + sine wave is not vocals.
- Do not restyle Studio Receipt. Do not rename Dcta/P2pn/aufx.

## Cleanup

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen cleanup
```

Kills only the pid in this run's pidfile. Removes `/tmp/punch2pen-verify-<run-id>/`. **Does not delete** `.cursor/skills/verify-punch2pen/artifacts/<run-id>/`. Never `killall punch2penEngine`. After cleanup, confirm artifacts still exist.

Failed attempts: run cleanup before the next launch so port 7483 is free.

## Helpers

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen help
.cursor/skills/verify-punch2pen/scripts/control-punch2pen launch
.cursor/skills/verify-punch2pen/scripts/control-punch2pen doctor
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive correction
.cursor/skills/verify-punch2pen/scripts/control-punch2pen cleanup
```

Optional env: `PUNCH2PEN_VERIFY_RUN_ID`, `PUNCH2PEN_VERIFY_BUILD_DIR` (default `$REPO/build`).

Keep the map honest with `/maintain-verification-skill` as the product changes.
