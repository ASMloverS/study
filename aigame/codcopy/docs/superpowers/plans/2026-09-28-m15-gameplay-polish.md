# M15 手感打磨实施计划（枪模区分 / 音效 CoD 化 / 连杀放置模式）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复空袭/集束炸弹（CoD 式俯图放置、自伤豁免、全套 incoming 反馈），拉开 7 把枪的第一/第三人称轮廓区分度，枪声升级为五层合成 + MW 风命中反馈。

**Architecture:** 权威服务器（30Hz `Room.step`）+ 瘦客户端。放置模式：客户端俯图选点 → `InputMsg.streakTarget/streakYaw` 随输入上行 → 服务端校验落点并调度 `pendingStrikes`（新增 `exemptId` 豁免使用者）。音效仍全合成（Web Audio），新增 crack/body/sub/tail/mech 五层参数表。枪模为程序化几何，仅调尺寸与部件。

**Tech Stack:** TypeScript monorepo（shared/server/client npm workspaces）、vitest、Three.js、Web Audio API、Canvas 2D。

**验证命令（每个任务后运行相应项，Task 11 全量）：** `npm run typecheck`、`npx vitest run`、`npm run build -w client`（在 `C:\Workspace\Repositories\study\aigame\codcopy` 下执行）。

**约定：** 所有提交信息格式 `<gitmoji> <type>(scope): <message>`（英文）。仓库根在 `study/`，只在 `codcopy/` 下操作。所有测试文件 LF 行尾、UTF-8。

**关键现状（写码前先读这些位置）：**
- `shared/src/protocol.ts:24-33` `InputMsg`
- `server/src/game/world.ts:194-202` `pendingStrikes` 声明、`445-465` `enqueueInput` 清洗、`507-517` `takeInput`、`845-905` `activateStreak`+`groundTarget`、`907-914` `processStrikes`、`789-820` `explodeAt`
- `shared/src/constants.ts:77-90` 连杀常量
- `client/src/input.ts:79-117` 键鼠事件、`169-203` `buildInput`
- `client/src/main.ts:540-562` streakUse 事件处理、`799-867` 输入循环、`446-473` kill 事件
- `client/src/audio.ts` 全文件（241 行）
- `client/src/render/viewmodel.ts:412-504` buildAr、`585-658` buildLmg、`788-796` BUILDERS
- `client/src/render/actors.ts:63-131` GEO 与 gunFor
- `client/src/render/minimap.ts` 全文件、`client/src/render/effects.ts:23-33,316-342`
- `client/index.html`（DOM）、`client/src/style.css`（样式）

---

### Task 1: 协议扩展与服务端输入清洗（TDD）

**Files:**
- Modify: `shared/src/protocol.ts`（InputMsg）
- Modify: `server/src/game/world.ts`（enqueueInput、takeInput）
- Test: `server/test/streakPlace.test.ts`（新建）

- [ ] **Step 1: 写失败测试**

新建 `server/test/streakPlace.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import { MAPS } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';

type RoomInternals = {
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function input(seq: number, streak: number, streakTarget?: { x: number; z: number }, streakYaw?: number) {
  return { seq, moveX: 0, moveZ: 0, yaw: 0, pitch: -1.0, buttons: 0, slot: 0, streak, streakTarget, streakYaw };
}

describe('[M15] killstreak placement input', () => {
  it('non-finite streakTarget is dropped (falls back to player position at activation)', () => {
    const room = new Room(MAPS.warehouse, { seed: 41, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    Object.assign(a.st, { x: 5, z: 5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: Number.NaN, z: 5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(5);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.abs(e.pos.x - 5)).toBeLessThanOrEqual(5.5);
      expect(Math.abs(e.pos.z - 5)).toBeLessThanOrEqual(5.5);
    }
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run server/test/streakPlace.test.ts`
Expected: FAIL——当前 `activateStreak` 用 `groundTarget`（pitch=-1.0 俯视 → 落点 ≈ 玩家前方 2m），5 发 blast 落在 (5,5) 附近 ±2.2m 尚可，但 NaN target 未经清洗；实际失败点为 blast 数量/位置断言（若恰好通过，Step 4 的 clamped-target 测试会暴露旧行为）。

- [ ] **Step 3: 扩展协议与清洗**

`shared/src/protocol.ts` 的 `InputMsg`（24-33 行）末尾追加两个字段：

```ts
export interface InputMsg {
  seq: number;
  moveX: number;
  moveZ: number;
  yaw: number;
  pitch: number;
  buttons: number;
  slot: number;
  streak?: number;
  /** [M15] 放置模式落点（世界坐标，服务端激活时二次 clamp）与空袭航线角（rad） */
  streakTarget?: { x: number; z: number };
  streakYaw?: number;
}
```

`server/src/game/world.ts` `enqueueInput` 清洗对象中 `streak:` 行后追加：

```ts
      streakTarget:
        input.streakTarget && Number.isFinite(input.streakTarget.x) && Number.isFinite(input.streakTarget.z)
          ? { x: input.streakTarget.x, z: input.streakTarget.z }
          : undefined,
      streakYaw: typeof input.streakYaw === 'number' && Number.isFinite(input.streakYaw) ? input.streakYaw : undefined,
```

`takeInput`（512-515 行）空队列重放清理同步覆盖新字段：

```ts
    } else if (p.lastInput.streak !== 0) {
      // [M14] 空队列重放上一输入：连杀激活是一次性意图，不随重放重复触发
      p.lastInput = { ...p.lastInput, streak: 0, streakTarget: undefined, streakYaw: undefined };
    }
```

- [ ] **Step 4: 运行确认通过**

Run: `npx vitest run server/test/streakPlace.test.ts`
Expected: PASS（此时 groundTarget 仍在，但本测试俯视瞄准落点恰在断言范围内；真正的落点语义切换在 Task 2）。

- [ ] **Step 5: 提交**

```bash
git add shared/src/protocol.ts server/src/game/world.ts server/test/streakPlace.test.ts
git commit -m "✨ feat(codcopy): extend InputMsg with streak placement target and sanitize server-side"
```

---

### Task 2: 放置落点语义 + 自伤豁免（TDD）

**Files:**
- Modify: `server/src/game/world.ts`（pendingStrikes 类型、activateStreak、explodeAt、processStrikes；删除 groundTarget）
- Test: `server/test/streakPlace.test.ts`（追加）

- [ ] **Step 1: 追加失败测试**

在 `server/test/streakPlace.test.ts` 末尾追加：

```ts
describe('[M15] placement targeting and self-exemption', () => {
  function setup(seed: number) {
    const room = new Room(MAPS.warehouse, { seed, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const c = room.addPlayer('C', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    return { room, a, b, c };
  }

  it('airstrike uses client placement target and heading', () => {
    const { room, a, c } = setup(42);
    c.spawnProtUntil = 0;
    Object.assign(c.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 10, z: -8 }, Math.PI / 2));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(5);
    expect(c.alive).toBe(false);
    expect(a.kills).toBe(6);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(e.pos.z).toBeCloseTo(-8, 0);
      expect(e.pos.x).toBeGreaterThanOrEqual(5.5);
      expect(e.pos.x).toBeLessThanOrEqual(14.5);
    }
  });

  it('caller standing at ground zero is exempt from own airstrike', () => {
    const { room, a, c } = setup(43);
    c.spawnProtUntil = 0;
    Object.assign(a.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    Object.assign(c.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 10, z: -8 }, Math.PI / 2));
    for (let i = 0; i < 130; i++) room.step();
    expect(a.alive).toBe(true);
    expect(a.health).toBe(100);
    expect(c.alive).toBe(false);
  });

  it('out-of-bounds placement target is clamped to map edge', () => {
    const { room, a } = setup(44);
    room.enqueueInput(a.id, input(1, 2, { x: 999, z: -999 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast');
    expect(blasts.length).toBe(5);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.abs(e.pos.x)).toBeLessThanOrEqual(23.1);
      expect(Math.abs(e.pos.z)).toBeLessThanOrEqual(23.1);
    }
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run server/test/streakPlace.test.ts`
Expected: FAIL——落点仍来自 `groundTarget`（pitch=-1.0 → 玩家脚下 2m），target 断言与豁免断言（a 会受伤）失败。

