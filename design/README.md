# Punch2Pen design system

One system for the DAW plugin (AU/VST3 Living Transcript) and punch2pen.com. Built as Paper-ready HTML so it can go straight onto a Paper Desktop canvas. See [PAPER.md](PAPER.md).

![Cover](renders/01-cover.webp)

## Direction (decided 2026-09-30, rebranded the same day)

**The founder's 2PEN logo leads: brick red and a legal-yellow pad, on the dark graphite chrome the plugin ships today.**

The first pass used a marker green sampled from the founder's sketches. When the finished logo arrived (`brand/source/logo-legal-pad.webp`), the system was re-tokened to it. The green is retired; the graphite chrome, type, spacing, components and flows carry over unchanged.

| Where | Before | Now |
|---|---|---|
| Plugin UI (`#21`) | "Open": graphite, teal `#7AD4C0` | Shipped in `plugin/Source/ui/public/index.html`: graphite kept, red only for REC and the red pen, live states white |
| Site + `plugin/Source/ui/HANDOFF.md` (`#32`) | "Booth paper": warm black, vermillion `#E4452F`, serif lyrics | Shipped in `site/public/site.css` (Archivo headlines, brand-red fills, tier cards on the Pad). HANDOFF's token table updated |

The brand red is `#AC2623`, picked from the logo; the median of the logo file's 84,398 red pixels is `#AF2724`. It is a fill with white type (6.9:1), the way the 2 badge is drawn, and brand ink on the legal pad (6.0:1). As text on graphite it lifts to `#EA7E7B` (5.7:1 on chrome). The pad is `#F9F2BC` with `#A7D0D2` rules. Every text pair passes WCAG AA; the one deliberate exception is past lyric lines at 0.38 opacity (frame 02 has the table).

The rules that fall out of it:

- **REC is recording.** `#FF453A` colors the recording state and nothing else: the REC badge (label and pulsing dot), the header line while recording, the stream cursor, the R key hint. Brighter and more orange than the brand red, and never used outside recording.
- **Red is the brand and the pen.** On the site: the brand fill, one primary red button per surface. In the plugin: only corrections (the red pen) and the caret. A red button or toggle in a DAW reads as record-arm, so the plugin's primary button, toggles and badge are white.
- **White is live:** PLAY, LIVE, synced, the playhead, focus rings.
- **Yellow is the highlighter:** legal yellow over words on graphite for the seek flash and dictionary words.
- **Amber is waiting:** WAIT, the engine banner, pending sync.
- **Ink is the plate:** the word under the playhead is an ink plate, never a color fill.
- **Mono is the punch** (clock, labels), **sans is the words**, **Archivo is the poster** (site and brand only; the DAW loads no webfonts).
- **The Pad** is the legal pad from the logo: yellow stock, blue rules, pen-black ink, one red header rule. It's where words land when they leave the booth.
- **Shaders are Paper's own** ([Paper Shaders](https://shaders.paper.design), Apache-2.0): one moving shader per screen, token colors only, never REC red, brand red only on the site, and nothing moves in the plugin while music plays.

## What's here

```
design/
├── PAPER.md                 how to put this on the Paper canvas
├── tokens/
│   ├── tokens.json          source of truth (W3C design-token format)
│   ├── build.mjs            → tokens.css + contrast report
│   └── tokens.css           generated
├── brand/
│   ├── source/              the founder's logo file (the trace source)
│   ├── fonts/               Archivo (SIL OFL), pinned by sha256 for the wordmark
│   ├── trace_logo.py        logo → fist-paths.json (vector layers + the 2 badge)
│   ├── build_marks.py       every mark from the trace and one geometry file
│   ├── *.svg                fist, pen, two, bolt, wordmark (-booth / -pad / -plugin), favicon
│   └── explore/             proposed marks: punch range, transient, seek word
├── components/
│   ├── plugin.css           Living Transcript parts (p-*)
│   ├── site.css             punch2pen.com parts (s-*)
│   ├── pad.css              the legal-pad surface
│   └── shaders.css          shader layers and their fallbacks (fx, fx-mark)
├── shaders/
│   ├── src/                 runtime (no React) + brand presets from tokens.json
│   ├── build.mjs            → p2p-shaders.js, one offline ESM file (111 KB, 44 KB gzip)
│   ├── p2p-shaders.js       vendored build: Paper Shaders 0.0.81, six shaders
│   └── NOTICE, LICENSE-paper-shaders.txt
├── frames/                  one HTML file per Paper artboard
├── renders/                 WebP of every frame (for review and Paper comparison)
└── tools/render.mjs         re-measure frames and re-render
```

| Frame | Artboard | Status |
|---|---|---|
| 01 | Cover | |
| 02 | Color | |
| 03 | Type | |
| 04 | Space, radius, motion | |
| 05 | Marks: the traced logo, badge, plugin marks, explorations | New; explorations proposed |
| 06 | Shaders: what's new at Paper, seven presets, plugin smoke, budget | Proposed |
| 10 | Plugin anatomy | Shipped behavior, proposed parts marked |
| 11 | Plugin components | Shipped behavior, proposed parts marked |
| 12 | Plugin states (6) | Shipped behavior |
| 13 | Plugin flows: correct, sign in, click to jump | Correct and sign in shipped; jump proposed |
| 14 | Plugin proposed: Flow Grid, Dictionary, Pen it + lyric sheet | Proposed |
| 20 | Site components | Restyle of shipped site |
| 21 | Site home, 1440 | Restyle, real copy |
| 22 | Site home, 390 | Restyle, real copy |
| 23 | Site routes and funnel | Real routes and PostHog events |

