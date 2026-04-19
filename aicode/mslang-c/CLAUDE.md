# CLAUDE.md

## Project & Structure
Project info/arch/layout/conventions/phases → `.claude/rules/project.md`

## Build & Test
Build/test cmds → `.claude/rules/build-and-test.md`

## Permission Rules
- Deny: /build
- Deny: /build_ninja
- Deny: /node_modules
- Deny: /.git
- Deny: /.clangd
- Deny: /tmp
- Deny: *.log
- Deny: *.swp