- [ ] **Step 3: 实现落点语义与豁免**

`server/src/game/world.ts` `pendingStrikes` 声明（194-202 行）加 `exemptId`：

```ts
  private readonly pendingStrikes: {
    attackerId: number;
    cause: 'airstrike' | 'cluster';
    exemptId?: number;
    x: number;
    z: number;
    atTick: number;
    radius: number;
    dmg: number;
  }[] = [];
```

`activateStreak`（845-893 行）整体替换为（并删除 `groundTarget` 方法 895-905 行）：

```ts
  private activateStreak(p: ServerPlayer, tier: number, input: InputMsg): void {
    if (!streakCanUse(p.streaks, tier)) return;
    streakConsume(p.streaks, tier);
    if (tier === 1) {
      this.events.push({ type: 'streakUse', tick: this.tick, playerId: p.id, tier: 1 });
      return;
    }
    // [M15] CoD 式放置：落点与航线来自客户端俯图，服务端只做边界 clamp
    const lim = this.map.size / 2 - 1;
    const clamp = (v: number): number => Math.max(-lim, Math.min(lim, v));
    const t = input.streakTarget ? { x: clamp(input.streakTarget.x), z: clamp(input.streakTarget.z) } : { x: p.st.x, z: p.st.z };
    const heading =
      typeof input.streakYaw === 'number' && Number.isFinite(input.streakYaw) ? input.streakYaw : input.yaw;
    this.events.push({ type: 'streakUse', tick: this.tick, playerId: p.id, tier: tier as 2 | 3, target: { x: t.x, y: 0.5, z: t.z }, yaw: heading });
    if (tier === 2) {
      const dx = -Math.sin(heading);
      const dz = -Math.cos(heading);
      for (let i = 0; i < AIRSTRIKE_COUNT; i++) {
        const off = (i - (AIRSTRIKE_COUNT - 1) / 2) * AIRSTRIKE_SPACING;
        this.pendingStrikes.push({
          attackerId: p.id, cause: 'airstrike', exemptId: p.id,
          x: clamp(t.x + dx * off), z: clamp(t.z + dz * off),
          atTick: this.tick + AIRSTRIKE_DELAY_TICKS + i * 3, radius: AIRSTRIKE_RADIUS, dmg: AIRSTRIKE_DMG,
        });
      }
    } else {
      for (let i = 0; i < CLUSTER_COUNT; i++) {
        const ang = this.rng() * Math.PI * 2;
        const r = Math.sqrt(this.rng()) * CLUSTER_SCATTER;
        this.pendingStrikes.push({
          attackerId: p.id, cause: 'cluster', exemptId: p.id,
          x: clamp(t.x + Math.cos(ang) * r), z: clamp(t.z + Math.sin(ang) * r),
          atTick: this.tick + AIRSTRIKE_DELAY_TICKS + i * 2, radius: CLUSTER_RADIUS, dmg: CLUSTER_DMG,
        });
      }
    }
  }
```

`explodeAt`（789-820 行）签名加末参 `exemptId?: number`，玩家遍历开头（`if (!q.alive) continue;` 后）加：

```ts
      if (exemptId !== undefined && q.id === exemptId) continue;
```

`processStrikes`（907-914 行）爆炸调用改为：

```ts
      this.explodeAt(s.attackerId, s.x, 0.6, s.z, s.radius, s.dmg, s.cause, s.exemptId);
```

手雷两处 `explodeAt` 调用（708、782 行）不传 exemptId（保留自伤）。

- [ ] **Step 4: 运行确认通过**

Run: `npx vitest run server/test/streakPlace.test.ts server/test/streak.test.ts`
Expected: 全 PASS（streak.test.ts 的旧空袭测试用 pitch=-0.9 俯视，落点仍落在准星地面交点附近的 dummy 上，语义变化不影响它）。

- [ ] **Step 5: 提交**

```bash
git add server/src/game/world.ts server/test/streakPlace.test.ts
git commit -m "✨ feat(codcopy): airstrike/cluster use client-placed target with caller damage exemption"
```

---

### Task 3: 空袭延迟 1.5s → 3s（TDD）

**Files:**
- Modify: `shared/src/constants.ts:82`
- Modify: `server/test/streak.test.ts`（旧空袭测试步数 80 → 130）
- Test: `server/test/streakPlace.test.ts`（追加）

- [ ] **Step 1: 追加延迟边界测试**

在 `server/test/streakPlace.test.ts` 末尾追加：

```ts
describe('[M15] strike delay', () => {
  it('airstrike detonates after 90-tick (3s) delay, not before', () => {
    const room = new Room(MAPS.warehouse, { seed: 45, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    room.enqueueInput(a.id, input(1, 2, { x: 0, z: 0 }, 0));
    for (let i = 0; i < 82; i++) room.step();
    expect(room.drainEvents().filter((e) => e.type === 'blast').length).toBe(0);
    for (let i = 0; i < 25; i++) room.step();
    expect(room.drainEvents().filter((e) => e.type === 'blast').length).toBe(5);
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run server/test/streakPlace.test.ts`
Expected: FAIL——82 步后已有 blast（当前延迟 45 tick）。同时 `server/test/streak.test.ts` 旧测试也会因延迟变长失败，属预期。

- [ ] **Step 3: 修改常量并更新旧测试**

`shared/src/constants.ts:82`：

```ts
export const AIRSTRIKE_DELAY_TICKS = 90;
```

`server/test/streak.test.ts` 空袭测试（`for (let i = 0; i < 80; i++) room.step();`）改为：

```ts
    for (let i = 0; i < 130; i++) room.step();
```

- [ ] **Step 4: 运行确认通过**

Run: `npx vitest run server/test/streakPlace.test.ts server/test/streak.test.ts`
Expected: 全 PASS。

- [ ] **Step 5: 提交**

```bash
git add shared/src/constants.ts server/test/streakPlace.test.ts server/test/streak.test.ts
git commit -m "⏫ feat(codcopy): extend strike inbound delay to 3s for dodge window"
```

---

### Task 4: Placement 俯视战术地图模块（TDD 纯函数）

**Files:**
- Create: `client/src/placement.ts`
- Modify: `client/src/render/minimap.ts`（导出 COVER_COLORS）
- Test: `client/test/placement.test.ts`（新建）

- [ ] **Step 1: 写失败测试**

新建 `client/test/placement.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import { MAPS } from 'shared';
import { Placement, clampToMap, HEADING_STEP } from '../src/placement';

const stubCanvas = { width: 400, style: { display: 'none' } } as unknown as HTMLCanvasElement;

describe('[M15] placement math', () => {
  it('clampToMap bounds to ±(size/2 - 1)', () => {
    expect(clampToMap(999, 48)).toBe(23);
    expect(clampToMap(-999, 48)).toBe(-23);
    expect(clampToMap(10, 48)).toBe(10);
  });

  it('moveCursor translates pixels to world units and clamps to map', () => {
    const p = new Placement(MAPS.warehouse, stubCanvas);
    p.open(2, { x: 0, z: 0, yaw: 0 }, []);
    p.moveCursor(100, -100); // 400px ↔ 48m → 0.12 m/px → (12, -12)
    expect(p.cursor.x).toBeCloseTo(12);
    expect(p.cursor.z).toBeCloseTo(-12);
    p.moveCursor(100000, 100000);
    expect(p.cursor.x).toBe(23);
    expect(p.cursor.z).toBe(23);
  });

  it('rotate steps heading; confirm returns result and closes', () => {
    const p = new Placement(MAPS.warehouse, stubCanvas);
    p.open(2, { x: 1, z: 2, yaw: 0.5 }, []);
    p.rotate(1);
    expect(p.heading).toBeCloseTo(0.5 + HEADING_STEP);
    const r = p.confirm();
    expect(r.streak).toBe(2);
    expect(r.streakTarget).toEqual({ x: 1, z: 2 });
    expect(r.streakYaw).toBeCloseTo(0.5 + HEADING_STEP);
    expect(p.active).toBe(false);
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run client/test/placement.test.ts`
Expected: FAIL——模块不存在。

