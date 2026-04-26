# Task 04 - Runtime Exposure and Capability Index

## Status

Complete. Verified on 2026-04-26 with the acceptance commands in this
document and `uv run python tests\test_harness_task04_runtime_exposure.py`.

## Goal

Expose canonical harness assets to Codex runtime discovery only where needed
and generate a human-readable capability index that is not a runtime router.

## Design Links

- Harness architecture baseline: [./codex-harness-design.md](./codex-harness-design.md)
- Status index: [./tasks.index.md](./tasks.index.md)

## Dependencies

1. Task 01
2. Task 02
3. Task 03

## Scope

1. Expose custom subagents from `~/.codex/custom-harness/agents/` to
   `~/.codex/agents/` only when Codex runtime discovery requires it.
2. Expose skills to native Codex discovery locations only when required.
3. Use symlinks only when they preserve one canonical behavior source.
4. Add bounded `[agents]` settings when custom harness subagents are exposed.
5. Generate `~/.codex/custom-harness/capability-index.generated.md` for human
   audit only.

## File Ownership

1. `C:\Users\asmlo\.codex\custom-harness\agents\code-implementer.toml`
2. `C:\Users\asmlo\.codex\custom-harness\agents\code-reviewer.toml`
3. `C:\Users\asmlo\.codex\custom-harness\agents\bug-fixer.toml`
4. `C:\Users\asmlo\.codex\custom-harness\agents\doc-reviewer.toml`
5. `C:\Users\asmlo\.codex\custom-harness\agents\doc-fixer.toml`
6. `C:\Users\asmlo\.codex\custom-harness\agents\git-committer.toml`
7. `C:\Users\asmlo\.codex\config.toml`
8. `C:\Users\asmlo\.codex\custom-harness\capability-index.generated.md`

## Capability Index Contract

1. The capability index is generated documentation only.
2. The capability index must not be used as a registry, router, or source of
   truth for runtime behavior.
3. Each entry should include `kind`, `name`, `scope`, `source_path`,
   `trigger`, `status`, and `verified_by`.
4. Active, deferred, and legacy-reference entries should be distinguishable.
5. The index should point back to canonical skill and subagent files rather
   than duplicating behavior text.

## Planned Steps

1. Check whether Codex discovers custom subagents directly from
   `~/.codex/custom-harness/agents/`.
2. If native exposure is required, create symlinks from `~/.codex/agents/` to
   canonical subagent TOML files.
3. Check whether Codex discovers canonical custom harness skills directly.
4. If skill exposure is required, create symlinks or an install step to a
   native discovery location while keeping `~/.codex/custom-harness/skills/`
   canonical.
5. Add bounded `[agents]` settings with `max_threads = 6` and
   `max_depth = 1` when custom subagents are exposed.
6. Generate the capability index from the actual files created in Tasks 01
   through 03.

## Acceptance

1. Runtime exposure points are symlinks, hardlinks, or documented install
   outputs that preserve a single canonical behavior source.
2. No exposure point becomes a second canonical copy of a skill or subagent.
3. `[agents]` settings are bounded when custom subagents are exposed.
4. The capability index lists active skills, active subagents, deferred items,
   and legacy-reference exclusions.
5. The capability index states that it is documentation only.
6. The task is complete only when all edited files are UTF-8, LF, and free of
   trailing whitespace.

## Acceptance Commands

```powershell
Get-Content C:\Users\asmlo\.codex\custom-harness\capability-index.generated.md
Get-Content C:\Users\asmlo\.codex\config.toml
Get-Item C:\Users\asmlo\.codex\custom-harness\agents\code-reviewer.toml
Get-Item C:\Users\asmlo\.codex\agents\code-reviewer.toml
uv run python tests\test_harness_task04_runtime_exposure.py
```

## Out of Scope

1. Changing canonical skill or subagent behavior.
2. Creating a custom registry under `~/.codex/harness/`.
3. Creating a command runtime under `~/.codex/commands/`.
4. Treating the capability index as runtime metadata.
