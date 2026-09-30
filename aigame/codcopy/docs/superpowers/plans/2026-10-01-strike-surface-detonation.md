# Strike Surface Detonation + Direct-Hit Kill 修复计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复空袭/集束"看不见爆炸、打不死人"：落弹按目标点实际表面高度（含掩体顶面）引爆，并将单发伤害提到直击必杀水平。

**Architecture:** 新增共享纯函数 `surfaceYAt(boxes, x, z)`（对 AABB 列表做 2D footprint 包含查询，返回最高顶面 y，地面默认 0）。服务端 `processStrikes` 在每个落弹**引爆时刻**用 `this.obstacles`（实时，含已破坏掩体/动态门状态）采样表面，在 `surface + 0.6` 引爆。客户端红烟标记用同一函数（静态 `mapToObstacles`）。伤害常量 90/70 → 120/120。

**Tech Stack:** TypeScript monorepo（shared/server/client 三包），vitest，既有 `Room` 测试模式（`killPlayer` internals 赚连杀 + `enqueueInput` 激活 + `room.step()` 推进）。

**根因回顾（已与用户确认）：** `world.ts:924` 硬编码 `explodeAt(..., s.x, 0.6, s.z, ...)`。落点在集装箱（顶 2.6m）/木箱堆/内墙上时，爆炸生成于几何体内部 → 粒子被遮挡（不可见）+ 伤害射线被掩体自身 LOS 阻挡（打不死）。仓库中央战斗热区密布此类掩体。次要问题：单发 90 < 100 HP，开阔地偏离弹线 2m 即存活。

**直击必杀的数学依据（h=1.8 站立，chest = y + 1.08，爆心 = surface + 0.6，垂直距离 0.48）：**
- 空袭 R=4：120 × (1 − 0.48/4) = 105.6 ≥ 100 ✓
- 集束 R=3：120 × (1 − 0.48/3) = 100.8 ≥ 100 ✓

**验证门槛（每任务后 + 最终）：** `npm run typecheck` + `npx vitest run` + `npm run build -w client` 全绿。

---

### Task 1: 共享 `surfaceYAt` 纯函数（TDD）

**Files:**
- Modify: `shared/src/physics/aabb.ts`（文件末尾追加函数）
- Test: `shared/test/aabb.test.ts`（追加 describe 块）

- [ ] **Step 1: 写失败测试** — 在 `shared/test/aabb.test.ts` 末尾追加：

```ts
import { boxCenter, moveBodyAxis, surfaceYAt, type Body, type AABB } from '../src';

describe('surfaceYAt', () => {
  const container: AABB = boxCenter(5, 1.3, -2, 2.4, 2.6, 6); // 顶面 2.6, footprint x∈[3.8,6.2] z∈[-5,1]
  const crate: AABB = boxCenter(0, 0.6, 0, 1.2, 1.2, 1.2); // 顶面 1.2

  it('returns 0 on open ground', () => {
    expect(surfaceYAt([container], 20, 20)).toBe(0);
  });

  it('returns cover top inside footprint (inclusive edges)', () => {
    expect(surfaceYAt([container], 5, -5)).toBeCloseTo(2.6, 5);
    expect(surfaceYAt([container], 3.8, -2)).toBeCloseTo(2.6, 5);
  });

  it('returns highest top among overlapping covers', () => {
    expect(surfaceYAt([crate, container], 5, -2)).toBeCloseTo(2.6, 5);
    expect(surfaceYAt([crate, boxCenter(5, 1.8, -2, 1.2, 1.2, 1.2)], 5, -2)).toBeCloseTo(2.4, 5);
  });

  it('returns 0 for empty obstacle list', () => {
    expect(surfaceYAt([], 0, 0)).toBe(0);
  });
});
```

注意：现有文件顶部 import 行需合并为上式（把 `surfaceYAt`、`type AABB` 加进既有 import）。

- [ ] **Step 2: 跑测试确认失败**

Run: `npx vitest run shared/test/aabb.test.ts`
Expected: FAIL，`surfaceYAt is not a function`（或 import 报错）。

- [ ] **Step 3: 最小实现** — `shared/src/physics/aabb.ts` 末尾追加：