- [ ] **Step 3: 实现 placement.ts**

`client/src/render/minimap.ts` 顶部 `const COVER_COLORS` 改为 `export const COVER_COLORS`（placement 复用）。新建 `client/src/placement.ts`：

```ts
import { AIRSTRIKE_COUNT, AIRSTRIKE_SPACING, CLUSTER_SCATTER, type MapDef } from 'shared';
import { COVER_COLORS } from './render/minimap';

/** [M15] CoD 式连杀放置：北向朝上战术地图，虚拟光标选点，空袭可旋转航线 */

export const HEADING_STEP = Math.PI / 12; // 15°/档

export function clampToMap(v: number, mapSize: number): number {
  const lim = mapSize / 2 - 1;
  return Math.max(-lim, Math.min(lim, v));
}

export interface PlacementResult {
  streak: 2 | 3;
  streakTarget: { x: number; z: number };
  streakYaw: number;
}

export class Placement {
  active = false;
  tier: 2 | 3 = 2;
  cursor = { x: 0, z: 0 };
  heading = 0;
  private self = { x: 0, z: 0, yaw: 0 };
  private uavEnemies: { x: number; z: number }[] = [];
  private ctx: CanvasRenderingContext2D | null = null;

  constructor(private map: MapDef, private canvas: HTMLCanvasElement) {}

  open(tier: 2 | 3, self: { x: number; z: number; yaw: number }, uavEnemies: { x: number; z: number }[]): void {
    this.tier = tier;
    this.self = self;
    this.uavEnemies = uavEnemies;
    this.cursor = { x: self.x, z: self.z };
    this.heading = self.yaw;
    this.active = true;
    this.canvas.style.display = 'block';
  }

  close(): void {
    this.active = false;
    this.canvas.style.display = 'none';
  }

  updateSelf(self: { x: number; z: number; yaw: number }): void {
    this.self = self;
  }

  moveCursor(dxPx: number, dyPx: number): void {
    if (!this.active) return;
    const worldPerPx = this.map.size / this.canvas.width;
    this.cursor.x = clampToMap(this.cursor.x + dxPx * worldPerPx, this.map.size);
    this.cursor.z = clampToMap(this.cursor.z + dyPx * worldPerPx, this.map.size);
  }

  rotate(dir: 1 | -1): void {
    if (!this.active) return;
    this.heading += dir * HEADING_STEP;
  }

  confirm(): PlacementResult {
    const r: PlacementResult = { streak: this.tier, streakTarget: { x: this.cursor.x, z: this.cursor.z }, streakYaw: this.heading };
    this.close();
    return r;
  }

  render(): void {
    if (!this.active) return;
    if (!this.ctx) this.ctx = this.canvas.getContext('2d');
    const ctx = this.ctx;
    if (!ctx) return;
    const size = this.canvas.width;
    const world = this.map.size;
    const s = size / world;
    ctx.clearRect(0, 0, size, size);
    ctx.fillStyle = 'rgba(12,16,20,0.92)';
    ctx.fillRect(0, 0, size, size);
    for (const c of this.map.covers) {
      ctx.fillStyle = COVER_COLORS[c.type] ?? '#555';
      ctx.fillRect((c.pos[0] + world / 2) * s - (c.size[0] * s) / 2, (c.pos[2] + world / 2) * s - (c.size[2] * s) / 2, c.size[0] * s, c.size[2] * s);
    }
    const px = (x: number) => (x + world / 2) * s;
    // UAV 敌人
    for (const e of this.uavEnemies) {
      ctx.fillStyle = 'rgba(255,70,60,0.95)';
      ctx.beginPath();
      ctx.arc(px(e.x), px(e.z), 4, 0, Math.PI * 2);
      ctx.fill();
    }
    // 自己（白色箭头，指向朝向）
    const sx = px(this.self.x);
    const sz = px(this.self.z);
    ctx.save();
    ctx.translate(sx, sz);
    ctx.rotate(-this.self.yaw);
    ctx.fillStyle = '#e8f4ff';
    ctx.beginPath();
    ctx.moveTo(0, -8);
    ctx.lineTo(6, 7);
    ctx.lineTo(-6, 7);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
    // 落点预览
    const cx = px(this.cursor.x);
    const cz = px(this.cursor.z);
    ctx.strokeStyle = '#ff5040';
    ctx.lineWidth = 2;
    if (this.tier === 2) {
      const dx = -Math.sin(this.heading);
      const dz = -Math.cos(this.heading);
      for (let i = 0; i < AIRSTRIKE_COUNT; i++) {
        const off = (i - (AIRSTRIKE_COUNT - 1) / 2) * AIRSTRIKE_SPACING;
        ctx.beginPath();
        ctx.arc(px(this.cursor.x + dx * off), px(this.cursor.z + dz * off), 4, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.beginPath();
      ctx.moveTo(cx - dx * 40 * s, cz - dz * 40 * s);
      ctx.lineTo(cx + dx * 40 * s, cz + dz * 40 * s);
      ctx.setLineDash([6, 6]);
      ctx.stroke();
      ctx.setLineDash([]);
    } else {
      ctx.beginPath();
      ctx.arc(cx, cz, CLUSTER_SCATTER * s, 0, Math.PI * 2);
      ctx.stroke();
    }
    // 光标
    ctx.strokeStyle = '#ffd60a';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx - 10, cz);
    ctx.lineTo(cx + 10, cz);
    ctx.moveTo(cx, cz - 10);
    ctx.lineTo(cx, cz + 10);
    ctx.stroke();
  }
}
```

- [ ] **Step 4: 运行确认通过**

Run: `npx vitest run client/test/placement.test.ts`
Expected: PASS（构造器不触碰 getContext/DOM 渲染，node 环境安全）。

- [ ] **Step 5: 提交**

```bash
git add client/src/placement.ts client/src/render/minimap.ts client/test/placement.test.ts
git commit -m "✨ feat(codcopy): add tactical map placement module for streak targeting"
```

---

### Task 5: InputSystem 放置模式输入状态机

**Files:**
- Modify: `client/src/input.ts`

- [ ] **Step 1: 新增状态字段**

`InputSystem` 类中 `private pendingStreak = 0;`（75 行）后追加：

```ts
  /** [M15] 放置模式：打开请求（0=无，2/3=tier，main 轮询）；激活期间鼠标增量/滚轮/Q-E 转发给俯图 */
  placementRequest = 0;
  placementActive = false;
  private uiDX = 0;
  private uiDY = 0;
  private wheelSteps = 0;
  private confirmPlacement = false;
  private cancelPlacement = false;
  private pendingStreakTarget: { x: number; z: number } | null = null;
  private pendingStreakYaw = 0;
```

- [ ] **Step 2: 改键鼠事件路由**

keydown（79-88 行）改为（5/6 不再直接激活；放置中 Q/E 旋转航线）：

```ts
    document.addEventListener('keydown', (e) => {
      this.keys.add(e.code);
      if (e.code === 'Space' || e.code === 'Tab') e.preventDefault();
      if (e.code === 'Digit1') this.pendingSlot = 1;
      if (e.code === 'Digit2') this.pendingSlot = 2;
      if (e.code === 'Digit4') this.pendingStreak = 1;
      if (e.code === 'Digit5') this.placementRequest = 2;
      if (e.code === 'Digit6') this.placementRequest = 3;
      if (this.placementActive && e.code === 'KeyQ') this.wheelSteps -= 1;
      if (this.placementActive && e.code === 'KeyE') this.wheelSteps += 1;
      if (e.code === 'KeyQ' && this.qSwapSlot > 0 && !this.placementActive) this.pendingSlot = this.qSwapSlot;
    });
```

