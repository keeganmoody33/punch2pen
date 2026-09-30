# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Punch2Pen is a Mac DAW plugin (JUCE AU + VST3) plus a local transcription engine daemon. It transcribes vocals in real time and shows them as a **Living Transcript** on the host clock. `README.md` covers install, release, and the Logic/DAW manual checklist; `CONTEXT.md` is the domain glossary (use its terms: Engine, Plugin, Audio Chunk, Correction, Vocabulary, Initial Prompt, Profile/Seat).

## Commands

### Native (engine + plugin, CMake, C++20)

```bash
# Engine only (fast; what most engine work needs)
cmake -S . -B build -DPUNCH2PEN_BUILD_PLUGIN=OFF
cmake --build build --target punch2penEngine -j4

# With plugin (fetches pinned JUCE; macOS only)
cmake -S . -B build -DPUNCH2PEN_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target punch2penEngine punch2pen_plugin_AU punch2pen_plugin_VST3 -j4
```

Tests are standalone executables (no test framework/ctest). To run one: build its target, then run the binary.

```bash
# Engine tests → build/bin/<name>
cmake --build build --target dictionaryTest && ./build/bin/dictionaryTest
# targets: dictionaryTest accountManagerTest cloudProfileClientTest protocolSerializationTest
#          ipcServerHandshakeTest openAIJsonTest transcriptionCoordinatorTest transcriptTimingTest

# Plugin tests (needs -DPUNCH2PEN_BUILD_PLUGIN=ON) → build/plugin/<name>_artefacts/Release/<name>
cmake --build build --config Release --target ringBufferTest
./build/plugin/ringBufferTest_artefacts/Release/ringBufferTest
# targets: ringBufferTest ipcClientTest pluginProcessorStateTest pluginProcessorCaptureTest

# Python checks CI also runs
python3 scripts/verify_engine.py selftest
python3 scripts/test_page_ready.py
python3 scripts/test_host_clock.py
```

Engine runtime and end-to-end checks:

```bash
./scripts/download_model.sh base          # ~/.punch2pen/models/ggml-base.bin (engine exits 1 without it)
./build/bin/punch2penEngine               # local whisper; --cloud --api-key=… (or OPENAI_API_KEY) for OpenAI Realtime
./scripts/engine_smoke.sh --skip-build    # isolated PUNCH2PEN_HOME: handshake + correction + optional vocal fixture
python3 scripts/verify_engine.py vocals --playback   # engine timeline coverage only; SKIPs without the WAV
./installer/macos/build_pkg.sh            # unsigned dist/Punch2Pen_Installer.pkg
```

Port `127.0.0.1:7483` is hardcoded and only one engine can bind it. If the installed LaunchAgent holds it, stop it before smoke tests: `launchctl bootout "gui/$(id -u)" /Library/LaunchAgents/com.doctaaa.punch2pen.engine.plist`.

### Cloud (Convex, `cloud/`)

```bash
cd cloud && npm ci && npm run typecheck    # CI gate
npx convex dev                             # dev deployment only; never `convex deploy` from here
CONVEX_AGENT_MODE=anonymous npx convex dev # isolated local deployment for cloud agents (127.0.0.1:3211)
```

### Site (Cloudflare Worker, `site/`)

```bash
./site/check.sh                  # CI copy gates (see below)
cd site && npx wrangler dev      # local; deploy is `npx wrangler deploy` or the Deploy site workflow
```

### Design system (`design/`)

```bash
node design/tokens/build.mjs                       # tokens.json → tokens.css + contrast report
python3 design/brand/build_marks.py                # regenerate brand SVGs
(cd design/shaders && npm ci && node build.mjs)    # vendored p2p-shaders.js
node design/tools/render.mjs                       # needs playwright; re-renders frames
```

`tokens.json` is the source of truth; `tokens.css` and `shaders/src/tokens.generated.js` are generated.

## Architecture

Two processes talk over local TCP on `127.0.0.1:7483` using the little-endian binary wire format in `shared/Protocol.h` (message types: AudioChunk, TranscriptionResult, Handshake, HandshakeResponse, Correction=5, TransportStop, ProfileCommand=7, ProfileStatus=8; the last two carry raw JSON). Any wire change must update `shared/Protocol.h`, both sides, `protocolSerializationTest`, and `scripts/p2p_protocol.py` (used by the Python verifiers).

**Plugin (`plugin/Source/`)** — `PluginProcessor::processBlock` copies input into a lock-free SPSC `AudioRingBuffer` only while the host transport is playing or recording (stopped transport never captures) and leaves the audio buffer untouched (zero effect latency). `IPCClient` is a `juce::Thread` that handshakes, drains the ring buffer into `AudioChunk`s stamped with host sample time, sends Correction/TransportStop, and auto-launches the engine (`EngineLaunchPaths.h`: `PUNCH2PEN_ENGINE` → `/Applications/Punch2Pen/punch2penEngine.app` → nested `Contents/Helpers/punch2penEngine.app`, via `open -g -n` so Logic's AU sandbox isn't inherited). The whole UI is one HTML file, `plugin/Source/ui/public/index.html`, embedded as JUCE BinaryData and driven by `WebViewEditor` (C++ ↔ JS bridge on a 30 Hz timer). The plugin must instantiate with the engine down; it sits on WAIT until a HandshakeResponse arrives (TCP accept alone is not "connected").

