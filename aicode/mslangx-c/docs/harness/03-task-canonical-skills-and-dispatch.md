# Task 03 - Canonical Skills and Dispatch

## Status

Complete. Verified on 2026-04-25 with the acceptance commands in this
document and `py -3 tests\test_harness_task03_skills.py`.

## Goal

Create the seed Codex skills and the optional dispatch compatibility skill
using native skill packages rather than command manifests or a custom parser
runtime.

## Design Links

- Harness architecture baseline: [./codex-harness-design.md](./codex-harness-design.md)
- Status index: [./tasks.index.md](./tasks.index.md)

## Dependencies

1. Task 01
2. Task 02

## Scope

1. Resolve the real canonical source for each seed skill before copying or
   linking anything.
2. Create canonical skill packages under `~/.codex/custom-harness/skills/`.
3. Create the dispatch compatibility skill under
   `~/.codex/skills/dispatch/`.
4. Preserve one canonical behavior source when symlinks are used.
5. Do not migrate `dev-flow`, `git-commit`, or `vsc-commit` as part of the
   initial seed set.

## File Ownership

1. `C:\Users\asmlo\.codex\custom-harness\skills\doc-refine\SKILL.md`
2. `C:\Users\asmlo\.codex\custom-harness\skills\doc-sync\SKILL.md`
3. `C:\Users\asmlo\.codex\custom-harness\skills\doc-sync\agents\openai.yaml`
4. `C:\Users\asmlo\.codex\custom-harness\skills\doc-sync\scripts\`
5. `C:\Users\asmlo\.codex\custom-harness\skills\doc-write\SKILL.md`
6. `C:\Users\asmlo\.codex\skills\dispatch\SKILL.md`
7. `C:\Users\asmlo\.codex\skills\dispatch\agents\openai.yaml`
8. `C:\Users\asmlo\.codex\custom-harness\registry.yaml`

## Field Contract

1. Every active skill must have `SKILL.md` frontmatter with `name` and
   `description`.
2. `agents/openai.yaml` is optional and must be valid YAML when present.
3. `default_prompt` is optional and must not be treated as required.
4. Heavy workflow skills should set
   `policy.allow_implicit_invocation = false` in `agents/openai.yaml` when the
   file exists.
5. Deterministic logic belongs in `scripts/`; reference material belongs in
   `references/`.
6. The dispatch compatibility skill must resolve bare names through the registry map and stop on unknown or ambiguous names.

## Dispatch Behavior

1. `$dispatch agent:code-reviewer review the current diff` maps to the
   `code-reviewer` subagent.
2. `$dispatch skill:doc-sync check harness docs` maps to the canonical
   `doc-sync` skill.
3. `$dispatch code-reviewer review the current diff` resolves through the
   registry map and maps to the `code-reviewer` subagent.
4. `$dispatch doc-sync check harness docs` resolves through the registry map
   and maps to the canonical `doc-sync` skill.
5. `$dispatch dev-flow` resolves through the registry map and reports that
   `dev-flow` is not migrated in the seed set.
6. `$dispatch git-commit` resolves through the registry map and reports that
   `git-commit` is not migrated in the seed set.
7. Mutation requested while Plan Mode is active produces a plan only.

## Planned Steps

1. Audit the current local skill sources and identify the canonical source for
   `doc-refine`, `doc-sync`, and `doc-write`.
2. Copy or symlink the canonical skills into
   `~/.codex/custom-harness/skills/`.
3. Add or preserve `agents/openai.yaml` only where it provides useful
   interface metadata.
4. Write `~/.codex/custom-harness/registry.yaml` as the canonical compatibility
   map for dispatch resolution.
5. Write `~/.codex/skills/dispatch/SKILL.md` as a compatibility translator
   from legacy dispatch-style requests to native Codex skill, subagent, or
   deferred command usage.
6. Add optional dispatch interface metadata at
   `~/.codex/skills/dispatch/agents/openai.yaml`.
7. Verify that the seed set excludes `dev-flow`, `git-commit`, and
   `vsc-commit`.

## Acceptance

1. `doc-refine`, `doc-sync`, and `doc-write` exist under the canonical harness
   skills tree.
2. The dispatch compatibility skill exists under `~/.codex/skills/dispatch/`.
3. Every active skill has valid `SKILL.md` frontmatter.
4. Optional `agents/openai.yaml` files parse as YAML.
5. Unknown, ambiguous, and not-migrated dispatch targets stop without guessing.
6. The task is complete only when all edited files are UTF-8, LF, and free of
   trailing whitespace.

## Acceptance Commands

```powershell
Get-Content C:\Users\asmlo\.codex\custom-harness\skills\doc-sync\SKILL.md
Get-Content C:\Users\asmlo\.codex\custom-harness\registry.yaml
Get-Content C:\Users\asmlo\.codex\skills\dispatch\SKILL.md
Test-Path C:\Users\asmlo\.codex\custom-harness\skills\vsc-commit
```

## Out of Scope

1. Creating custom command manifests.
2. Creating `~/.codex/commands/`.
3. Migrating `dev-flow`, `git-commit`, or `vsc-commit`.
4. Implementing a standalone slash parser.