mousedown（90-94 行）与 wheel（100-104 行）、mousemove（105-111 行）改为：

```ts
    document.addEventListener('mousedown', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        if (e.button === 0) this.confirmPlacement = true;
        if (e.button === 2) this.cancelPlacement = true;
        return;
      }
      if (e.button === 0) this.fireHeld = true;
      if (e.button === 2) this.adsHeld = true;
    });
    document.addEventListener('wheel', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        this.wheelSteps += Math.sign(e.deltaY);
        return;
      }
      void e.deltaY;
      this.pendingSlot = this.lastSlotSent === 1 ? 2 : 1;
    });
    document.addEventListener('mousemove', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        this.uiDX += e.movementX;
        this.uiDY += e.movementY;
        return;
      }
      const sens = this.settings.sensitivity * (this.ads ? this.settings.adsSens : 1);
      this.yaw -= e.movementX * sens;
      this.pitch -= e.movementY * sens;
      this.clampPitch();
    });
```

- [ ] **Step 3: 新增方法与 buildInput 门控**

`requestStreak`（132-134 行）后追加：

```ts
  /** [M15] 俯图确认后携带落点激活 */
  requestStreakTargeted(streak: 2 | 3, target: { x: number; z: number }, yaw: number): void {
    this.pendingStreak = streak;
    this.pendingStreakTarget = target;
    this.pendingStreakYaw = yaw;
  }

  /** [M15] 放置模式帧增量（main 每帧轮询后清零） */
  drainPlacementDeltas(): { dx: number; dy: number; wheel: number; confirm: boolean; cancel: boolean } {
    const r = { dx: this.uiDX, dy: this.uiDY, wheel: this.wheelSteps, confirm: this.confirmPlacement, cancel: this.cancelPlacement };
    this.uiDX = 0;
    this.uiDY = 0;
    this.wheelSteps = 0;
    this.confirmPlacement = false;
    this.cancelPlacement = false;
    return r;
  }
```

`buildInput`（169-203 行）：`buttons` 组装完成后、`slot` 逻辑前插入门控，`slot` 与 streak 发射改为：

```ts
    if (this.placementActive) buttons &= BTN.JUMP | BTN.CROUCH | BTN.SPRINT;
    const slot = this.placementActive ? 0 : this.pendingSlot;
    if (slot > 0 && slot !== this.lastSlotSent) {
      this.qSwapSlot = this.lastSlotSent;
      this.lastSlotSent = slot;
    }
    this.pendingSlot = 0;
    this.scoreboard = k.has('Tab') || pad.scoreboard;
    const streak = this.pendingStreak;
    const streakTarget = this.pendingStreakTarget ?? undefined;
    const streakYaw = this.pendingStreakTarget ? this.pendingStreakYaw : undefined;
    this.pendingStreak = 0;
    this.pendingStreakTarget = null;
    this.pendingStreakYaw = 0;
    return { seq, moveX, moveZ, yaw: this.yaw + swayYaw, pitch: this.pitch + swayPitch, buttons, slot, streak, streakTarget, streakYaw };
```

- [ ] **Step 4: 类型检查**

Run: `npm run typecheck -w client`
Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add client/src/input.ts
git commit -m "✨ feat(codcopy): route streak placement input through InputSystem ui mode"
```

---

### Task 6: main.ts 集成放置模式 + DOM/CSS

**Files:**
- Modify: `client/src/main.ts`、`client/src/placement.ts`（hint 提示条）、`client/index.html`、`client/src/style.css`

- [ ] **Step 1: main.ts 接线**

导入块（3-29 行）shared 导入追加 `TICK_RATE, UAV_DURATION_TICKS`（保持字母序）；`import { Minimap, ... }` 后加：

```ts
import { Placement } from './placement';
```

`const minimap = new Minimap(MAPS.warehouse);`（57 行）后加：

```ts
const placement = new Placement(MAPS.warehouse, document.getElementById('tacmap') as HTMLCanvasElement);
```

`showPause()`（119 行）开头加：

```ts
  if (input.placementActive) {
    placement.close();
    input.placementActive = false;
  }
```

手柄连杀（778 行 `if (pad.streak > 0) input.requestStreak(pad.streak);`）改为：

```ts
  if (pad.streak > 0) {
    if (pad.streak === 1) input.requestStreak(1);
    else input.placementRequest = pad.streak as 2 | 3;
  }
```

`gamepad.markPrev(pad);`（797 行）后插入放置模式轮询：

```ts
  // [M15] 连杀放置模式：5/6 打开俯图（需对应奖励就绪）
  const reqTier = input.placementRequest;
  input.placementRequest = 0;
  if (reqTier >= 2 && gameState === 'playing' && selfSnap?.a && !kcReplay) {
    const svMask = selfSnap.sv ?? 0;
    if (svMask & (reqTier === 2 ? 2 : 4)) {
      const uavActive = nowMs2 < uavUntilLocal;
      const st0 = predictor.state;
      placement.open(
        reqTier as 2 | 3,
        { x: st0.x, z: st0.z, yaw: input.yaw },
        uavActive ? lastPlayers.filter((p) => p.id !== selfId && p.a).map((p) => ({ x: p.x, z: p.z })) : [],
      );
      input.placementActive = true;
    }
  }
  if (input.placementActive) {
    const d = input.drainPlacementDeltas();
    if (d.wheel !== 0) placement.rotate(d.wheel > 0 ? 1 : -1);
    placement.moveCursor(d.dx, d.dy);
    if (d.cancel) {
      placement.close();
      input.placementActive = false;
    } else if (d.confirm) {
      const r = placement.confirm();
      input.placementActive = false;
      input.requestStreakTargeted(r.streak, r.streakTarget, r.streakYaw);
    } else {
      const st1 = predictor.state;
      placement.updateSelf({ x: st1.x, z: st1.z, yaw: input.yaw });
    }
    placement.render();
  }
```

kill 事件 `if (e.victimId === selfId) {`（457 行）块首加：

```ts
      if (input.placementActive) {
        placement.close();
        input.placementActive = false;
      }
```

- [ ] **Step 2: DOM 与样式**

`client/index.html` `<canvas id="minimap" ...>` 后加：

```html
      <canvas id="tacmap" width="520" height="520"></canvas>
      <div id="tacmaphint">左键确认投放 · 右键取消 · 滚轮/Q/E 旋转航线</div>
```

`client/src/style.css` 末尾追加：

```css
#tacmap {
  position: absolute;
  left: 50%;
  top: 50%;
  transform: translate(-50%, -50%);
  display: none;
  border: 2px solid rgba(255, 201, 77, 0.7);
  border-radius: 8px;
  box-shadow: 0 0 40px rgba(0, 0, 0, 0.85);
  z-index: 30;
}
#tacmaphint {
  position: absolute;
  left: 50%;
  bottom: 18%;
  transform: translateX(-50%);
  display: none;
  color: #ffc94d;
  font-size: 14px;
  background: rgba(12, 16, 20, 0.85);
  padding: 6px 14px;
  border-radius: 6px;
  z-index: 31;
}
```

`client/src/placement.ts` 的 `open()` 末尾与 `close()` 末尾分别追加提示条开关：

```ts
    (document.getElementById('tacmaphint') as HTMLElement | null)?.style.setProperty('display', 'block');
```

```ts
    (document.getElementById('tacmaphint') as HTMLElement | null)?.style.setProperty('display', 'none');
