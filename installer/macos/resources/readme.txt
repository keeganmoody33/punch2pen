Punch2Pen 1.0.0 (unsigned macOS AU + VST3)

Identity (do not rename): manufacturer Dcta, plugin P2pn, AU type aufx.
Display name: Doctaaa: punch2pen. Bundle ID: com.doctaaa.punch2pen.

After installing:
1. Place the local Whisper model at ~/.punch2pen/models/ggml-base.bin.
   This installer does not download a model.
2. Confirm AU identity: auval -strict -v aufx P2pn Dcta
3. Insert punch2pen in a DAW. The plugin auto-launches
   /Applications/Punch2Pen/punch2penEngine if it is not already running.
   Engine stdout/stderr go to ~/.punch2pen/engine.log.

This package is unsigned. Gatekeeper will warn until a Developer ID
signature and notarization exist. No prices, accounts, or vendor keys
are included.
