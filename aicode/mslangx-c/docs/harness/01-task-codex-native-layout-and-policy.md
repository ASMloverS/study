# Task 01 - Codex Native Layout and Policy

## Status

Complete. Verified on 2026-04-25 with the acceptance commands in this
document.

## Goal

Create the Codex-native harness source layout and policy documentation without
introducing a separate registry, router, command runtime, or slash parser.

## Design Links

- Harness architecture baseline: [./codex-harness-design.md](./codex-harness-design.md)
- Status index: [./tasks.index.md](./tasks.index.md)

## Dependencies

None.

## Scope

1. Create `~/.codex/custom-harness/` as the canonical harness root.
2. Create canonical `agents/` and `skills/` directories under the harness root.
3. Add a concise harness `README.md` that states the runtime surfaces:
   `AGENTS.md`, Codex skills, and Codex custom subagents.
4. Document that `~/.codex/agents/`, `~/.agents/skills/`,
   `.codex/agents/`, and `.agents/skills/` are exposure or override points,
   not the canonical source.
5. Document that `~/.claude/` is read-only migration evidence and must not be
   modified.

## File Ownership

1. `C:\Users\asmlo\.codex\custom-harness\README.md`
2. `C:\Users\asmlo\.codex\custom-harness\agents\`
3. `C:\Users\asmlo\.codex\custom-harness\skills\`

## Planned Steps

1. Create the canonical harness root if it does not already exist.
2. Create empty canonical `agents/` and `skills/` directories.
3. Write `README.md` with the canonical layout, exposure rules, and excluded
   runtime mechanisms.
4. State explicitly that the harness must use native Codex discovery and custom
   subagent loading.
5. Confirm that no `~/.codex/harness/*.toml` registry, `~/.codex/commands/`
   command runtime, or `~/.claude/` mutation is introduced.

## Acceptance

1. `~/.codex/custom-harness/README.md` exists and identifies the canonical
   source tree.
2. The canonical `agents/` and `skills/` directories exist.
3. The README names native Codex skills and custom subagents as the runtime
   extension points.
4. The README explicitly excludes a custom registry runtime, command runtime,
   required slash parser, and writes to `~/.claude/`.
5. The task is complete only when all edited files are UTF-8, LF, and free of
   trailing whitespace.

## Acceptance Commands

```powershell
Get-Content C:\Users\asmlo\.codex\custom-harness\README.md
Test-Path C:\Users\asmlo\.codex\custom-harness\agents
Test-Path C:\Users\asmlo\.codex\custom-harness\skills
```

## Out of Scope

1. Writing subagent TOML files.
2. Migrating skill packages.
3. Creating the dispatch compatibility skill.
4. Generating the capability index.
