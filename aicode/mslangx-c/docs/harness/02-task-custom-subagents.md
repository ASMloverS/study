# Task 02 - Custom Subagents

## Status

Complete. Verified on 2026-04-25 with the acceptance commands in this
document and `python tests/test_harness_task02_subagents.py`.

## Goal

Create the initial Codex custom subagents as real `.toml` agent definitions in
the canonical harness source tree.

## Design Links

- Harness architecture baseline: [./codex-harness-design.md](./codex-harness-design.md)
- Status index: [./tasks.index.md](./tasks.index.md)

## Dependencies

1. Task 01

## Scope

1. Create active subagents in `~/.codex/custom-harness/agents/`.
2. Use Codex-supported custom subagent fields only.
3. Use hyphenated names that match the design baseline.
4. Make review and audit roles read-only.
5. Make write roles describe their write scope and verification expectations.
6. Defer `doc-designer` and `doc-corrector` unless they gain behavior distinct
   from `doc-fixer`, `doc-reviewer`, or normal planning.

## File Ownership

1. `C:\Users\asmlo\.codex\custom-harness\agents\bug-fixer.toml`
2. `C:\Users\asmlo\.codex\custom-harness\agents\code-implementer.toml`
3. `C:\Users\asmlo\.codex\custom-harness\agents\code-reviewer.toml`
4. `C:\Users\asmlo\.codex\custom-harness\agents\doc-fixer.toml`
5. `C:\Users\asmlo\.codex\custom-harness\agents\doc-reviewer.toml`
6. `C:\Users\asmlo\.codex\custom-harness\agents\git-committer.toml`

## Field Contract

1. Every active subagent must define `name`, `description`, and
   `developer_instructions`.
2. Reviewers and auditors must set `sandbox_mode = "read-only"`.
3. Write-capable agents must set `sandbox_mode = "workspace-write"` only when
   they are expected to edit files.
4. Optional `model`, `model_reasoning_effort`, and `nickname_candidates` may be
   used only when they add clear value.
5. Do not use unsupported legacy fields such as `entrypoint`, `invoke_mode`,
   or `output_contract`.

## Planned Steps

1. Write `code-reviewer.toml` as a read-only reviewer that leads with findings
   and does not edit files.
2. Write `code-implementer.toml` as a scoped implementation worker that edits
   only the assigned files and reports verification.
3. Write `bug-fixer.toml` as a targeted debugging worker that reproduces or
   localizes failures before changing code.
4. Write `doc-reviewer.toml` as a read-only documentation auditor.
5. Write `doc-fixer.toml` as a documentation-only editing worker.
6. Write `git-committer.toml` as an explicit-only commit preparation agent
   that respects scoped staging and repository policy.
7. Confirm that the hyphenated names are the documented runtime identities.

## Acceptance

1. Each active subagent file is valid TOML.
2. Each active subagent uses the Codex custom subagent schema required by the
   design.
3. Read-only roles cannot write files.
4. Write roles state their allowed scope and verification expectations.
5. No active subagent unintentionally shadows a built-in agent name.
6. The task is complete only when all edited files are UTF-8, LF, and free of
   trailing whitespace.

## Acceptance Commands

```powershell
Get-Content C:\Users\asmlo\.codex\custom-harness\agents\code-reviewer.toml
Get-Content C:\Users\asmlo\.codex\custom-harness\agents\code-implementer.toml
Get-Content C:\Users\asmlo\.codex\custom-harness\agents\git-committer.toml
```

## Out of Scope

1. Skill package migration.
2. Dispatch compatibility behavior.
3. Runtime exposure symlinks or install steps.
4. Capability index generation.
