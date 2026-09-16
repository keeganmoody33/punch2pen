# Engine ready

Engine ready is the local `punch2penEngine` daemon listening on loopback so the Mac plugin (or an IPC client) can connect. Until it prints ready and owns port 7483, the editor stays on WAIT / disconnected.

## Sub-features

- `engine-build` produces `build/bin/punch2penEngine` from the repo CMake path.
- `engine-model` requires `ggml-base.bin` for local whisper (engine exits 1 if the model will not load).
- `engine-listen` binds `127.0.0.1:7483` and logs `Engine ready.`
- `engine-doctor` confirms this run's pid owns that port.
- `engine-isolate` uses a disposable HOME so verify does not share the user's `~/.punch2pen`.

## How to get to it (user POV)

- Run `./build/bin/punch2penEngine` in a terminal after `scripts/download_model.sh base`.
- Insert the punch2pen plugin in a DAW so `IPCClient` auto-launches `~/punch2pen/bin/punch2penEngine` (or `/Applications/Punch2Pen/punch2penEngine`) if 7483 is down.
- Run `./scripts/test_daw_integration.sh` (it starts a temporary engine unless `--skip-engine`).

## Driving it with control-punch2pen

Preconditions:

- Repository root is a punch2pen checkout at this skill's revision.
- Nothing is listening on `127.0.0.1:7483`.
- CMake ≥ 3.22 and a C++20 compiler are on `PATH`. On Linux, `g++` with libstdc++ is enough; a Clang `c++` that cannot find `<iostream>` is not.
- Local mode (no `--cloud`).

- **Launch isolated engine.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen launch`. Exit code `0`. Log contains `Mode: [LOCAL] whisper.cpp`, `IPC Server started on 127.0.0.1:7483`, and `Engine ready.`
- **Doctor.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen doctor`. Exit code `0`. Output names this run's pid as the listener on 7483 and the isolated HOME path.
- **Refuse a second instance.** Run `control-punch2pen launch` again while the first is up. Exit code non-zero. Message says port 7483 is already in use.
- **Proof.** Copy doctor stdout to `artifacts/<run-id>/doctor.txt` and the engine log tail to `artifacts/<run-id>/engine-ready.log`. Both identify punch2pen Engine v1.0.0 and 127.0.0.1:7483.

## Gotchas

- Missing `~/.punch2pen/models/ggml-base.bin` (in the **isolated** HOME) makes the process exit before `Engine ready.` Launch downloads or copies the model; do not assume the user's real home is used.
- Port 7483 is not configurable. A leftover engine, a DAW-spawned helper, or `test_daw_integration.sh` will block launch.
- `verify_setup.sh` only dry-runs CMake into `build_verify/`. It does not start the engine.
- A TCP connect to 7483 is a real engine client (`Client connected!` in the log). Doctor/launch prefer `lsof` so they do not spam accepts.
- Linux/CI can prove this feature without AU/VST3. That is not Mac plugin proof.
- Do not `killall punch2penEngine`. Cleanup uses the pidfile from this run.