```ts
/** [M16] 垂直采样 (x,z) 处最高表面（含掩体顶面，footprint 边界含端点），地面为 0；空袭/集束按此高度引爆 */
export function surfaceYAt(boxes: readonly AABB[], x: number, z: number): number {
  let top = 0;
  for (const b of boxes) {
    if (x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ && b.maxY > top) top = b.maxY;
  }
  return top;
}
```

（`shared/src/index.ts` 已 `export * from './physics/aabb'`，无需额外导出。）

- [ ] **Step 4: 跑测试确认通过**

Run: `npx vitest run shared/test/aabb.test.ts`
Expected: PASS（原有 moveBodyAxis 4 项 + 新 4 项）。

- [ ] **Step 5: Commit**

```bash
git add shared/src/physics/aabb.ts shared/test/aabb.test.ts
git commit -m "✨ feat(codcopy): add surfaceYAt vertical surface sampling helper"
```

---

### Task 2: 服务端按表面高度引爆（TDD）

**Files:**
- Create: `server/test/strikeSurface.test.ts`
- Modify: `server/src/game/world.ts:919-926`（`processStrikes`）与 import 块（加 `surfaceYAt`）

- [ ] **Step 1: 写失败测试** — 新建 `server/test/strikeSurface.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import { AIRSTRIKE_COUNT, MAPS } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';

type RoomInternals = {
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function input(seq: number, streak: number, streakTarget?: { x: number; z: number }, streakYaw?: number) {
  return { seq, moveX: 0, moveZ: 0, yaw: 0, pitch: -1.0, buttons: 0, slot: 0, streak, streakTarget, streakYaw };
}

function setup(seed: number) {
  const room = new Room(MAPS.warehouse, { seed, bots: 0, killLimit: 100 });
  const a = room.addPlayer('A', false);
  const b = room.addPlayer('B', false);
  const c = room.addPlayer('C', false);
  const internals = room as unknown as RoomInternals;
  for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
  a.spawnProtUntil = 0;
  c.spawnProtUntil = 0;
  return { room, a, c };
}

/** [M16] 集装箱 pos [5,1.3,-2] size [2.4,2.6,6]：footprint x∈[3.8,6.2] z∈[-5,1]，顶面 y=2.6。
 * 落点 (5,-5)、heading 0 时 5 弹 z = -0.6/-2.8/-5.0/-7.2/-9.4，前 3 发落在箱顶。 */
describe('[M16] strike surface detonation', () => {
  it('bomblets on container footprint detonate at container top, killing players standing on it', () => {
    const { room, a, c } = setup(51);
    Object.assign(c.st, { x: 5, y: 2.6, z: -5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 5, z: -5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(AIRSTRIKE_COUNT);
    let onTop = 0;
    let offTop = 0;
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      if (e.pos.z >= -5.001 && e.pos.z <= 1) {
        onTop++;
        expect(e.pos.y).toBeCloseTo(3.2, 1); // 集装箱顶 2.6 + 0.6
      } else {
        offTop++;
        expect(e.pos.y).toBeCloseTo(0.6, 1); // 开阔地面
      }
    }
    expect(onTop).toBe(3);
    expect(offTop).toBe(2);
    expect(c.alive).toBe(false);
    expect(a.kills).toBe(6);
  });

  it('player sheltered behind the container is shielded from top-surface blasts (LOS)', () => {
    const { room, a, c } = setup(52);
    Object.assign(c.st, { x: 7.5, y: 0, z: -5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 5, z: -5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const evs = room.drainEvents();
    expect(c.alive).toBe(true);
    expect(evs.some((e) => e.type === 'kill' && e.victimId === c.id)).toBe(false);
  });
});
```

- [ ] **Step 2: 跑测试确认失败**

Run: `npx vitest run server/test/strikeSurface.test.ts`
Expected: FAIL——第 1 项：`onTop` 弹的 `pos.y` 是 0.6 而非 3.2，且 `c.alive === true`（只受擦伤）；第 2 项在旧代码下即通过（掩体本身挡住地面爆炸，属预期，保留作回归防线）。

- [ ] **Step 3: 实现** — `server/src/game/world.ts`：

3a. import 列表（`boxCenter,` 一行附近）加入 `surfaceYAt,`。

3b. `processStrikes` 改为：

