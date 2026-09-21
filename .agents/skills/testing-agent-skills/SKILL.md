---
name: testing-agent-skills
description: Test project-installed agent skills and bundled shell guardrails. Use when a PR adds, updates, or reviews `.agents/skills`, `skills-lock.json`, or a skill hook script.
---

# Testing Agent Skills

Use this when a change adds or updates project skills under `.agents/skills`, especially when a lockfile or hook script is part of the change.

## Devin Secrets Needed

None for local skills/guardrails verification.

## Scope

Prefer shell-only testing unless the changed skill explicitly drives a browser or desktop UI. Do not record the desktop for shell-only tests; capture command output as evidence instead.

## Setup Checks

1. Confirm the PR branch and commit:
   ```bash
   git status --short --branch
   git rev-parse --short HEAD
   ```
2. Confirm the repo has Node/npm available before invoking the skills CLI:
   ```bash
   node --version
   npm --version
   ```
3. If `npx skills@latest` prompts to install the package, use `npx -y skills@latest ...` during testing so the command is non-interactive.

## Skills Install Assertions

Validate the lockfile and on-disk skills together. A good test should fail if either the generated files are incomplete or the CLI cannot discover the project skills.

Recommended assertions:

- `skills-lock.json` parses as JSON.
- `version === 1` when that is the current CLI lockfile format.
- `Object.keys(skills).length` matches the expected installed skill count.
- Every lock entry has the expected `source`, `sourceType`, non-empty `skillPath`, and a 64-character lowercase hex `computedHash`.
- The number of `.agents/skills/**/SKILL.md` files matches the lockfile count.
- Every `SKILL.md` starts with YAML frontmatter containing `name:` and `description:`.
- `npx -y skills@latest list --json` includes all expected skill names with `scope: "project"` when the CLI exposes scope.

## Guardrails Hook Assertions

For shell hook scripts that receive Claude hook JSON, test by piping JSON directly into the script:

```bash
printf '{"tool_input":{"command":"git checkout -- ."}}\n' | .agents/skills/git-guardrails-claude-code/scripts/block-dangerous-git.sh
```

For blocked commands, assert:

- Exit code is exactly `2`.
- stderr contains `BLOCKED`.
- stderr includes the command under test.

For safe commands, assert:

- Exit code is exactly `0`.
- stdout and stderr are empty.

High-value guardrails regression cases include:

- Canonical path reset forms such as `git checkout -- .` and `git restore -- .`.
- Git config flags before subcommands, such as `git -c user.name=x push`.
- Combined clean flags such as `git clean -dfx`.
- A safe baseline such as `git status` to catch overblocking.

## Reporting

In the PR comment and user report, include:

- Branch and commit tested.
- Exact pass/fail assertions.
- A short raw-output excerpt showing the final count and guardrails exit codes.
- CI status after runtime testing.

If there is no UI or preview deployment, state that explicitly instead of recording an idle desktop.
