Punch2Pen 1.0.0 (unsigned macOS AU + VST3)

Identity (do not rename): manufacturer Dcta, plugin P2pn, AU type aufx.
Display name: Doctaaa: punch2pen. Bundle ID: com.doctaaa.punch2pen.

After installing:
1. Place the local Whisper model at ~/.punch2pen/models/ggml-base.bin.
   This installer does not download a model.
2. Confirm AU identity: auval -strict -v aufx P2pn Dcta
3. Insert punch2pen in a DAW. The plugin launches
   punch2penEngine.app (nested in the AU/VST3 bundle, and also at
   /Applications/Punch2Pen/punch2penEngine.app) via Launch Services so
   Logic's AU sandbox does not swallow the helper. It does not launch
   leftover ~/punch2pen/bin/punch2penEngine from old builds. Engine logs
   go to ~/.punch2pen/engine.log; plugin launch attempts to
   ~/.punch2pen/plugin-ipc.log.
4. If the editor stays on WAITING FOR ENGINE CONNECTION, remove any
   leftover ~/punch2pen/bin/punch2penEngine, right-click Open
   /Applications/Punch2Pen/punch2penEngine.app once to clear Gatekeeper,
   then reopen the plugin.

This package is unsigned. Gatekeeper will warn until a Developer ID
signature and notarization exist. No prices, accounts, or vendor keys
are included.
