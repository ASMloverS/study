# AGENTS.md

Mandatory rules for this repository. No exceptions.

## AGENTS.md Rules

- All rules added to this file MUST be clear, unambiguous, concise, and written as mandatory English statements.

## Text Files

- All text files MUST be UTF-8 (no BOM) with LF line endings.
- Trailing whitespace MUST be stripped on every save.

## C Code

- C code MUST follow `docs/language/28-style-c.md`.
- All build output MUST go to `build/` only. Creating multiple build directories is forbidden.
- Exactly one executable target (`ms`). Building separate executables per feature is forbidden.
- Every rebuild MUST overwrite the existing executable in place. Generating a different executable file per build is forbidden.

## ms Code

- ms scripts MUST follow `docs/language/11-style-ms.md`.

## Python Scripts

- Python 3.14+ standard, PEP 8 compliant.
- All Python code MUST have complete type hints.
- Use `X | None` and `A | B`, never `Optional[X]` or `Union[A, B]`.

## Git Commits

- Commit messages: clear, concise English.
- Format: `<gitmoji> <type>(<scope>): <message>`
- Example: `✨ feat(stdlib/strings): add format function`
- Every completed change MUST be committed as a corresponding git commit for tracking and rollback.

## Testing

- Every change MUST include new or updated tests.
- All tests and verification MUST pass before delivery to the user.