```

- [ ] **Step 3: 验证**

Run: `npm run typecheck -w client && npx vitest run client/ && npm run build -w client`
Expected: 全 PASS（build 产物含新模块）。

- [ ] **Step 4: 提交**

```bash
git add client/src/main.ts client/src/placement.ts client/index.html client/src/style.css
git commit -m "✨ feat(codcopy): wire tactical map placement into game loop and hud"
```

---

### Task 7: Incoming 全套反馈（呼啸音/红烟/预警圈 + UAV 常量清理）

**Files:**
- Modify: `client/src/render/minimap.ts`、`client/src/render/effects.ts`、`client/src/audio.ts`、`client/src/main.ts:547-562`

- [ ] **Step 1: 小地图预警圈**

`Minimap` 类加字段与方法，`render()` 中 uavEnemies 循环后绘制：

```ts
  private warnings: { x: number; z: number; until: number }[] = [];

  addStrikeWarning(x: number, z: number, until: number): void {
    this.warnings.push({ x, z, until });
  }
```

```ts
    this.warnings = this.warnings.filter((w) => now <= w.until);
    for (const w of this.warnings) {
      const k = 0.55 + 0.45 * Math.sin(now / 120);
      ctx.strokeStyle = `rgba(255,64,48,${k.toFixed(2)})`;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc((w.x + world / 2) * s, (w.z + world / 2) * s, 6 + 2 * Math.sin(now / 120), 0, Math.PI * 2);
      ctx.stroke();
    }
```

- [ ] **Step 2: 红烟标记 FX**

`effects.ts` `explosion()` 后加：

```ts
  /** [M15] 空袭/集束落点红烟标记（持续约 3s 的上升烟柱） */
  smokeMarker(pos: THREE.Vector3): void {
    for (let i = 0; i < 42; i++) {
      this.spawn(pos.clone().add(new THREE.Vector3((Math.random() - 0.5) * 1.6, Math.random() * 0.4, (Math.random() - 0.5) * 1.6)), {
        color: Math.random() < 0.7 ? 0xd8382f : 0x8a1f18,
        size: 0.45,
        vel: new THREE.Vector3((Math.random() - 0.5) * 0.8, 1.6 + Math.random() * 1.2, (Math.random() - 0.5) * 0.8),
        life: 2.6 + Math.random() * 0.8,
        gravity: -0.6,
        grow: 2.2,
      });
    }
  }
```

- [ ] **Step 3: 爆炸视觉增强**

`effects.ts` `explosion()`（328-342 行）粒子加大加密：两个火球 `size` 0.8→1.1、0.4→0.6，`grow` 9→11、12→14；烟尘循环 `i < 10` → `i < 14`，`life` 1.1→1.3。`main.ts` blast 分支震动半径（536 行 `if (dist < 10)` 与 537 行 `0.45 * (1 - dist / 10)`）改为 `dist < 18` 与 `0.45 * (1 - dist / 18)`。

- [ ] **Step 4: 喷气机呼啸音**

`audio.ts` `AudioSys` 加方法（`noise` 沿线路径扫掠 + 低频轰鸣）：

`audio.ts` `AudioSys` 加方法（`noise` 沿线路径扫掠 + 低频轰鸣）：

```ts
  /** [M15] 喷气机呼啸掠过（空袭/集束 incoming，所有客户端 3D 可闻） */
  jetFlyby(x: number, z: number, yaw: number): void {
    if (!this.ctx || !this.master) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const dx = -Math.sin(yaw);
    const dz = -Math.cos(yaw);
    const panner = ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    panner.refDistance = 12;
    panner.maxDistance = 160;
    panner.rolloffFactor = 0.9;
    panner.positionY.value = 8;
    panner.positionX.setValueAtTime(x - dx * 40, t);
    panner.positionZ.setValueAtTime(z - dz * 40, t);
    panner.positionX.linearRampToValueAtTime(x + dx * 40, t + 1.6);
    panner.positionZ.linearRampToValueAtTime(z + dz * 40, t + 1.6);
    panner.connect(this.master);
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const bp = ctx.createBiquadFilter();
    bp.type = 'bandpass';
    bp.Q.value = 1.1;
    bp.frequency.setValueAtTime(2400, t);
    bp.frequency.exponentialRampToValueAtTime(420, t + 1.6);
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(0.5, t + 0.5);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 1.7);
    src.connect(bp).connect(g).connect(panner);
    src.start(t);
    src.stop(t + 1.75);
    this.thump(panner, t + 0.4, 75, 1.1, 0.28);
  }
```

- [ ] **Step 5: streakUse 处理改写 + UAV 常量**

`main.ts` streakUse 分支（547-562 行）整体替换：

```ts
  } else if (e.type === 'streakUse') {
    if (e.playerId === selfId && e.tier === 1) {
      uavUntilLocal = nowMs2 + (UAV_DURATION_TICKS / TICK_RATE) * 1000;
      hud.showKill('UAV 已启动：敌人位置已标记');
      audio.playLocal('uav');
    } else if (e.playerId === selfId) {
      hud.showKill(e.tier === 2 ? '精准空袭已呼叫！' : '集束炸弹已投放！');
    } else if (e.tier === 1) {
      hud.showKill('⚠ 敌方 UAV 已启动');
      audio.playLocal('uav');
      audio.announce('警告，敌方无人侦察机已升空');
    }
    if (e.tier !== 1 && e.target) {
      audio.jetFlyby(e.target.x, e.target.z, e.yaw ?? 0);
      fx.smokeMarker(new THREE.Vector3(e.target.x, 0.8, e.target.z));
      minimap.addStrikeWarning(e.target.x, e.target.z, nowMs2 + 3000);
    }
  }
```

- [ ] **Step 6: 验证并提交**

Run: `npm run typecheck -w client && npx vitest run client/ && npm run build -w client`
Expected: 全 PASS。

```bash
git add client/src/render/minimap.ts client/src/render/effects.ts client/src/audio.ts client/src/main.ts
git commit -m "✨ feat(codcopy): full incoming feedback for strikes (jet flyby, smoke marker, minimap ring)"
```

---

### Task 8: 音频五层合成引擎 + MW 风命中音（TDD，上）

**Files:**
- Modify: `client/src/audio.ts`
- Test: `client/test/audio-params.test.ts`（新建）

- [ ] **Step 1: 写失败测试**

新建 `client/test/audio-params.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import { SHOT_PARAMS, distanceCutoff } from '../src/audio';

const IDS = ['ar_shot', 'smg_shot', 'lmg_shot', 'dmr_shot', 'sg_shot', 'sr_shot', 'pistol_shot'] as const;