**Engine (`engine/src/`)** — `main.cpp` binds 7483 *before* constructing `AccountManager`, which is constructed *before* the whisper `Transcriber`; CI asserts this ordering by text search in `main.cpp`. `TranscriptionCoordinator` polls `IPCServerInterface` in a 1 ms loop, feeds audio to a `TranscriberInterface` backend (`Transcriber` = whisper.cpp, `OpenAICloudTranscriber` = OpenAI Realtime WebSocket, resampled 48→16 kHz), finalizes on TransportStop, routes Corrections/ProfileCommands to the `ProfileService`, and reapplies vocabulary bias when the dictionary revision changes. Interfaces (`IPCServerInterface`, `TranscriberInterface`, `ProfileService`, HTTP transport) exist so unit tests use fakes; tests compile only the sources they need (see `engine/CMakeLists.txt`).

**Free vs paid** — `AccountManager` owns tier. Free/lite: in-memory session `Dictionary`, no files written, no network beyond 7483. Paid/pro: sign-in via `CloudProfileClient` (HTTP over IXWebSocket) against the Convex API in `cloud/`; per-profile dictionary cache at `~/.punch2pen/profiles/<id>.json`, session at `~/.punch2pen/account.json` (0600). The profile API URL comes from `-DPUNCH2PEN_PROFILE_API_URL`, `PUNCH2PEN_PROFILE_API`, or `~/.punch2pen/profile-api.json`; empty means free only. `cloud/convex/http.ts` is the contract `CloudProfileClient` and `cloudProfileClientTest` mirror — change them together. `PUNCH2PEN_HOME` isolates `~/.punch2pen` for tests.

**Packaging** — whisper/ggml/IXWebSocket are statically linked; remaining dylibs are bundled next to the engine with `@executable_path`/`@loader_path` rpaths. POST_BUILD steps run `installer/macos/bundle_engine_libs.sh`, `make_engine_app.sh` (nests the engine .app inside each AU/VST3 bundle), and `scripts/check_engine_rpath.sh`. A build-machine rpath leaking into the engine is a known past failure (dyld abort before `main`, nothing binds 7483).

## Invariants

- **Never rename the plugin identity**: manufacturer `Dcta`, plugin code `P2pn`, AU type `aufx`, bundle ID `com.doctaaa.punch2pen`.
- Don't restyle or rename the Studio Receipt IDs/states the bridge and verifiers bind to (`#status-badge`, `#transcript-container`, `#correction-overlay`, `body[data-state]`, badge copy `WAIT`/`IDLE`/`REC`/`PLAY`, etc.). Visual changes go through `design/`; see `design/README.md` "Mapping to shipped code". The plugin (`index.html`) and site (`site.css`) copy the token values from `design/tokens/tokens.json`; change them together.
- Copy language is lyric highlighting ("Living Transcript", "Temporal Hierarchy"). Never use sing-along wording; `site/check.sh` fails on "karaoke", invented prices (`$N`, `/mo`), password fields, SSO buttons, and forms on the creators page. No prices exist anywhere in the repo, including `cloud/`.
- Don't commit commercial songs; vocal fixtures follow `fixtures/vocals/README.md`.
- Don't bake an OpenAI key into the engine or installer.
- `auval -strict -v aufx P2pn Dcta` and Logic punch-ins are manual on a Mac; GitHub runners skip auval. There is no scripted DAW host — don't invent one. Only the owner can dispatch the macOS Release workflow (agents get 403).

## CI (`.github/workflows/ci.yml`)

macOS jobs (`engine-tests`, `plugin-tests`, `engine-smoke`) run only when paths under `engine|plugin|installer|scripts|shared|fixtures/`, root `CMakeLists.txt`, or `verify_setup.sh` change. `site-check` and `cloud-typecheck` always run on Ubuntu. `deploy-site.yml` deploys `site/` on pushes to `main` that touch `site/**` (or by manual dispatch).

## Verification skill

`.cursor/skills/verify-punch2pen/SKILL.md` is the agent verification harness. Use its helper rather than ad-hoc scripts:

```bash
.cursor/skills/verify-punch2pen/scripts/control-punch2pen launch   # isolated HOME, refuses if 7483 is taken
.cursor/skills/verify-punch2pen/scripts/control-punch2pen doctor
.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive correction   # also: identity, studio-receipt-contract, daw-transcript
.cursor/skills/verify-punch2pen/scripts/control-punch2pen cleanup  # kills only this run's pid; never `killall punch2penEngine`
```

Evidence goes to `.cursor/skills/verify-punch2pen/artifacts/<run-id>/`. Unit tests with mocks are not product proof; `scripts/verify_transcription.py` is a sine-wave protocol probe, not STT evidence.
