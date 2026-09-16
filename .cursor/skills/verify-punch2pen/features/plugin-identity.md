# Plugin identity

Plugin identity is how Mac hosts register punch2pen: Audio Unit `aufx` / `P2pn` / `Dcta` and VST3 product name `punch2pen`. Verification reads those codes and, on macOS, may run `auval`. It never renames them.

## Sub-features

- `identity-cmake` confirms `plugin/CMakeLists.txt` still has manufacturer `Dcta`, code `P2pn`, formats `VST3 AU`, product `punch2pen`.
- `identity-auval` runs `auval -v aufx P2pn Dcta` when the AU is installed on Darwin.
- `identity-bundles` locates built `punch2pen.vst3` and `punch2pen.component` under `build/` on Darwin.
- `identity-company` notes `COMPANY_NAME "Doctaaa"` as present; do not “fix” it during verify.

## How to get to it (user POV)

- Scan plugins in Logic Pro (AU) or REAPER/Ableton (VST3) for **punch2pen**.
- Run `auval -v aufx P2pn Dcta` in Terminal after installing the AU.
- Run `./scripts/test_daw_integration.sh --install-plugin` then the same `auval` line (the readiness script already calls it when `auval` exists).

## Driving it with control-punch2pen

Preconditions:

- Working tree is the punch2pen repo.
- You will not edit `PLUGIN_MANUFACTURER_CODE`, `PLUGIN_CODE`, or AU type `aufx`.
- Windows VST3 is out of scope.

- **CMake identity.** Run `.cursor/skills/verify-punch2pen/scripts/control-punch2pen drive identity`. Exit code `0` on a tree that still has `PLUGIN_MANUFACTURER_CODE "Dcta"` and `PLUGIN_CODE "P2pn"` and `FORMATS VST3 AU`.
- **Bundles (Darwin).** Same command reports paths to `punch2pen.vst3` and, if present, `punch2pen.component` under `build/`. Missing AU is a warning if VST3 exists; missing both on Darwin is a fail. On Linux the command records `skipped (not Darwin)` for bundles and still passes CMake identity.
- **auval (Darwin, AU installed).** If `auval` exists and `~/Library/Audio/Plug-Ins/Components/punch2pen.component` is present, the helper runs `auval -v aufx P2pn Dcta` and saves the log. Failure of auval is a fail of this feature, not a cue to change codes.
- **Proof.** Save stdout to `artifacts/<run-id>/plugin-identity.txt`. Feature ID `plugin-identity`. Quote `aufx P2pn Dcta` in the artifact.

## Gotchas

- Changing Dcta / P2pn / aufx breaks existing sessions and this map. If identity fails, report the mismatch; do not patch CMake as part of verify.
- `installer/macos/build_pkg.sh` still uses `com.doctaaa.punch2pen.*`. That is adjacent identity, not an invitation to rename.
- `auval` cache can be stale. The readiness script's manual checklist mentions `AudioComponentRegistrar` and `com.apple.audiounits.cache`. Do not delete caches unless auval is the target of a Mac session and doctor already passed.
- Doctaaa company name vs punch2pen product name: both appear; neither is Windows work.
