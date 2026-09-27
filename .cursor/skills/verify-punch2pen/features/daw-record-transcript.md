# DAW record transcript

DAW transcript is the product loop: drop the plugin on a vocal track and press play to transcribe audio already there, or arm and punch in a new take. The Living Transcript follows the host playhead. Click a word to correct it. This tree has a readiness script and a manual checklist. It does **not** have automated host validation.

## Sub-features

- `daw-ready` machine/build/model/plugin checks via `scripts/test_daw_integration.sh`.
- `daw-playback-capture` audio already on the track is captured while the host is playing, including when the track is not record-armed. Stopped transport does not capture. A live punch still captures while the host reports recording.
- `daw-highlight` words land on the host clock. Playback highlights the word under the playhead and treats the active line as the title. Click a word to correct. Not karaoke.
- `daw-manual` human confirmation in Logic, REAPER, or Ableton on Mac.
- `daw-skip` honest skip when no DAW, no Darwin, or stale transcription helper.

## How to get to it (user POV)

- Run `./scripts/test_daw_integration.sh --download-model --install-plugin --setup-engine-link`.
- Start one engine on 7483, open Logic/REAPER/Ableton, insert punch2pen on a vocal track that already has audio, press play without record-arming, and watch the existing vocal land on the Living Transcript. Arm and punch in when you want a new take.
- Click a wrong word and Apply (see [Correction IPC](./correction-ipc.md)).

## Driving it with control-punch2pen

Preconditions:

- Darwin with a detected DAW **and** a human at the session, or this feature is SKIP.
- Port 7483 is free or owned by the single engine you intend to use.
- `scripts/verify_transcription.py` is **not** used as STT proof (a 440 Hz sine is not vocals). Use `scripts/verify_engine.py vocals --playback` when `fixtures/vocals/dry-vocal.wav` exists. That sends the fixture as non-recording playback and requires words on the host playhead.

- **Automated skip (default for agents).** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive daw-transcript`. Exit code `0` with status `SKIP`. Stdout names the missing piece (not Darwin, no DAW app, or no human host driver). Write that stdout to `artifacts/<run-id>/daw-transcript-skip.txt`. This is **not** a pass of the Living Transcript.
- **Readiness only (Mac, optional).** Run `./scripts/test_daw_integration.sh --download-model --report artifacts/<run-id>/daw-readiness.txt` **after** `control-punch2pen cleanup` so ports do not collide. Treat `[FAIL]` lines as unreadiness, not as a substitute for a recorded take.
- **Human playback (Mac).** Follow the checklist printed by `test_daw_integration.sh`: press play on a track that already has vocal audio (not record-armed), confirm words on the host timeline, confirm the word under the playhead and the active line, click a word to correct. A live punch is the same transcript while recording. Evidence is a screenshot of Studio Receipt in `playback` plus the engine log `Transcription:` lines. Agents cannot sign this off.
- **Proof.** Feature ID `daw-record-transcript`. If only the skip path ran, the artifact must say `SKIP` and the unmet precondition. Do not file engine unit tests or sine-wave IPC as this feature.

## Gotchas

- README roadmap still says end-to-end host validation is unimplemented. Believe it.
- `--open-daw logic` only `open`s the app. It does not insert the plugin or press record.
- Plugin IPC tests use port **17483** with `autoLaunchEngine=false`. Green `ipcClientTest` is not DAW proof and must not start the real engine.
- Two listeners on 7483: if the plugin auto-launches while `control-punch2pen` still holds the port, connections race. Cleanup first.
- Real vocals are required for STT quality. Do not claim WER from this skill.
