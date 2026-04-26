# Task 05 - Harness Validation and Release Gate

## Status

Complete. Verified on 2026-04-26 with the acceptance commands in this
document and `uv run python tests\test_harness_task05_validation_gate.py`.

## Goal

Validate the Codex-native harness end to end and define the release gate for
future harness changes.

## Design Links

- Harness architecture baseline: [./codex-harness-design.md](./codex-harness-design.md)
- Status index: [./tasks.index.md](./tasks.index.md)

## Dependencies

1. Task 01
2. Task 02
3. Task 03
4. Task 04

## Scope

1. Validate active skill package structure.
2. Validate active custom subagent TOML files.
3. Validate bounded Codex agent configuration.
4. Validate dispatch compatibility behavior.
5. Validate negative scenarios from the design baseline.
6. Validate that excluded mechanisms were not introduced.

## Static Validation

1. Every active skill has `SKILL.md`.
2. Every active `SKILL.md` has frontmatter `name` and `description`.
3. No duplicate skill names exist in the same scope unless documented.
4. `agents/openai.yaml` is valid YAML when present.
5. Heavy workflow skills disable implicit invocation when they include
   interface metadata.
6. Every active subagent file is valid TOML.
7. Every active subagent has `name`, `description`, and
   `developer_instructions`.
8. Review and audit agents use `sandbox_mode = "read-only"`.
9. Write agents document write scope and verification expectations.
10. No custom subagent unintentionally shadows built-in roles.
11. `max_depth` remains `1` unless explicitly justified.

## Behavioral Validation

1. `$dispatch agent:code-reviewer review harness docs` maps to the
   `code-reviewer` subagent and does not look for a custom registry route.
2. `$dispatch skill:doc-sync check harness docs` maps to the canonical
   `doc-sync` skill and does not require a custom registry route.
3. `$dispatch code-reviewer review harness docs` resolves through the registry
   map and maps to the `code-reviewer` subagent.
4. `$dispatch doc-sync check harness docs` resolves through the registry map
   and maps to the canonical `doc-sync` skill.
5. `$dispatch dev-flow` reports that `dev-flow` is not migrated.
6. `$dispatch git-commit` reports that `git-commit` is not migrated.
7. `code-reviewer` reviews without editing files.
8. `code-implementer` edits only its assigned scope and reports verification.
9. `bug-fixer` reproduces or localizes a failure before changing code.
10. `doc-fixer` edits documentation only.
11. Unknown dispatch targets stop with valid choices.
12. Ambiguous legacy names stop and ask for a type-qualified form.
13. Commit behavior stops unless commit intent is explicit.
14. Missing skill or subagent files are reported as configuration drift.

## Planned Steps

1. Write and run static checks for active skill packages.
2. Write and run static checks for active subagent TOML files.
3. Write and run config checks for bounded `[agents]` settings.
4. Exercise the dispatch compatibility scenarios.
5. Exercise the subagent behavior scenarios with scoped prompts.
6. Exercise negative scenarios for unknown names, ambiguous names, missing
   files, Plan Mode mutation, and accidental commit requests.
7. Confirm that `~/.codex/harness/`, `~/.codex/commands/`, and `~/.claude/`
   were not used as runtime mutation targets.

## Acceptance

1. Static validation passes for all active skills, subagents, and config.
2. Behavioral validation passes for dispatch and subagent scenarios.
3. Negative scenarios stop with explicit, actionable failures.
4. Excluded mechanisms are absent from the runtime implementation.
5. The generated capability index reflects the final active, deferred, and
   legacy-reference state.
6. The task is complete only when all edited files are UTF-8, LF, and free of
   trailing whitespace.

## Acceptance Commands

```powershell
Get-ChildItem C:\Users\asmlo\.codex\custom-harness -Recurse
Get-Content C:\Users\asmlo\.codex\custom-harness\registry.yaml
Get-Content C:\Users\asmlo\.codex\custom-harness\capability-index.generated.md
Get-Content C:\Users\asmlo\.codex\skills\dispatch\SKILL.md
uv run python tests\test_harness_task05_validation_gate.py
```

## Out of Scope

1. Adding new harness features.
2. Backfilling missing behavior that earlier tasks did not implement.
3. Using the capability index as a runtime router.
