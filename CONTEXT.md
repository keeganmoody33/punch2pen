# Domain Glossary

This document outlines the core domain concepts of the `punch2pen` project.

- **Engine** — The standalone background C++ process that manages the whisper.cpp model, runs transcription inference, and persists user vocabulary and corrections.
- **Plugin** — The DAW audio plugin (built with JUCE) that runs inside the host, captures raw audio during recording, and displays the transcription.
- **Audio Chunk** — A contiguous block of audio samples captured by the Plugin and streamed to the Engine for transcription.
- **Correction** — A user-provided pair mapping mis-transcribed text to the intended correct text (e.g., "spoke" -> "corrected"). Used to update the vocabulary.
- **Vocabulary** — The set of unique words extracted from the user's Corrections, used to dynamically bias the transcription engine.
- **Initial Prompt** — The formatted string composed of active Vocabulary words, passed to whisper.cpp to improve the accuracy of subsequent transcriptions.
