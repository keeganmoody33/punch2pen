# Put the design system into Paper

Paper Desktop's MCP server runs only on your own computer: the `paper mcp` CLI, or `http://127.0.0.1:29979/mcp` while a file is open ([Paper MCP docs](https://paper.design/docs/mcp)). A cloud session can't reach it. So this system was built as Paper-ready HTML in `design/frames/`: one file per artboard, fixed width, flex layout. Paper's canvas is HTML and CSS, so an agent on your Mac can rebuild each file on the canvas without translating anything.

## 1. Connect Claude Code to Paper (once)

1. Install and open [Paper Desktop](https://paper.design/downloads). Create a new file named `Punch2Pen Design System`.
2. In Paper, open the MCP panel (top-right callout, or the right panel with nothing selected) and click **Add to Claude**. That installs Paper for the Claude app and Claude Code.
   Manual route, from the same docs:
   ```bash
   claude plugin marketplace add paper-design/agent-plugins && claude plugin install paper-desktop@paper
   ```
3. Restart Claude Code and run `/mcp`. Paper should be listed.

## 2. Get the files on your Mac

```bash
cd punch2pen
git fetch origin claude/admiring-ride-xmdgph
git checkout claude/admiring-ride-xmdgph
```

## 3. Paste this into Claude Code, in the repo, with the Paper file open

```text
Build the Punch2Pen design system in the Paper file that is open right now.

Source of truth: the HTML files in design/frames/ (skip files starting with "_").
Each file is one artboard. Its <meta name="paper:artboard"> tag gives the artboard
name, data-width and data-height.

1. First, create Paper color tokens from the "semantic" group in
   design/tokens/tokens.json (bg-deep, bg-pane, bg-chrome, bg-raised, ink, ink-2,
   ink-3, ink-4, accent, accent-ink, rec, wait, play, marker, pad-*). Resolve
   aliases to hex.
2. Then build artboards one at a time in file-name order (01, 02 … 23). For each:
   - Create an artboard with the meta name and size.
   - Rebuild the page with the same structure and flex layouts. Resolve CSS
     variables from design/tokens/tokens.css and the classes from
     design/components/*.css and design/frames/_board.css.
   - Place the SVGs from design/brand/ as vectors, not bitmaps.
   - Display text is Archivo from Google Fonts at width 68, weight 850. UI text is
     the system sans (SF Pro) and mono (SF Mono).
   - Compare your artboard with design/renders/<same name>.webp and fix any
     differences before starting the next one.
3. Lay artboards out left to right in three rows, 200 px apart:
   row 1: 01–05 (foundations and brand), row 2: 10–14 (plugin), row 3: 20–23 (site).
4. Stop after each row and tell me which artboards are done.
```

## If it stalls

- Paper's docs say long agent sessions are the most common cause of trouble. Restart Claude Code and say "continue from artboard NN".
- Paper's docs also mention MCP usage limits on some plans. Fourteen artboards is a lot of tool calls. If you hit the limit, the rows are independent, so finish the rest in another session.
- If an artboard drifts from its render, point the agent at the one element that differs. Don't regenerate the whole artboard.

## After it's in Paper

Paper becomes where the visuals get iterated. If you change a token or a mark in Paper, bring it back to `design/tokens/tokens.json` or `design/brand/build_marks.py` so the plugin and site build from the same source. The Paper MCP reads selections too, so "update tokens.json to match the colors on this artboard" works from the same session.
