# AGENTS.md

## 工作流规则

- 每次改动完成，都必须创建一个对应的 git commit，方便后续追踪与回滚。
- 每次改动后，都必须编写或更新测试文件，并在交付给用户前确保所有测试和验证全部通过（`npm run typecheck` + `npx vitest run` + `npm run build -w client`）。