## Regenerate

```bash
node design/tokens/build.mjs            # tokens.css + contrast report
pip install fonttools brotli pillow numpy potracer   # once
python3 design/brand/trace_logo.py      # only when the logo file changes
python3 design/brand/build_marks.py     # every SVG in design/brand/
(cd design/shaders && npm ci && node build.mjs)   # p2p-shaders.js from src/ + tokens.json
node design/tools/render.mjs            # needs playwright; serves design/ over HTTP; then PNG → renders/*.webp
```

## Mapping to shipped code

The shipped editor (`plugin/Source/ui/public/index.html`) binds to IDs. The semantic token names (`--bg-deep`, `--bg-pane`, `--bg-chrome`, `--bg-raised`, `--ink-*`, `--accent`, `--rec`, `--wait`, `--play`, `--past`, `--upcoming`, `--lyric-size`, `--title-size`, `--mono`, `--sans`) are the same names the plugin and `site/public/site.css` already use, so adopting this is mostly a value swap.

| Design class | Shipped element |
|---|---|
| `.p-header` · `.p-wordmark` · `.p-pill` · `.p-badge` | `#header-bar` · `#app-title` · `#profile-pill` · `#status-badge` |
| `.p-transport` · `.p-sync` | `#position-display` · `#sync-indicator` |
| `.p-banner` | `#connection-banner` |
| `.p-pane` · `.p-line` · `.p-word` | `#transcript-container` · `.lyric-line` · `.lyric-word` |
| `.p-rtl` | `#return-to-live` |
| `.p-sheet` | `#correction-overlay` |
| `.p-pop` · `.p-row` | `#profile-popover` · `.profile-row` / `.seat-row` |
| `.p-status` · `.p-logo` | `#status-bar` · `#logo-mark` |

## Restyle (shipped)

Restyle only. The C++ ↔ JS contract, IDs, states, and `Dcta / P2pn / aufx` stay.

- Accent teal → white for live states and the red pen for corrections; `--ink-3` 0.48 → 0.54 so chrome labels clear 4.5:1.
- Wordmark is an outlined SVG with the one-color 2 badge; `two-plugin.svg` fills the `#logo-mark` placeholder (HANDOFF follow-up 1).
- Corrected words take a red-pen underline; dictionary words and the seek flash take the yellow highlighter.
- The struck-out word in the correction sheet is grey, not red.
- The free-tier nudge in the status bar wraps to two lines instead of truncating at 400 px.
- Mobile site: the download button becomes "Copy download link", since the pkg is Mac-only. **Not built yet** (it is a `site.js` behavior change, not a restyle).

Proposed features, drawn but not built:

- **Click a word to jump the playhead** (frame 13). A standard AU/VST3 insert can read the host clock but can't move it. ARA 2's playback controller (`requestSetPlaybackPosition`) can. Without ARA 2 the click shows the exact bar, beat, and time with a copy button. Correcting moves to double-click or Return.
- **Syllable fill and the Flow Grid** (frames 11, 14). They need syllable onsets from the engine; today the engine returns word start and end.
- **Dictionary panel** with word, adlib, and phrase entries (frame 14).
- **Pen it**: copy, .txt, PDF lyric sheet on the Pad, share link for signed-in profiles (frame 14).
- **Shaders** (frame 06): Gem Smoke on the 2 in the plugin's WAIT (amber) and empty IDLE (white) states, disposed when the transport moves or words arrive; Paper Texture 2.0 under the Pad (tier cards, lyric sheet); Grain Gradient behind the site hero; Heatmap on the fist in a closing band (red to white-hot); Halftone Dots for share cards (black on legal yellow); Lens Distortion on the 404.

## Open threads

1. **Paper import.** You run [PAPER.md](PAPER.md) on your Mac.
2. **ARA 2 or not.** Click-to-jump depends on it. This is an engineering call before it's a design call.
3. **Syllable timing.** Check what the transcription path can return before building the grid.
4. **Restyle shipped.** Tokens, outlined wordmark and 2 badge are in `plugin/Source/ui/public/index.html`; `site/public/site.css` and every page header carry the 2PEN system; `HANDOFF.md` is current. Left: the mobile "Copy download link" behavior, and a look inside Logic's WKWebView.
5. **Activation is unmeasured.** The site sends six PostHog events. The plugin, engine, and profile API send none, so install, first punch, first correction, and sign-in are invisible (frame 23). Any instrumentation needs an opt-in the free tier can keep honest.
6. **Shaders in the real plugin and site.** The site needs one module script and the data attributes. The plugin has to embed `p2p-shaders.js` in the WebView (it loads nothing remote), and the C++ bridge has to dispose the smoke when the transport moves. WKWebView has WebGL2 on current macOS (Safari 15 and later); confirm it inside Logic on the target Macs.
7. **Remote MCP.** Paper lists it as coming soon. Once it ships, a cloud session could build the canvas itself instead of handing you a prompt.
8. **Logo master file.** The fist is traced from a 1969 × 2000 WebP. A larger master in `brand/source/` plus `trace_logo.py` gives sharper print curves.
9. **No red in the plugin chrome** is a proposal, not a given: the plugin's buttons, toggles and badge go white so red never reads as record-arm. Reverse it by pointing `.p-btn.primary` back at `--accent-fill`.
10. **Explorations** (frame 05): Punch range [2], Transient, Seek word. Adopt, iterate, or kill each.

Example data in the frames (the profile "Nova", "Room 4", take numbers, dictionary counts) is made up and labelled as such on each board. Lyrics are the original demo verse from the plugin's preview mode.
