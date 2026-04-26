# Codex Harness Design

## Summary

Design a Codex-native custom harness that reproduces the useful behavior of the
Claude `dispatch` + `custom-harness` stack without cloning its runtime model.

The harness should use documented Codex extension points:

- `AGENTS.md` for durable repository and user operating policy
- `~/.codex/custom-harness/skills/` as the canonical harness skill source
- `~/.codex/custom-harness/agents/` as the canonical harness subagent source
- `~/.codex/skills/dispatch/` for the Codex-native dispatch compatibility
  skill
- optional `agents/openai.yaml` files for skill interface metadata
- native Codex discovery locations exposed by symlink, hardlink, or install
  step when Codex does not scan the custom harness directory directly
- optional generated audit indexes for humans, not runtime routing

Do not create a separate `~/.codex/harness/*.toml` registry, router, command
runtime, or slash parser for v1. Codex already has native skill discovery and
custom subagent loading. The custom harness should compose those mechanisms
instead of introducing a second execution model.

The Claude harness is migration evidence only. It must not become the runtime
source of truth, and `~/.claude/` must not be modified.

Official Codex references:

- [Codex skills](https://developers.openai.com/codex/skills)
- [Codex subagents](https://developers.openai.com/codex/subagents)
- [Codex app commands](https://developers.openai.com/codex/app/commands)

## Key Changes

### 1. Codex-Native Harness Layout

Use `~/.codex/custom-harness/` as the canonical harness source tree, and expose
entries to native Codex discovery locations only when required by Codex runtime
loading.

Canonical harness assets:

```text
~/.codex/
  custom-harness/
    agents/
      code-implementer.toml
      code-reviewer.toml
      bug-fixer.toml
      doc-reviewer.toml
      doc-fixer.toml
      git-committer.toml
    skills/
      doc-sync/
        SKILL.md
        agents/openai.yaml
        scripts/
      doc-write/
        SKILL.md
      doc-refine/
        SKILL.md
    registry.yaml
    README.md
    capability-index.generated.md
```

Runtime exposure when Codex requires native locations:

```text
~/.codex/
  agents/
    code-implementer.toml -> ~/.codex/custom-harness/agents/code-implementer.toml
    code-reviewer.toml -> ~/.codex/custom-harness/agents/code-reviewer.toml
  skills/
    dispatch/
      SKILL.md
      agents/openai.yaml
```

Optional project-local exposure for repository-specific overrides:

```text
.codex/agents/
.agents/skills/
```

Rules:

- Put canonical harness subagents in `~/.codex/custom-harness/agents/`.
- Put canonical harness skills in `~/.codex/custom-harness/skills/`, except
  for the dispatch compatibility skill.
- Put the dispatch compatibility skill in `~/.codex/skills/dispatch/`.
- Use `~/.codex/agents/` and `~/.agents/skills/` only as Codex runtime
  exposure points when needed.
- Use `.codex/agents/` and `.agents/skills/` only for repository-local
  overrides that should travel with a specific repository.
- Use symlinks or hardlinks only when they preserve a single canonical behavior
  source.
- Do not use `~/.codex/commands/` as a command runtime.
- Do not use `~/.codex/harness/` as a runtime registry or router; the harness
  root is `~/.codex/custom-harness/`.

### 2. Runtime Model

The harness has three active surfaces:

- policy surface: `AGENTS.md`
- workflow surface: Codex skills
- delegation surface: Codex custom subagents

Runtime resolution is native:

- A skill is invoked explicitly with `$skill-name`, selected by the Codex UI,
  or selected implicitly when its `description` clearly matches the task.
- A custom subagent is available from its `.toml` file and should be spawned
  only when the parent task explicitly benefits from delegation.
- A workflow that needs subagents should say which custom subagent to spawn and
  what scope to assign.
- Legacy `dispatch` behavior is optional compatibility and should be
  implemented as a skill that consults `~/.codex/custom-harness/registry.yaml`,
  not as a standalone parser or registry resolver.

### 3. Claude Concept Mapping

Map Claude harness concepts to Codex-native concepts:

| Claude concept | Codex target | Runtime role |
| --- | --- | --- |
| `agents/*.md` | `~/.codex/custom-harness/agents/*.toml` | real custom subagents |
| `commands/*.md` | `~/.codex/custom-harness/skills/*/SKILL.md` | workflow skills |
| `skills/*/SKILL.md` | `~/.codex/custom-harness/skills/*/SKILL.md` | native skills |
| `dispatch` | `~/.codex/skills/dispatch/SKILL.md` | optional compatibility translator |
| `registry.yaml` | `~/.codex/custom-harness/registry.yaml` | compatibility map reference only |

Do not preserve Claude file names if they conflict with Codex conventions.
Prefer hyphenated subagent names such as `code-reviewer` and hyphenated skill
names such as `doc-sync`.

### 4. Workflow Skills Instead of Commands

Replace command manifests with skills only when a command has an approved
Codex-native migration target. Do not migrate `dev-flow`, `git-commit`, or
`vsc-commit` as part of the initial seed set.

Required compatibility skill:

- `dispatch`
  - translates legacy dispatch-style requests into Codex-native skill or
    subagent usage
  - lives at `~/.codex/skills/dispatch/SKILL.md`
  - should not be required for normal Codex use

Recommended native invocation:

```text
$doc-sync check documentation drift
Spawn code-reviewer to review the current diff.
```

Supported compatibility invocation:

```text
$dispatch agent:code-reviewer review the current diff
$dispatch skill:doc-sync check harness docs
$dispatch code-reviewer review the current diff
$dispatch doc-sync check harness docs
```

The compatibility layer must resolve bare names through the registry map when
the match is unique. It must stop on unknown or ambiguous names, must not
guess, mutate files in Plan Mode, or bypass repository policy.

### 5. Custom Subagents

Use the documented custom subagent schema. Required fields are:

```toml
name = "code-reviewer"
description = "Review changes for correctness, regressions, security, and missing tests."
developer_instructions = """
Review like a code owner.
Lead with findings ordered by severity.
Do not edit files.
"""
```

Optional fields may include:

```toml
nickname_candidates = ["Atlas", "Delta"]
model = "gpt-5.4"
model_reasoning_effort = "high"
sandbox_mode = "read-only"
```

Recommended project agent config:

```toml
[agents]
max_threads = 6
max_depth = 1
```

Rules:

- Use `sandbox_mode = "read-only"` for reviewers and auditors.
- Use `sandbox_mode = "workspace-write"` only for agents that are expected to
  edit files.
- Omit `model` unless the role needs a different reasoning or cost profile.
- Keep `max_depth = 1` unless recursive delegation is explicitly required.
- Do not create custom agents that unintentionally shadow built-in agent names.
- Do not treat old custom TOML fields such as `entrypoint`, `invoke_mode`, or
  `output_contract` as Codex runtime fields.

Initial active subagents:

- `code-implementer`: scoped implementation after requirements are clear
- `code-reviewer`: read-only code review
- `bug-fixer`: systematic debugging and targeted fixes
- `doc-reviewer`: read-only documentation audit
- `doc-fixer`: documentation edits only
- `git-committer`: commit preparation when explicitly asked

Defer these until they have distinct behavior:

- `doc-designer`, unless it differs from normal planning and design writing
- `doc-corrector`, unless it differs from `doc-fixer` and `doc-sync`

## Public Interfaces and Types

### Skill Package

Required:

```yaml
---
name: skill-name
description: Clear trigger scope and boundaries.
---
```

Optional:

```text
scripts/
references/
assets/
agents/openai.yaml
```

Recommended `agents/openai.yaml` shape:

```yaml
interface:
  display_name: "Human Name"
  short_description: "Short UI summary"
  default_prompt: "Optional prompt"
policy:
  allow_implicit_invocation: false
dependencies:
  tools:
    - type: "mcp"
      value: "openaiDeveloperDocs"
      description: "OpenAI Docs MCP server"
      transport: "streamable_http"
      url: "https://developers.openai.com/mcp"
```

Rules:

- `SKILL.md` frontmatter `name` and `description` are required.
- `agents/openai.yaml` is optional.
- `default_prompt` is optional.
- Heavy workflow skills should set `allow_implicit_invocation: false`.
- Skill descriptions must state when to use the skill and when not to use it.
- Deterministic logic should live in `scripts/`; reference material should
  live in `references/`.

### Custom Subagent

Required:

```toml
name = "agent_name"
description = "When Codex should use this agent."
developer_instructions = """
Role-specific behavior and output contract.
"""
```

Optional:

```toml
nickname_candidates = ["Atlas", "Delta"]
model = "gpt-5.4"
model_reasoning_effort = "high"
sandbox_mode = "read-only"
```

Rules:

- The `name` field is the identity source of truth.
- File names should match `name` where practical.
- Review agents must be read-only.
- Write agents must describe write scope and verification expectations.
- Subagents inherit the parent session approval model unless explicitly
  configured by Codex-supported fields.

### Capability Index

If a global overview is useful, generate it as documentation only:

```text
~/.codex/custom-harness/capability-index.generated.md
```

Suggested fields:

```text
kind: skill | subagent | policy
name: string
scope: repo | user | admin | system
source_path: path
trigger: explicit | implicit | explicit-only
status: active | legacy-reference | deferred
verified_by: command or check name
```

The capability index must not become a runtime router, registry, or source of
truth that duplicates skill and subagent metadata.

## Seed Migration

### Agents

| Claude item | Codex target | Status |
| --- | --- | --- |
| `bug-fixer` | `~/.codex/custom-harness/agents/bug-fixer.toml` | active as `bug-fixer` |
| `code-implementer` | `~/.codex/custom-harness/agents/code-implementer.toml` | active as `code-implementer` |
| `code-reviewer` | `~/.codex/custom-harness/agents/code-reviewer.toml` | active as `code-reviewer` |
| `doc-fixer` | `~/.codex/custom-harness/agents/doc-fixer.toml` | active as `doc-fixer` |
| `doc-reviewer` | `~/.codex/custom-harness/agents/doc-reviewer.toml` | active as `doc-reviewer` |
| `git-committer` | `~/.codex/custom-harness/agents/git-committer.toml` | active as `git-committer`, explicit only |
| `doc-corrector` | merge into `doc-fixer` or defer | deferred |
| `doc-designer` | merge into planning workflow or defer | deferred |

### Commands

| Claude item | Codex target | Status |
| --- | --- | --- |
| `dispatch` | `~/.codex/skills/dispatch/SKILL.md` | active compatibility skill |
| `dev-flow` | none | not migrated in this seed set |
| `git-commit` | none | not migrated in this seed set |

### Skills

| Claude item | Codex target | Status |
| --- | --- | --- |
| `doc-refine` | `~/.codex/custom-harness/skills/doc-refine` | active |
| `doc-sync` | `~/.codex/custom-harness/skills/doc-sync` | active |
| `doc-write` | `~/.codex/custom-harness/skills/doc-write` | active |
| `vsc-commit` | none | not migrated in this seed set |

The migration must resolve the real canonical skill source before copying or
linking anything. A symlink or hardlink is acceptable when it keeps one source
of behavior.

## Compatibility Boundaries

Explicitly exclude:

- modifying `~/.claude/`
- bidirectional sync with Claude files
- a custom registry runtime under `~/.codex/harness/`
- a custom command runtime under `~/.codex/commands/`
- a custom slash parser as a required execution path
- undocumented custom subagent fields as runtime behavior
- exact emulation of Claude tool names or execution internals

Explicitly include:

- behavior-level migration from Claude reference files
- source audit for drifted Claude registry paths
- native Codex skills for workflows
- native Codex custom subagents for role specialization
- optional dispatch compatibility as a skill
- generated human-readable capability index if useful

## Test Plan

### Static Validation

Skill checks:

- every active skill has `SKILL.md`
- every `SKILL.md` has frontmatter `name` and `description`
- no duplicate skill names exist in the same scope unless documented
- `agents/openai.yaml` is valid YAML when present
- `default_prompt` is not treated as required
- heavy workflow skills disable implicit invocation

Subagent checks:

- every active subagent file is valid TOML
- every active subagent has `name`, `description`, and
  `developer_instructions`
- read-only roles use `sandbox_mode = "read-only"`
- write roles document write scope and verification expectations
- no custom subagent unintentionally shadows built-in roles

Config checks:

- Codex config has bounded `[agents]` settings when custom harness subagents
  are exposed to the runtime
- `max_depth` remains `1` unless explicitly justified
- no config bypasses repository or user approval policy

### Behavioral Validation

Skill behavior scenarios:

- `$dispatch agent:code-reviewer review harness docs`
  - maps to the `code-reviewer` subagent
  - does not look for a custom registry route
- `$dispatch skill:doc-sync check harness docs`
  - maps to the canonical `doc-sync` skill
  - does not require a custom registry route
- `$dispatch command:dev-flow`
  - reports that `dev-flow` is not migrated in the seed set
  - does not invent a workflow skill
- `$dispatch command:git-commit`
  - reports that `git-commit` is not migrated in the seed set
  - does not map to `vsc-commit`

Subagent behavior scenarios:

- `code-reviewer` reviews without editing files
- `code-implementer` edits only the assigned scope and reports verification
- `bug-fixer` reproduces or localizes a failure before changing code
- `doc-fixer` edits documentation only
- the parent refuses or rescopes overlapping write assignments before spawning
  multiple writing agents

Negative scenarios:

- unknown dispatch target stops with valid choices
- ambiguous legacy name stops and asks for type-qualified form
- bare dispatch name resolves through the registry map when unique
- mutation requested while Plan Mode is active produces a plan only
- commit requested without explicit commit instruction stops
- missing skill or subagent file is reported as configuration drift

## Assumptions

- Codex skills and custom subagents are the only runtime extension mechanisms
  required for v1.
- Canonical harness files should live in `~/.codex/custom-harness/skills` and
  `~/.codex/custom-harness/agents`.
- The dispatch compatibility skill should live in
  `~/.codex/skills/dispatch/SKILL.md`.
- Native Codex discovery locations such as `~/.agents/skills` and
  `~/.codex/agents` should be exposure points, not the canonical harness
  source.
- Claude `custom-harness` remains read-only migration input.
- Skills are the correct Codex abstraction for commands and workflows.
- Custom subagents are the correct Codex abstraction for role specialization.
- Slash-like compatibility is optional and should be implemented through a
  skill, not a parser runtime.
- Generated indexes are documentation and audit aids only; they are not
  execution routers.
