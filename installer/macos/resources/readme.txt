Punch2Pen 1.0.0 (unsigned macOS AU + VST3)

Identity (do not rename): manufacturer Dcta, plugin P2pn, AU type aufx.
Display name: Doctaaa: punch2pen. Bundle ID: com.doctaaa.punch2pen.

After installing (quit Logic first):
1. Postinstall downloads the local Whisper model to
   ~/.punch2pen/models/ggml-base.bin when it is missing. If you are
   offline, place that file yourself.
2. Confirm AU identity: auval -strict -v aufx P2pn Dcta
3. The installer starts punch2penEngine.app and registers a RunAtLoad
   LaunchAgent so 127.0.0.1:7483 is up before the editor opens. The
   plugin also auto-launches /Applications/Punch2Pen/punch2penEngine.app
   (then the nested Contents/Helpers copy) with `open -g -n` if the
   port is down. It does not launch leftover
   ~/punch2pen/bin/punch2penEngine from old builds. Engine logs go to
   ~/.punch2pen/engine.log; plugin launch attempts to
   ~/.punch2pen/plugin-ipc.log.
4. If the editor stays on WAITING FOR ENGINE CONNECTION, remove any
   leftover ~/punch2pen/bin/punch2penEngine, confirm
   ~/.punch2pen/models/ggml-base.bin exists, and right-click Open
   /Applications/Punch2Pen/punch2penEngine.app once if Gatekeeper still
   blocks the unsigned helper, then reopen the plugin.

This package is unsigned. Gatekeeper will warn until a Developer ID
signature and notarization exist. No prices, accounts, or vendor keys
are included.