describe('[M15] shot synth params', () => {
  it('every weapon has a complete, finite, positive layer set', () => {
    for (const id of IDS) {
      for (const [k, v] of Object.entries(SHOT_PARAMS[id])) {
        expect(Number.isFinite(v), `${id}.${k}`).toBe(true);
        expect(v as number, `${id}.${k}`).toBeGreaterThan(0);
      }
    }
  });

  it('weapon layers differ: each pair differs in >=3 fields', () => {
    for (let i = 0; i < IDS.length; i++) {
      for (let j = i + 1; j < IDS.length; j++) {
        const a = SHOT_PARAMS[IDS[i]];
        const b = SHOT_PARAMS[IDS[j]];
        const diff = Object.keys(a).filter((k) => a[k as keyof typeof a] !== b[k as keyof typeof b]);
        expect(diff.length, `${IDS[i]} vs ${IDS[j]}`).toBeGreaterThanOrEqual(3);
      }
    }
  });

  it('distanceCutoff falls with distance and clamps to [500, 8000]', () => {
    expect(distanceCutoff(0)).toBeGreaterThan(distanceCutoff(40));
    expect(distanceCutoff(40)).toBeGreaterThan(distanceCutoff(80));
    expect(distanceCutoff(0)).toBeLessThanOrEqual(8000);
    expect(distanceCutoff(1000)).toBe(500);
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run client/test/audio-params.test.ts`
Expected: FAIL——`SHOT_PARAMS`/`distanceCutoff` 未导出。

- [ ] **Step 3: 参数表与纯函数**

`client/src/audio.ts` 的 `SoundKind` 定义后追加：

```ts
/** [M15] 五层枪声参数：crack 高频裂响 / body 中频主体 / sub 低频砰 / tail 混响湿度 / mech 枪机声 */
export interface ShotParams {
  crackHz: number;
  crackDur: number;
  crackGain: number;
  bodyHz: number;
  bodyDur: number;
  bodyGain: number;
  subHz: number;
  subDur: number;
  subGain: number;
  tailGain: number;
  mechGain: number;
  mechHz: number;
}

export const SHOT_PARAMS: Record<'ar_shot' | 'smg_shot' | 'lmg_shot' | 'dmr_shot' | 'sg_shot' | 'sr_shot' | 'pistol_shot', ShotParams> = {
  ar_shot:     { crackHz: 4200, crackDur: 0.05,  crackGain: 0.3,  bodyHz: 900,  bodyDur: 0.14, bodyGain: 0.28, subHz: 100, subDur: 0.06,  subGain: 0.25, tailGain: 0.5,  mechGain: 0.06, mechHz: 2800 },
  smg_shot:    { crackHz: 5200, crackDur: 0.035, crackGain: 0.24, bodyHz: 1100, bodyDur: 0.09, bodyGain: 0.2,  subHz: 130, subDur: 0.04,  subGain: 0.16, tailGain: 0.3,  mechGain: 0.05, mechHz: 3200 },
  lmg_shot:    { crackHz: 3200, crackDur: 0.07,  crackGain: 0.34, bodyHz: 650,  bodyDur: 0.2,  bodyGain: 0.38, subHz: 75,  subDur: 0.1,   subGain: 0.34, tailGain: 0.8,  mechGain: 0.12, mechHz: 1800 },
  dmr_shot:    { crackHz: 3800, crackDur: 0.06,  crackGain: 0.34, bodyHz: 700,  bodyDur: 0.16, bodyGain: 0.32, subHz: 85,  subDur: 0.09,  subGain: 0.3,  tailGain: 0.7,  mechGain: 0.1,  mechHz: 2400 },
  sg_shot:     { crackHz: 2400, crackDur: 0.09,  crackGain: 0.4,  bodyHz: 450,  bodyDur: 0.26, bodyGain: 0.42, subHz: 55,  subDur: 0.14,  subGain: 0.4,  tailGain: 0.9,  mechGain: 0.08, mechHz: 1600 },
  sr_shot:     { crackHz: 3000, crackDur: 0.1,   crackGain: 0.42, bodyHz: 500,  bodyDur: 0.3,  bodyGain: 0.44, subHz: 60,  subDur: 0.16,  subGain: 0.42, tailGain: 1,    mechGain: 0.1,  mechHz: 2000 },
  pistol_shot: { crackHz: 4600, crackDur: 0.04,  crackGain: 0.22, bodyHz: 1000, bodyDur: 0.08, bodyGain: 0.18, subHz: 150, subDur: 0.035, subGain: 0.15, tailGain: 0.25, mechGain: 0.07, mechHz: 3000 },
};

/** [M15] 远距枪声低通：越远越闷，钳制 [500, 8000] */
export function distanceCutoff(dist: number): number {
  return Math.max(500, Math.min(8000, 8000 - dist * 90));
}
```

- [ ] **Step 4: 母带链（压缩器 + 合成混响）与距离低通**

`AudioSys` 字母段区（26-29 行）追加：

```ts
  private compressor: DynamicsCompressorNode | null = null;
  private convolver: ConvolverNode | null = null;
  private tailBus: GainNode | null = null;
  private listenerPos = { x: 0, y: 0, z: 0 };
```

`resume()` 中 `this.master.connect(this.ctx.destination);` 替换为：

```ts
      this.compressor = this.ctx.createDynamicsCompressor();
      this.compressor.threshold.value = -12;
      this.compressor.ratio.value = 6;
      this.master.connect(this.compressor).connect(this.ctx.destination);
      // [M15] 合成混响 IR：1.8s 指数衰减白噪（枪声 tail 层专用）
      const irLen = Math.floor(this.ctx.sampleRate * 1.8);
      const ir = this.ctx.createBuffer(2, irLen, this.ctx.sampleRate);
      for (let ch = 0; ch < 2; ch++) {
        const d = ir.getChannelData(ch);
        for (let i = 0; i < irLen; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / irLen, 2.6);
      }
      this.convolver = this.ctx.createConvolver();
      this.convolver.buffer = ir;
      this.tailBus = this.ctx.createGain();
      this.tailBus.gain.value = 0.32;
      this.tailBus.connect(this.convolver).connect(this.master);
```

`setListener()` 开头记录 `this.listenerPos = { x, y, z };`；`playAt()` 中 `panner.connect(this.master);` 替换为按距离串低通：

```ts
    const dist = Math.hypot(x - this.listenerPos.x, y - this.listenerPos.y, z - this.listenerPos.z);
    const lp = this.ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = distanceCutoff(dist);
    panner.connect(lp).connect(this.master);
```

- [ ] **Step 5: 新原语 snap（高通裂响）与 mech（枪机双击）**

`blip()` 方法后追加：

```ts
  private snap(dest: AudioNode, t: number, hz: number, dur: number, gain: number): void {
    const ctx = this.ctx!;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const hp = ctx.createBiquadFilter();
    hp.type = 'highpass';
    hp.frequency.value = hz;
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    src.connect(hp).connect(g).connect(dest);
    src.start(t);
    src.stop(t + dur + 0.02);
  }

  private mech(dest: AudioNode, t: number, hz: number, gain: number): void {
    this.blip(dest, t, hz, 0.018, gain, 'square');
    this.blip(dest, t + 0.028, hz * 0.72, 0.014, gain * 0.7, 'square');
  }
```

- [ ] **Step 6: 运行参数表测试确认通过**

Run: `npx vitest run client/test/audio-params.test.ts`
Expected: PASS。

---

### Task 8（续，下）：emit 枪声/命中音改写

- [ ] **Step 7: emit() 五层枪声**

`emit()` 中 7 个 `case '*_shot'`（111-138 行）整体替换为一个合并分支：

```ts
      case 'ar_shot':
      case 'smg_shot':
      case 'lmg_shot':
      case 'dmr_shot':
      case 'sg_shot':
      case 'sr_shot':
      case 'pistol_shot': {
        const p = SHOT_PARAMS[kind];
        this.snap(dest, t, p.crackHz, p.crackDur, p.crackGain);
        this.burst(dest, t, p.bodyHz, p.bodyDur, p.bodyGain);
        this.thump(dest, t, p.subHz, p.subDur, p.subGain);
        this.mech(dest, t + 0.01, p.mechHz, p.mechGain);
        if (this.tailBus && dest === this.master) {
          // 本地枪声送混响 tail（远端经距离低通已够闷）
          const send = this.ctx!.createGain();
          send.gain.value = p.tailGain * 0.28;
          this.burst(send, t, 1400, 0.12 + p.tailGain * 0.15, 0.5);
          send.connect(this.tailBus);
        }
        break;
      }
```

- [ ] **Step 8: MW 风命中/爆头与爆炸低频**

`hit`/`headshot` 分支（139-144 行）替换：

```ts
      case 'hit':
        this.thump(dest, t, 280, 0.05, 0.18);
        this.burst(dest, t, 600, 0.03, 0.08);
        break;
      case 'headshot':
        this.snap(dest, t, 2600, 0.06, 0.32);
        this.thump(dest, t, 180, 0.1, 0.4);
        break;
```

`explode` 分支（167-170 行）首行后加低频层：

```ts
        this.thump(dest, t, 45, 0.5, 0.4);
```

- [ ] **Step 9: 验证并提交**

Run: `npm run typecheck -w client && npx vitest run client/ && npm run build -w client`
Expected: 全 PASS。

```bash
git add client/src/audio.ts client/test/audio-params.test.ts
git commit -m "✨ feat(codcopy): five-layer gunshot synthesis, MW-style hit feedback, distance muffling"
```

---

### Task 9: FP 枪模 AR 收窄 / LMG 放大 + swayK（TDD，上）

**Files:**
- Modify: `client/src/render/viewmodel.ts`

- [ ] **Step 1: GunModel 加 swayK 并导出 BUILDERS**

`GunModel` 接口（45-55 行）`ejectZ: number;` 后加 `swayK: number;`；文件末尾 `const BUILDERS` 改 `export const BUILDERS`，`interface GunModel` 改 `export interface GunModel`。7 个 builder 的返回对象统一追加 `swayK`（数值：ar 1、smg 1.15、sg 0.9、dmr 0.85、lmg 0.6、sr 0.55、pistol 1.3）。

`update()` 摇摆/走路晃动按 swayK 缩放（146-156 行）：

```ts
    const swayTargetX = THREE.MathUtils.clamp(-o.lookDX * 0.012 * gun.swayK, -0.03, 0.03);
    const swayTargetY = THREE.MathUtils.clamp(o.lookDY * 0.01 * gun.swayK, -0.02, 0.02);
```

```ts
    pos.x += (Math.sin(this.bobT) * 0.012 * gun.swayK + this.swayX) * bobK + Math.sin(this.idleT * 1.2) * 0.0016;
    pos.y += (-Math.abs(Math.cos(this.bobT)) * 0.01 * gun.swayK + this.swayY) * bobK + Math.cos(this.idleT * 1.7) * 0.0012;
```

- [ ] **Step 2: buildAr 收窄（纤细卡宾）**

`buildAr` 内逐项替换尺寸（行号为改前）：

| 行 | 旧 | 新 |
|---|---|---|
| 416 | `box(0.048, 0.052, 0.26, M.polymer, 0, 0, 0.02)` | `box(0.042, 0.046, 0.24, M.polymer, 0, 0, 0.02)` |
| 417 | `box(0.052, 0.046, 0.3, M.gunMetal, 0, 0.042, -0.02)` | `box(0.046, 0.04, 0.28, M.gunMetal, 0, 0.042, -0.02)` |
| 431 | `CylinderGeometry(0.026, 0.026, 0.24, 8)` | `CylinderGeometry(0.023, 0.023, 0.24, 8)` |
| 436 | `box(0.056, 0.007, 0.05, ...)` | `box(0.05, 0.007, 0.05, ...)` |
| 437/438 | `box(0.007, 0.03, 0.05, ..., ±0.027, ...)` | `box(0.006, 0.028, 0.05, ..., ±0.024, ...)` |
| 444 | `cyl(0.008, 0.008, 0.2, ...)` | `cyl(0.007, 0.007, 0.2, ...)` |

- [ ] **Step 3: buildLmg 放大（弹链机枪）**

`buildLmg` 内逐项替换（行号为改前）：

| 行 | 旧 | 新 |
|---|---|---|
| 589 | `box(0.056, 0.066, 0.3, ...)` | `box(0.064, 0.078, 0.34, ...)` |
| 590 | `box(0.05, 0.03, 0.26, ...)` | `box(0.056, 0.034, 0.3, ...)` |
| 605 | `cyl(0.013, 0.011, 0.34, ..., -0.26, ...)` | `cyl(0.015, 0.013, 0.38, ..., -0.28, ...)` |
| 606 | `cyl(0.015, 0.015, 0.07, ..., -0.46, ...)` | `cyl(0.018, 0.018, 0.08, ..., -0.5, ...)` |
| 608 | `cyl(0.013, 0.013, 0.008, ..., -0.5, ...)` | `cyl(0.014, 0.014, 0.008, ..., -0.54, ...)` |
| 611/613 | `cyl(0.004, 0.003, 0.12, ..., -0.44, ...)` | `cyl(0.004, 0.003, 0.17, ..., -0.48, ...)` |
| 629 | `box(0.07, 0.075, 0.1, M.polymer, 0, -0.06, 0.06, 0.008)` | `box(0.088, 0.094, 0.13, M.polymer, 0, -0.09, 0.06, 0.008)` |
| 630 | `box(0.074, 0.012, 0.104, ..., -0.1, ...)` | `box(0.092, 0.014, 0.134, ..., -0.137, ...)` |
| 631 | `box(0.02, 0.05, 0.02, ..., 0.037, ...)` | `box(0.024, 0.06, 0.024, ..., 0.046, ...)` |
| 642 | `box(0.04, 0.06, 0.16, ...)` | `box(0.046, 0.07, 0.18, ...)` |
| 650 | `muzzle: new THREE.Vector3(0, 0.02, -0.51)` | `muzzle: new THREE.Vector3(0, 0.02, -0.55)` |

`mag` 组装后（632 行 `root.add(mag);` 后）加外露弹链：

```ts
  // [M15] 外露弹链（黄铜链节，从弹箱爬入机匣右侧）
  for (let i = 0; i < 3; i++) {
    const link = box(0.018, 0.011, 0.03, M.brass, 0.05, -0.118 + i * 0.028, 0.075, 0.003);
    link.rotation.z = -0.5 + i * 0.25;
    root.add(link);
  }
```

- [ ] **Step 4: SR 制退器加大 / SMG 收紧微调**

`buildSr` 制退器组（345-348 行）：`cyl(0.019, 0.019, 0.05` → `cyl(0.024, 0.024, 0.06`；`box(0.04, 0.008, 0.012` → `box(0.056, 0.01, 0.014`；两块 `box(0.008, 0.026, 0.012` → `box(0.01, 0.034, 0.014`。`buildSmg`：机匣（511 行）`box(0.044, 0.05, 0.2` → `box(0.04, 0.046, 0.18`；枪管（521 行）`cyl(0.009, 0.009, 0.14` → `cyl(0.008, 0.008, 0.12`。

- [ ] **Step 5: 类型检查**

Run: `npm run typecheck -w client`
Expected: PASS（swayK 缺失会在此暴露，逐个 builder 补齐）。

---

### Task 9（续，下）：包围盒回归测试

- [ ] **Step 5: 写包围盒断言测试**

新建 `client/test/viewmodel.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { WEAPON_LIST } from 'shared';
import { BUILDERS } from '../src/render/viewmodel';

const guns = WEAPON_LIST.map((w) => ({ w, gun: BUILDERS[w]() }));
const idx = (w: string) => guns.findIndex((g) => g.w === w);
const bboxOf = (w: string) => {
  const gun = guns[idx(w)].gun;
  gun.root.updateMatrixWorld(true);
  return new THREE.Box3().setFromObject(gun.root);
};

describe('[M15] viewmodel silhouettes', () => {
  it('lmg is decisively wider than every other weapon', () => {
    const lmgW = bboxOf('lmg').max.x - bboxOf('lmg').min.x;
    for (const g of guns) {
      if (g.w === 'lmg') continue;
      const bw = bboxOf(g.w).max.x - bboxOf(g.w).min.x;
      expect(lmgW, `lmg(${lmgW.toFixed(3)}) vs ${g.w}(${bw.toFixed(3)})`).toBeGreaterThan(bw * 1.2);
    }
  });

  it('muzzle length ordering: pistol < smg < ar < dmr < sr', () => {
    const mz = (w: string) => -guns[idx(w)].gun.muzzle.z;
    expect(mz('pistol')).toBeLessThan(mz('smg'));
    expect(mz('smg')).toBeLessThan(mz('ar'));
    expect(mz('ar')).toBeLessThan(mz('dmr'));
    expect(mz('dmr')).toBeLessThan(mz('sr'));
  });

  it('every gun defines swayK in (0, 2]', () => {
    for (const g of guns) {
      expect(g.gun.swayK).toBeGreaterThan(0);
      expect(g.gun.swayK).toBeLessThanOrEqual(2);
    }
  });
});
```

- [ ] **Step 6: 运行测试**

Run: `npx vitest run client/test/viewmodel.test.ts`
Expected: PASS。若 `lmg vs 其他` 断言失败，检查 Step 3 的弹箱/弹链尺寸是否漏改。

- [ ] **Step 7: 验证并提交**

Run: `npm run typecheck -w client && npx vitest run client/ && npm run build -w client`
Expected: 全 PASS。

```bash
git add client/src/render/viewmodel.ts client/test/viewmodel.test.ts
git commit -m "✨ feat(codcopy): exaggerate ar/lmg silhouettes with per-weapon sway weight"
```

---

### Task 10: 第三人称 gunFor 升级（TDD）

**Files:**
- Modify: `client/src/render/actors.ts`（GEO 扩充 + gunFor 重写并导出）
- Test: `client/test/actors-guns.test.ts`（新建）

- [ ] **Step 1: 写失败测试**

新建 `client/test/actors-guns.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { WEAPON_LIST } from 'shared';
import { gunFor } from '../src/render/actors';

const mat = new THREE.MeshBasicMaterial();

describe('[M15] third-person gun silhouettes', () => {
  it('pistol has >=3 parts, long guns >=5, lmg >=6', () => {
    for (const w of WEAPON_LIST) {
      const n = gunFor(w, mat).children.length;
      if (w === 'pistol') expect(n, w).toBeGreaterThanOrEqual(3);
      else if (w === 'lmg') expect(n, w).toBeGreaterThanOrEqual(6);
      else expect(n, w).toBeGreaterThanOrEqual(5);
    }
  });

  it('lmg carries the drum and bipod signature parts', () => {
    const names = gunFor('lmg', mat).children.map((c) => (c as THREE.Mesh).geometry.uuid);
    expect(new Set(names).size).toBeGreaterThanOrEqual(5);
  });
});
```

- [ ] **Step 2: 运行确认失败**

Run: `npx vitest run client/test/actors-guns.test.ts`
Expected: FAIL——`gunFor` 未导出，且现 lmg 仅 4 件。

- [ ] **Step 3: 扩充 GEO 并重写 gunFor**

`GEO` 对象（63-87 行）`gunPump` 行后追加：

```ts
  gunDrum: new THREE.BoxGeometry(0.09, 0.1, 0.11),
  gunBarrel: new THREE.BoxGeometry(0.028, 0.028, 0.36),
  gunBipod: new THREE.BoxGeometry(0.012, 0.16, 0.012),
  gunWire: new THREE.BoxGeometry(0.01, 0.01, 0.22),
  gunSlide: new THREE.BoxGeometry(0.038, 0.028, 0.15),
```

`gunFor`（89-131 行）改为 `export function gunFor` 并整体替换布局：

```ts
export function gunFor(w: WeaponId, mat: THREE.Material): THREE.Group {
  const g = new THREE.Group();
  const add = (geo: THREE.BufferGeometry, x: number, y: number, z: number) => {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x, y, z);
    m.castShadow = true;
    g.add(m);
  };
  if (w === 'ar') {
    add(GEO.gunBody, 0, 0, -0.06);
    add(GEO.gunMag, 0, -0.09, 0.02);
    add(GEO.gunStock, 0, -0.01, 0.26);
    add(GEO.gunScope, 0, 0.07, 0.04);
    add(GEO.gunBarrel, 0, 0.01, -0.36);
  } else if (w === 'sg') {
    add(GEO.gunBody, 0, 0.01, -0.08);
    add(GEO.gunPump, 0, -0.05, -0.14);
    add(GEO.gunStock, 0, -0.02, 0.24);
    add(GEO.gunBarrel, 0, 0.035, -0.3);
    add(GEO.gunScope, 0, 0.05, 0.05);
  } else if (w === 'sr') {
    add(GEO.gunBody, 0, 0.01, -0.16);
    add(GEO.gunMag, 0, -0.08, -0.02);
    add(GEO.gunStock, 0, -0.01, 0.3);
    add(GEO.gunScope, 0, 0.09, -0.04);
    add(GEO.gunBarrel, 0, 0.01, -0.52);
  } else if (w === 'smg') {
    add(GEO.gunBody, 0, 0, -0.02);
    add(GEO.gunMag, 0, -0.1, 0.04);
    add(GEO.gunWire, 0.014, 0.02, 0.24);
    add(GEO.gunWire, -0.014, 0.02, 0.24);
    add(GEO.gunScope, 0, 0.06, 0);
  } else if (w === 'lmg') {
    add(GEO.gunBody, 0, 0, -0.1);
    add(GEO.gunDrum, 0, -0.11, 0.02);
    add(GEO.gunBarrel, 0, 0.01, -0.42);
    add(GEO.gunBipod, 0.024, -0.1, -0.3);
    add(GEO.gunBipod, -0.024, -0.1, -0.3);
    add(GEO.gunStock, 0, -0.01, 0.28);
    add(GEO.gunScope, 0, 0.07, 0.06);
  } else if (w === 'dmr') {
    add(GEO.gunBody, 0, 0.01, -0.12);
    add(GEO.gunMag, 0, -0.07, 0);
    add(GEO.gunStock, 0, -0.01, 0.28);
    add(GEO.gunScope, 0, 0.08, -0.02);
    add(GEO.gunBarrel, 0, 0.01, -0.4);
  } else {
    add(GEO.gunBody, 0, 0.01, 0.08);
    add(GEO.gunMag, 0, -0.07, 0.12);
    add(GEO.gunSlide, 0, 0.045, 0.06);
  }
  return g;
}
```

- [ ] **Step 4: 运行测试通过并提交**

Run: `npx vitest run client/test/actors-guns.test.ts && npm run typecheck -w client`
Expected: PASS。

```bash
git add client/src/render/actors.ts client/test/actors-guns.test.ts
git commit -m "✨ feat(codcopy): distinct third-person gun silhouettes (drum, bipod, wire stock, barrels)"
```

---

### Task 11: 收尾验证与帮助文案

**Files:**
- Modify: `client/index.html`（开始界面操作说明）

- [ ] **Step 1: 更新操作说明**

`startoverlay` 内 `<p>` 的 `4/5/6 连杀奖励` 改为 `4 UAV · 5/6 打开战术地图放置空袭/集束`。

- [ ] **Step 2: 全量验证**

Run: `npm run typecheck && npx vitest run && npm run build -w client`
Expected: 三项全绿。

- [ ] **Step 3: 手动试玩清单（npm run dev）**

1. 3 杀 → UAV（按 4）：小地图持续 15s 标记敌人。
2. 5 杀 → 按 5 打开俯图：光标移动/滚轮旋转航线/左键确认/右键取消（不消耗）；确认后听到呼啸、看到红烟与小地图红圈，约 3s 后 5 连爆炸沿航线；击杀入账且连杀继续累积。
3. 6 杀 → 按 6 集束：8m 散布圆预览，投放后同上。
4. 站在自己落点中心：不被自己的炸弹炸死；敌人在落点死亡。
5. 放置模式中：可移动、不能开枪/切枪；死亡自动关闭俯图。
6. 7 把枪逐把试玩：AR/LMG 轮廓一眼可辨（弹箱+弹链+两脚架 vs 纤细卡宾）；远/近枪声听感区分；爆头 crack+thunk vs 身体 thud 明显不同。

- [ ] **Step 4: 提交**

```bash
git add client/index.html
git commit -m "📝 docs(codcopy): update help text for streak placement keys"
```

---

## 任务依赖与执行顺序

Task 1 → 2 → 3（服务端链路，顺序执行）；Task 4 → 5 → 6 → 7（客户端放置与反馈，顺序执行）；Task 8（音频，独立）；Task 9（FP 枪模，独立）；Task 10（TP 枪模，独立）；Task 11 收尾。Task 8/9/10 可与 4-7 并行。