```ts
private processStrikes(): void {
    for (let i = this.pendingStrikes.length - 1; i >= 0; i--) {
      const s = this.pendingStrikes[i];
      if (this.tick < s.atTick) continue;
      this.pendingStrikes.splice(i, 1);
      // [M16] 引爆时刻采样实时表面（含掩体顶面；掩体被摧毁则弹落回地面）
      const y = surfaceYAt(this.obstacles, s.x, s.z) + 0.6;
      this.explodeAt(s.attackerId, s.x, y, s.z, s.radius, s.dmg, s.cause, s.exemptId);
    }
}
```

- [ ] **Step 4: 跑全部服务端连杀测试确认通过且无回归**

Run: `npx vitest run server/test/strikeSurface.test.ts server/test/streak.test.ts server/test/streakPlace.test.ts server/test/streakQueue.test.ts`
Expected: 全 PASS。

- [ ] **Step 5: Commit**

```bash
git add server/src/game/world.ts server/test/strikeSurface.test.ts
git commit -m "✨ feat(codcopy): detonate strikes at sampled surface height"
```

---

### Task 3: 单发伤害提到直击必杀（120/120）+ 文档同步

**Files:**
- Modify: `shared/src/constants.ts:86,90`
- Modify: `docs/DESIGN.md:143-144`

- [ ] **Step 1: 改常量** — `shared/src/constants.ts`：

```ts
export const AIRSTRIKE_DMG = 120;
```

```ts
export const CLUSTER_DMG = 120;
```

（90→120、70→120，注释行不变。仓库内无任何测试硬编码 90/70，已 grep 确认。）

- [ ] **Step 2: 跑全量测试确认无回归**

Run: `npx vitest run`
Expected: 全 PASS（豁免测试断言的是"不受伤/满血"，与伤害数值无关；seed 42 的开阔地击杀测试只会更稳）。

- [ ] **Step 3: 同步 DESIGN.md** — §5 表格两行效果列：

空袭行 `（半径 4m / 单点最高 90 伤，LOS 阻挡）` → `（半径 4m / 单点最高 120 伤直击必杀，按落点表面高度引爆，LOS 阻挡）`

集束行 `（半径 3m / 单点最高 70 伤）` → `（半径 3m / 单点最高 120 伤直击必杀，按落点表面高度引爆）`

（该两行还残留 M15 之前的"准星指向""1.5s 延迟"旧描述——非本次改动引入的失真，不在本任务顺手改，仅向用户提示。）

- [ ] **Step 4: Commit**

```bash
git add shared/src/constants.ts docs/DESIGN.md
git commit -m "⚡ feat(codcopy): raise strike damage to 120 for guaranteed direct-hit kills"
```

---

### Task 4: 客户端红烟标记贴合表面高度

**Files:**
- Modify: `client/src/main.ts:577`（`streakUse` 分支）与 shared import（`MAPS` 已在 line 9 导入，追加 `mapToObstacles`、`surfaceYAt`）

- [ ] **Step 1: 加模块级常量** — `client/src/main.ts` 中 `const minimap = ...`（line 61）附近：

```ts
const MAP_OBSTACLES = mapToObstacles(MAPS.warehouse);
```

- [ ] **Step 2: 改 smokeMarker 调用** — line 577：

```ts
fx.smokeMarker(new THREE.Vector3(e.target.x, surfaceYAt(MAP_OBSTACLES, e.target.x, e.target.z) + 0.8, e.target.z));
```

- [ ] **Step 3: 类型检查 + 构建**

Run: `npm run typecheck`; `npm run build -w client`
Expected: 均无错误。

- [ ] **Step 4: Commit**

```bash
git add client/src/main.ts
git commit -m "✨ feat(codcopy): place strike smoke marker at surface height"
```

---

### Task 5: 最终验证

- [ ] **Step 1: 三门槛全绿**

Run: `npm run typecheck`; `npx vitest run`; `npm run build -w client`
Expected: 全部通过。

- [ ] **Step 2: 手动冒烟（可选，交付说明中列出）**

`npm run dev` 开本地局：攒 5 杀 → `5` 打开俯图 → 把落点选在中央集装箱上确认 → 3s 后应看到箱顶火球 + 站箱顶 bot 阵亡击杀信息；选开阔地落点 → 地面火球照常；烟囱红烟应出现在箱顶而非箱内。

- [ ] **Step 3: 汇总** — 向用户交付：根因 → 修复内容 → 测试证据 → 手动清单。
