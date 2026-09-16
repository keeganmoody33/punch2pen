# punch2pen verification map

This directory is the maintained source for verifying user-facing punch2pen behavior. Read this index before driving, then use the matching feature file.

## Baseline preconditions

- Run `control-punch2pen` from the **repository root**.
- Prefer an instance **this run launched**. Never drive a leftover engine on `127.0.0.1:7483`.
- Port **7483 is exclusive**. Refuse a second launch on the same host.
- Local whisper only unless the user explicitly asked for `--cloud`.
- Isolated engine `HOME` is `/tmp/punch2pen-verify-<run-id>/home`. Do not write corrections into the user's `~/.punch2pen` during verify.
- On Darwin, AU/VST3 identity remains `aufx` / `P2pn` / `Dcta`. Do not rename.
- On non-Darwin, engine IPC may still be driven; **do not report Mac plugin or DAW proof**.
- Put `control-punch2pen` on your command line as `.cursor/skills/verify-punch2pen/scripts/control-punch2pen`.
- Run `control-punch2pen doctor` and require a pass before a mutating drive.

## Driving conventions

- Start every recipe from the baseline unless its preconditions say otherwise.
- Treat every command as literal.
- Engine/IPC actions go through `control-punch2pen launch|doctor|drive|cleanup`.
- DAW recording, karaoke click, and host transport are **manual on Mac**. There is no expect/Playwright/AudioPluginHost session in this tree.
- Restore fixture corrections after a mutation. Do not remove proof artifacts during cleanup.

## Proof and skip reporting

- Capture the user action and the resulting state, not only the final screen or log line.
- Engine proof includes command, stdout/stderr tail, exit code, and pid/port ownership.
- Mutation proof includes a second read of `corrections.csv` (or auval log).
- Record the feature ID and entry point with every artifact.
- Report an unreachable path with the attempted command and the unmet precondition.
- Do not report a skipped DAW entry point as verified through unit tests or sine-wave python.

## Feature entry contract

Each feature file starts with an H1 title and one paragraph describing the user-visible behavior. It then uses exactly four H2 sections in this order.

1. `Sub-features` lists short IDs with one line for each behavior.
2. `How to get to it (user POV)` lists every user entry point.
3. `Driving it with control-punch2pen` starts with `Preconditions:` and uses labeled bullets that pair each user action with an exact command and observable result.
4. `Gotchas` lists traps that can waste or invalidate a verification run.

Keep implementation details out of the map. Name only user paths, stable handles, required state, commands, and observable proof.

## Features

- [Engine ready](./engine-ready.md) covers local `punch2penEngine` start, loopback 7483, and doctor.
- [Correction IPC](./correction-ipc.md) covers the same Correction message the Studio Receipt Apply button sends.
- [Studio Receipt states](./studio-receipt-states.md) covers the five editor states and the static HTML contract (not DAW automation).
- [Plugin identity](./plugin-identity.md) covers AU/VST3 codes `aufx`/`P2pn`/`Dcta` and `auval`.
- [DAW record transcript](./daw-record-transcript.md) covers punch-in transcription in a real host — manual only.
