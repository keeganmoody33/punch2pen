# Domain Glossary

This document outlines the core domain concepts of the `punch2pen` project.

- **Engine** — The standalone background C++ process that manages the whisper.cpp model, runs transcription inference, and persists user vocabulary and corrections.
- **Plugin** — The DAW audio plugin (built with JUCE) that runs inside the host, captures raw audio during recording, and displays the transcription.
- **Audio Chunk** — A contiguous block of audio samples captured by the Plugin and streamed to the Engine for transcription.
- **Correction** — A user-provided pair mapping mis-transcribed text to the intended correct text (e.g., "spoke" → "corrected"). Used to update the vocabulary.
- **Vocabulary** — The set of unique words extracted from the user's Corrections, used to dynamically bias the transcription engine.
- **Initial Prompt** — The formatted string composed of active Vocabulary words, passed to whisper.cpp to improve the accuracy of subsequent transcriptions.

## System Components

- **IPCClient** — JUCE Thread in the plugin managing the TCP connection to the engine. Completes Handshake/HandshakeResponse, drains the AudioRingBuffer, sends AudioChunk/TransportStop/Correction messages, and dispatches incoming TranscriptionResult events to listeners. On Mac it launches the packaged `punch2penEngine.app` via Launch Services (`open -g -n`: `/Applications/Punch2Pen` first, then nested `Contents/Helpers`) so a Logic AU sandbox does not inherit onto the helper. `open` failure immediately posix_spawns `Contents/MacOS/punch2penEngine`. It does not auto-launch leftover `~/punch2pen/bin/punch2penEngine` (`plugin/Source/IPCClient.h`, `plugin/Source/EngineLaunchPaths.h`).
- **IPCServer** — TCP server in the engine accepting plugin connections on 127.0.0.1:7483. Requires handshake before other messages. Queues incoming audio, transport-stop events, and corrections for the coordinator to consume (`engine/src/IPCServer.h`).
- **IPCServerInterface** — Abstract interface decoupling TranscriptionCoordinator from the concrete IPCServer, enabling test doubles (`engine/src/IPCServerInterface.h`).
- **Protocol** — Shared binary wire format header (`shared/Protocol.h`) defining MessageTypes: `AudioChunk`, `TranscriptionResult`, `Handshake`, `HandshakeResponse`, `Correction`, `TransportStop`, `ProfileCommand`, `ProfileStatus` (the last two carry raw JSON).
- **AudioRingBuffer** — SPSC lock-free ring buffer backed by `juce::AbstractFifo` for transferring float audio samples from the real-time audio thread to the IPC thread (`plugin/Source/RingBuffer.h`).
- **Transcriber** — Local whisper.cpp inference wrapper implementing TranscriberInterface (`engine/src/Transcriber.h`).
- **OpenAICloudTranscriber** — Cloud transcription via OpenAI Realtime WebSocket API implementing TranscriberInterface. Resamples from 48 kHz to 16 kHz and sends base64-encoded PCM (`engine/src/OpenAICloudTranscriber.h`).
- **TranscriberInterface** — Polymorphic base class for transcription backends, defining `pushAudioBlock`, `finalizeStream`, `setVocabularyBias`, and listener management (`engine/src/TranscriberInterface.h`).
- **TranscriptionCoordinator** — Engine main loop: polls IPCServer for audio, forwards to the active transcriber, handles transport-stop finalization, hands corrections and ProfileCommands to the ProfileService, and reapplies vocabulary bias when the dictionary revision changes (`engine/src/TranscriptionCoordinator.h`).
- **Dictionary** — One artist's correction pairs: case-sensitive word map applied to raw transcript words plus a capped, ranked vocabulary list for initial_prompt bias (`engine/src/Dictionary.h`).
- **AccountManager** — Owns tier (free/paid), the signed-in session, the active profile, and which Dictionary is live. Free: in-memory session dictionary, no files, no network. Paid: per-profile cache under `~/.punch2pen/profiles/` synced through CloudProfileClient on a worker thread (`engine/src/AccountManager.h`).
- **CloudProfileClient** — HTTP contract with the profile API in `cloud/` (login codes, sessions, dictionaries, seats); `IxHttpTransport` carries it over IXWebSocket (`engine/src/CloudProfileClient.h`).
- **WebViewEditor** — Plugin UI: Studio Receipt HTML in `juce::WebBrowserComponent`, driven by a JS bridge (`plugin/Source/WebViewEditor.h`).
- **Profile / Seat** — A profile is one artist's portable dictionary; a workspace owns profiles as seats. Only the seat holder can read or train that dictionary (`cloud/convex/profiles.ts`).

## Technical Abbreviations

- **SPSC** — Single-Producer Single-Consumer (ring buffer pattern used by AudioRingBuffer)
- **GGML** — Georgi Gerganov Machine Learning (whisper model format, e.g. `ggml-base.bin`)
- **VAD** — Voice Activity Detection (whisper.cpp feature for segmenting speech)
- **IPC** — Inter-Process Communication (TCP on localhost port 7483)
- **PPQ** — Pulses Per Quarter note (DAW timeline position unit)
- **VST3** — Virtual Studio Technology 3 (Steinberg plugin format)
- **AU** — Audio Unit (Apple plugin format)
- **CDP** — Chrome DevTools Protocol

## Data Persistence

- **Session dictionary (free)** — in memory only. Corrections bias this engine run and are dropped on restart or sign-out.
- **Whisper model** — `~/.punch2pen/models/ggml-base.bin` — downloaded via `scripts/download_model.sh`. Other model sizes (tiny, small, medium, large) can be specified as an argument.
- **Account + profile caches (paid)** — `~/.punch2pen/account.json` (bearer session, 0600) and `~/.punch2pen/profiles/<id>.json` (dictionary cache with pending-sync flags). Deleted on sign-out or when the API rejects the session.
- **Profile API URL** — compiled via `-DPUNCH2PEN_PROFILE_API_URL`, or `PUNCH2PEN_PROFILE_API` env, or `~/.punch2pen/profile-api.json`. Empty means free/lite only.
- **Plugin state** — `transcriptionMode` (offline/online) and `bpm` saved via ValueTree XML serialization in `getStateInformation` / `setStateInformation`. Restored by the DAW host on plugin reload.
