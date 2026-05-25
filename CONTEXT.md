# Domain Glossary

This document outlines the core domain concepts of the `punch2pen` project.

- **Engine** — The standalone background C++ process that manages the whisper.cpp model, runs transcription inference, and persists user vocabulary and corrections.
- **Plugin** — The DAW audio plugin (built with JUCE) that runs inside the host, captures raw audio during recording, and displays the transcription.
- **Audio Chunk** — A contiguous block of audio samples captured by the Plugin and streamed to the Engine for transcription.
- **Correction** — A user-provided pair mapping mis-transcribed text to the intended correct text (e.g., "spoke" → "corrected"). Used to update the vocabulary.
- **Vocabulary** — The set of unique words extracted from the user's Corrections, used to dynamically bias the transcription engine.
- **Initial Prompt** — The formatted string composed of active Vocabulary words, passed to whisper.cpp to improve the accuracy of subsequent transcriptions.

## System Components

- **IPCClient** — JUCE Thread in the plugin managing the TCP connection to the engine. Drains the AudioRingBuffer, sends AudioChunk/TransportStop/Correction messages, and dispatches incoming TranscriptionResult events to listeners (`plugin/Source/IPCClient.h`).
- **IPCServer** — TCP server in the engine accepting plugin connections on port 7483. Queues incoming audio, transport-stop events, and corrections for the coordinator to consume (`engine/src/IPCServer.h`).
- **IPCServerInterface** — Abstract interface decoupling TranscriptionCoordinator from the concrete IPCServer, enabling test doubles (`engine/src/IPCServerInterface.h`).
- **Protocol** — Shared binary wire format header (`shared/Protocol.h`) defining MessageTypes: `AudioChunk`, `TranscriptionResult`, `Handshake`, `HandshakeResponse`, `Correction`, `TransportStop`.
- **AudioRingBuffer** — SPSC lock-free ring buffer backed by `juce::AbstractFifo` for transferring float audio samples from the real-time audio thread to the IPC thread (`plugin/Source/RingBuffer.h`).
- **Transcriber** — Local whisper.cpp inference wrapper implementing TranscriberInterface (`engine/src/Transcriber.h`).
- **OpenAICloudTranscriber** — Cloud transcription via OpenAI Realtime WebSocket API implementing TranscriberInterface. Resamples from 48 kHz to 16 kHz and sends base64-encoded PCM (`engine/src/OpenAICloudTranscriber.h`).
- **TranscriberInterface** — Polymorphic base class for transcription backends, defining `pushAudioBlock`, `finalizeStream`, `setVocabularyBias`, and listener management (`engine/src/TranscriberInterface.h`).
- **TranscriptionCoordinator** — Engine main loop: polls IPCServer for audio, forwards to the active transcriber, handles transport-stop finalization, processes corrections through DatabaseManager, and refreshes vocabulary bias (`engine/src/TranscriptionCoordinator.h`).
- **DatabaseManager** — CSV-based persistence of corrections at `~/.punch2pen/corrections.csv`. Extracts vocabulary words from corrected text for initial_prompt bias (`engine/src/DatabaseManager.h`).
- **TranscriptView** — Plugin UI component with VBlank-synced scrolling, word-level alpha highlighting (karaoke-style), and mouseDown click detection for correction triggering (`plugin/Source/TranscriptView.h`).
- **CorrectionEditor** — Popup UI component shown on word click. Displays the original word, accepts a corrected replacement, and submits the correction via IPCClient (`plugin/Source/CorrectionEditor.h`).
- **PositionDisplay** — Plugin UI component showing the current DAW transport position as bar/beat (`plugin/Source/PositionDisplay.h`).
- **ProfileManager** — (Stub) Planned user profile persistence with per-user vocabulary and corrections. Methods exist but file I/O is not yet implemented (`engine/src/ProfileManager.h`).

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

- **Corrections CSV** — `~/.punch2pen/corrections.csv` — simple `original,corrected` format, one pair per line. Loaded on engine startup, appended on each new correction.
- **Whisper model** — `~/.punch2pen/models/ggml-base.bin` — downloaded via `scripts/download_model.sh`. Other model sizes (tiny, small, medium, large) can be specified as an argument.
- **Plugin state** — `transcriptionMode` (offline/online) and `bpm` saved via ValueTree XML serialization in `getStateInformation` / `setStateInformation`. Restored by the DAW host on plugin reload.
