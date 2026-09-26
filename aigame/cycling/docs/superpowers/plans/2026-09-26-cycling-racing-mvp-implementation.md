# 公路车竞速游戏 MVP 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `docs/superpowers/specs/2026-09-26-cycling-racing-mvp-design.md` 定义的 3D 公路车大组赛竞速 MVP：一局 5~8 分钟，功率档位 + 体力管理 + 跟车 + 终点冲刺，对抗 7 名 AI 车手。

**Architecture:** 仿真核心（物理/体力/赛道/AI/比赛规则）为纯 TypeScript 模块，固定步长（1/60s）确定性步进，不依赖渲染；Three.js 仅负责渲染，渲染层每帧对前后两个仿真状态插值；HUD 为 DOM 覆盖层。仿真与渲染通过纯数据结构 `RaceState` 解耦。

**Tech Stack:** TypeScript (strict) + Vite + Three.js（需 @types/three）+ Vitest（仅输入测试用 jsdom）+ 原生 DOM。无物理引擎、无后端。

---

## Part A 技术设计

### A1 总体架构与数据流

```
键盘 → InputController ──┐
                         ▼
        ┌────── stepRace(state, track, cmd, dt) ──────┐
        │  sim/（纯逻辑，确定性，可单测）             │
        │  physics.stepSpeed · energy.* · draft.*     │
        │  track.sampleAt · ai.aiCommand              │
        └──────────────────┬──────────────────────────┘
                           ▼  RaceState（prev / current 两份快照）
              core/loop（固定步长累加器 + 插值 alpha）
                           ▼
     render/*（Three.js：地形 / 路面 / 车手网格 / 追尾相机）
     ui/Hud（DOM：速度 / 档位 / 体力 / 坡度 / 名次 / 高程图 / 结算）
```

关键约束：

- `sim/` 内禁止 `Math.random()`、`Date.now()`、Three.js 依赖；随机数统一走 `sim/rng.ts`（状态为可序列化整数）。
- 渲染层每帧使用 `lerp(prev, curr, alpha)` 插值车手的 `dist`/`lateral`，物理与帧率解耦。
- 渲染层与 HUD 只读 `RaceState`，不含游戏逻辑。

### A2 与规格文档的偏差（已确认的决策）

| 项 | 规格 | 实现 | 原因 |
|---|---|---|---|
| 赛道长度 | 约 8km | 约 4.4km 闭环 | 8km 在真实功率-速度模型下需 11~13 分钟，与"一局 5~8 分钟"冲突；缩短赛道以同时满足时长与真实车速 |
| AI 数量 | 6~8 名 | 7 名（全场 8 人） | 足够体现跟车与战术，控制同屏开销 |
| HUD 技术 | 未指定 | DOM 覆盖层 | 已确认：开发/调试效率高，性能足够 |
| 物理步长 | 未指定 | 固定 1/60s | 已确认：确定性 → 单测可精确断言；P1 回放只需录输入 |
| 渲染层测试 | 未指定 | 不做单元测试 | WebGL 无法在 node 单测中运行；以 `npm run typecheck` + `npm run build` + 手动验收替代 |

### A3 骑行物理（sim/physics.ts）

功率平衡模型（简化自真实公式，纯函数）：

```
F_drive = P / max(v, 1)              # 驱动力（v→0 限幅防发散）
F_roll  = Crr · m · g · cosθ         # 滚动阻力
F_drag  = 0.5 · ρ · CdA · k · v²     # 空气阻力；k = 0.7（跟车时）否则 1
F_grav  = m · g · sinθ               # 坡度重力，θ = atan(gradient)
a = (F_drive − F_roll − F_drag − F_grav) / m
v' = max(0, v + a·dt)
```

参数（`sim/params.ts` 的 `PHYS`，全部可调）：`m = 78 kg`（人+车）、`Crr = 0.005`、`CdA = 0.32 m²`、`ρ = 1.226`、`draftDrag = 0.7`。

数值自检（平路稳态、无跟车）：300W → 10.95 m/s ≈ 39.4 km/h；750W → 15.5 m/s ≈ 56 km/h；6% 坡 300W → 5.4 m/s ≈ 19.4 km/h。符合公路车直觉，单测按这些区间断言。

### A4 体力与档位（sim/energy.ts）

- 4 档功率 = FTP × 比例：轻松 0.6 / 巡航 1.0 / 发力 1.5 / 冲刺 2.5。
- 消耗率 `drain(P) = (P/FTP)² × 30 W`（二次曲线：冲刺昂贵、巡航可持续）。
- 体力池 `maxEnergy ≈ 24000 J`：巡航 300W 消耗 30W → 约 800s 耗尽（比赛约 480s，留有余量）；冲刺 750W 消耗 187.5W → 全冲约 128s 后耗尽。
- 透支惩罚：`energy ≤ 0` 时功率上限 = 0.45 × FTP（约 135W，平路 ≈ 26 km/h 的"崩盘爬行"）。这是验收标准 3（无节制冲刺 → 掉名次）的机制来源。（实现期修正：原定 0.7×FTP ≈ 210W 惩罚过弱，单圈 4.5km 下全程冲刺反而快 23s；0.45 后实测 reckless 落后 steady 41s 且掉至全场末位。）

### A5 赛道（sim/track.ts + sim/trackData.ts）

- 闭合 Catmull-Rom 样条（24 个控制点，2400 个弧长采样点），`sampleAt(dist)` 二分查找返回 `{x, y, z, gradient, heading}`，逻辑与渲染共用同一份数据。
- 数据驱动高程：分段线性断点 `[u, 海拔]` → `[[0,0],[0.14,0],[0.26,28],[0.36,0],[0.58,0],[0.66,16],[0.74,0],[1,0]]`，即两段爬坡（约 5.3% / 4.4%）+ 平坦终点直道。
- 平面形状：半径 700m 基圆 + `sin/cos` 扰动，周长约 4.4km。
- 发车grid：8 人按 `dist = id × 1.6m` 纵向排开、横向交替 ±0.8m；玩家 id=0 在队尾（开局即体验跟车）。

### A6 跟车（sim/draft.ts）

- 判定窗口：任一其他车手在前方 `1m ≤ gap ≤ 15m`（沿赛道距离差）且横向差 `≤ 1.2m` → 视为跟车，风阻系数 ×0.7。不叠加（多车也只 ×0.7）。
- `nearestAheadIndex(self, riders)`：40m 内最近前车索引（无则 -1），供 AI 决策复用。

### A7 AI（sim/ai.ts）

无状态优先级决策（每 tick 纯函数求值，便于单测与确定性）：

```
1. 体力近空(e ≤ 0.02)      → 档位 0（保命）
2. 剩余距离 ≤ sprintDist 且 e > 0.12 → 档位 3（终点冲刺）
3. 前方 25m 内车速 > 自身+0.8 → 档位 2（响应进攻，横移跟上）
4. 坡度 > 4.5% 且 e > 0.5 且 aggression > 0.6 → 档位 2（爬坡型进攻）
5. 默认：e < 0.25 → 档位 0（省力），否则档位 1（巡航）
横移：跟随最近前车 lateral×0.8，否则小幅游走（wanderTarget，race 层用种子 RNG 更新）
```

车手分型（数据即差异，`AI_FIELD`）：

| 型 | FTP | maxEnergy | sprintDist | aggression |
|---|---|---|---|---|
| climber ×2 | 315 / 306 | 22000 / 22500 | 120 / 130 | 0.85 / 0.70 |
| sprinter ×2 | 286 / 293 | 26000 / 25500 | 350 / 300 | 0.40 / 0.50 |
| rouleur ×2 | 298 / 295 | 24000 / 24500 | 200 / 220 | 0.55 / 0.60 |
| all-rounder ×1 | 300 | 23500 | 240 | 0.50 |

玩家：`{ ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 }`。

### A8 比赛编排与确定性（sim/race.ts + core/loop.ts）

- 阶段机：`countdown`（3s，全员锁定）→ `racing` → `finished`。
- 冲线：`dist ≥ trackLength` 时按 `time + dt − 超出距离/speed` 回插精确成绩；之后车手冻结在终点线。
- 全员完赛或玩家完赛 30s 后 → 生成 `results`（名次 / 时间 / 均功率）。
- 确定性三要素：固定步长 dt、种子 RNG（`rngState` 存于 `RaceState`）、仿真中无任何非纯输入。同输入必得同结果（单测断言 JSON 相等），P1 回放 = 录制玩家按键。
- 主循环：`planFrame(acc, frameDt)` 纯函数（累加、上限 0.25s 防螺旋死亡、输出步数与插值 alpha），`createLoop` 用 rAF 驱动。

### A9 渲染与 HUD（render/ + ui/）

- 场景：晴空背景 + 雾、半球光 + 平行光；low-poly、flatShading、MeshLambert。
- 地形：161×161 网格平面，高度 = 赛道最近点海拔 + 距离加权（smoothstep 12~80m）丘陵噪声，路面以下 −0.15m，保证路与地面贴合。
- 路面：沿采样点挤出双向 ribbon（路肩 / 沥青 / 两侧白线 / 中心虚线）+ 终点拱门；`DoubleSide` 规避绕序问题。
- 路侧：320 棵 InstancedMesh 树（种子 RNG，落在赛道两侧 10~50m，贴地形高度）。
- 车手：约 10 个图元的低模（双轮 + 车架管 + 躯干 + 头 + 四肢），按 `sampleAt(dist)` + 法向偏移 `lateral` 摆放，yaw 对齐航向、pitch 随坡度。
- 相机：追尾第三人称，指数平滑跟随后上方 8.5m/3.2m 处，lookAt 前方 12m。
- HUD（DOM）：左上速度/档位/坡度、右上名次/剩余距离、左下体力条、右下 canvas 高程剖面 + 进度标记；倒计时居中大字；结算面板显示名次/时间/均功率，按 R 重开。

### A10 文件结构

```
index.html                # DOM 骨架（HUD 元素 + 开始遮罩）
package.json / tsconfig.json / vite.config.ts
src/main.ts               # 引导：实例化 Hud/Input/Game，绑定开始按钮
src/game.ts               # 组装：状态快照、循环、插值渲染、相机、HUD 视图
src/input.ts              # 键盘 → RiderCommand（档位边沿 + 横移保持）
src/style.css             # HUD/遮罩样式
src/core/loop.ts          # planFrame + createLoop
src/sim/types.ts          # GearId/RiderCommand/RiderType/RiderState/ResultRow/Phase
src/sim/params.ts         # PHYS / RACE / GEARS 常量
src/sim/rng.ts            # rngNext（状态机式）/ mulberry32（渲染侧用）
src/sim/physics.ts        # stepSpeed
src/sim/energy.ts         # drainRate / targetPower / stepEnergy
src/sim/track.ts          # Track：CR 样条 + 弧长采样 + nearest
src/sim/trackData.ts      # buildTrack：控制点 + 高程断点
src/sim/draft.ts          # isDrafting / nearestAheadIndex
src/sim/ai.ts             # aiCommand
src/sim/race.ts           # createRace / stepRace / standings
src/render/scene.ts       # createScene
src/render/terrain.ts     # terrainHeight / buildTerrain / smoothstep
src/render/trackMesh.ts   # buildTrackMesh（ribbon/虚线/拱门/树）
src/render/riders.ts      # buildRiderMesh / placeRider
src/render/camera.ts      # ChaseCamera
src/ui/hud.ts             # Hud（DOM 更新 + 高程 canvas + 结算）
测试与模块同目录：src/**/*.test.ts
```

---

## Part B 实施任务（TDD，按序执行）

每个任务以测试失败开始、测试通过结束、以提交收尾。命令均在仓库根目录运行。

### Task 0：工程脚手架

**Files:**
- Create: `package.json`、`tsconfig.json`、`vite.config.ts`、`index.html`、`src/main.ts`

- [ ] **Step 1：创建 `package.json`**

```json
{
  "name": "cycling-racing",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc --noEmit && vite build",
    "test": "vitest run --passWithNoTests",
    "typecheck": "tsc --noEmit"
  },
  "dependencies": {
    "three": "^0.180.0"
  },
  "devDependencies": {
    "jsdom": "^25.0.0",
    "typescript": "^5.6.0",
    "vite": "^6.0.0",
    "vitest": "^2.1.0"
  }
}
```

- [ ] **Step 2：创建 `tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true,
    "types": ["vite/client"],
    "skipLibCheck": true,
    "noEmit": true
  },
  "include": ["src"]
}
```

- [ ] **Step 3：创建 `vite.config.ts`**

```ts
import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',
  },
});
```

- [ ] **Step 4：创建 `index.html`**

```html
<!doctype html>
<html lang="zh">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>公路车竞速 MVP</title>
</head>
<body>
  <div id="app"></div>
  <div id="hud">
    <div id="top-left">
      <div id="speed">0.0 km/h</div>
      <div id="gear">档位 巡航</div>
      <div id="gradient">坡度 0.0%</div>
    </div>
    <div id="top-right">
      <div id="position">第 8 / 8 位</div>
      <div id="remaining">剩余 0.00 km</div>
    </div>
    <div id="bottom-left">
      <div class="bar"><div id="energy-bar"></div></div>
      <div id="energy-text">体力 100%</div>
    </div>
    <canvas id="elev" width="200" height="56"></canvas>
  </div>
  <div id="countdown"></div>
  <div id="results"></div>
  <div id="overlay-start">
    <h1>公路车竞速</h1>
    <p>W/S 或 ↑/↓ 切换功率档位 · A/D 或 ←/→ 左右占位 · 跟车省力，留体力终点冲刺</p>
    <button id="btn-start">开始比赛</button>
  </div>
  <script type="module" src="/src/main.ts"></script>
</body>
</html>
```

- [ ] **Step 5：创建占位 `src/main.ts`**

```ts
console.log('cycling mvp boot');
```

- [ ] **Step 6：安装依赖并验证**

Run: `npm install`
Run: `npm run typecheck` — 预期：0 错误
Run: `npm test` — 预期：`No test files found`，通过（passWithNoTests）
Run: `npm run build` — 预期：构建成功

- [ ] **Step 7：提交**

```bash
git add package.json package-lock.json tsconfig.json vite.config.ts index.html src/main.ts
git commit -m "🎉 init(project): scaffold vite + ts + three + vitest"
```

### Task 1：仿真基础（类型 / 参数 / RNG）

**Files:**
- Create: `src/sim/types.ts`、`src/sim/params.ts`、`src/sim/rng.ts`
- Test: `src/sim/rng.test.ts`

- [ ] **Step 1：写失败测试 `src/sim/rng.test.ts`**

```ts
import { describe, expect, it } from 'vitest';
import { mulberry32, rngNext } from './rng';

describe('rng', () => {
  it('same seed → same sequence', () => {
    const a = mulberry32(42);
    const b = mulberry32(42);
    for (let i = 0; i < 100; i++) expect(a()).toBe(b());
  });
  it('values in [0,1)', () => {
    const r = mulberry32(7);
    for (let i = 0; i < 1000; i++) {
      const v = r();
      expect(v).toBeGreaterThanOrEqual(0);
      expect(v).toBeLessThan(1);
    }
  });
  it('rngNext returns value and advanced state', () => {
    const [v1, s1] = rngNext(100);
    const [v2] = rngNext(s1);
    expect(v1).not.toBe(v2);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./rng`

- [ ] **Step 3：实现 `src/sim/types.ts`**

```ts
export type GearId = 0 | 1 | 2 | 3;

export interface RiderCommand {
  gear: GearId;
  steer: number;
}

export interface RiderType {
  label: string;
  ftp: number;
  maxEnergy: number;
  sprintDist: number;
  aggression: number;
}

export interface RiderState {
  id: number;
  name: string;
  isPlayer: boolean;
  type: RiderType;
  dist: number;
  lateral: number;
  speed: number;
  energy: number;
  gear: GearId;
  power: number;
  powerSum: number;
  timeSum: number;
  finishTime: number | null;
  wanderTarget: number;
}

export interface ResultRow {
  id: number;
  name: string;
  time: number;
  avgPower: number;
}

export type Phase = 'countdown' | 'racing' | 'finished';
```

- [ ] **Step 4：实现 `src/sim/params.ts`**

```ts
export const PHYS = {
  mass: 78,
  g: 9.81,
  Crr: 0.005,
  rho: 1.226,
  CdA: 0.32,
  draftDrag: 0.7,
  minSpeed: 1,
} as const;

export const RACE = {
  dt: 1 / 60,
  gearRatios: [0.6, 1.0, 1.5, 2.5],
  drainBase: 30,
  emptyCapRatio: 0.45,
  lateralMax: 2.2,
  lateralSpeed: 1.8,
  draftGapMin: 1,
  draftGapMax: 15,
  draftLateralWidth: 1.2,
  countdown: 3,
  finishWait: 30,
  trackWidth: 6,
} as const;

export const GEARS = ['轻松', '巡航', '发力', '冲刺'] as const;
```

- [ ] **Step 5：实现 `src/sim/rng.ts`**

```ts
export function rngNext(state: number): [number, number] {
  let a = (state + 0x6d2b79f5) | 0;
  let t = Math.imul(a ^ (a >>> 15), 1 | a);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return [((t ^ (t >>> 14)) >>> 0) / 4294967296, a >>> 0];
}

export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    const [v, next] = rngNext(a);
    a = next;
    return v;
  };
}
```

- [ ] **Step 6：跑测试确认通过** — `npm test`，预期：rng 3 项 PASS

- [ ] **Step 7：提交**

```bash
git add src/sim/types.ts src/sim/params.ts src/sim/rng.ts src/sim/rng.test.ts
git commit -m "✨ feat(sim): add rider types, tunable params and seeded rng"
```

### Task 2：功率→速度物理

**Files:**
- Create: `src/sim/physics.ts`
- Test: `src/sim/physics.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';

const dt = 1 / 60;

function steadyState(power: number, gradient: number, drafting: boolean): number {
  let v = 0;
  for (let i = 0; i < 60 * 180; i++) v = stepSpeed(v, power, gradient, drafting, dt);
  return v;
}

describe('stepSpeed', () => {
  it('300W flat settles near 39 km/h', () => {
    const v = steadyState(300, 0, false);
    expect(v).toBeGreaterThan(10.3);
    expect(v).toBeLessThan(11.5);
  });
  it('drafting raises steady speed by >1 m/s at 300W', () => {
    expect(steadyState(300, 0, true) - steadyState(300, 0, false)).toBeGreaterThan(1);
  });
  it('6% climb at 300W settles between 4.8 and 6.0 m/s', () => {
    const v = steadyState(300, 0.06, false);
    expect(v).toBeGreaterThan(4.8);
    expect(v).toBeLessThan(6.0);
  });
  it('coasting decays to zero and never negative', () => {
    let v = 10;
    for (let i = 0; i < 60 * 60; i++) v = stepSpeed(v, 0, 0, false, dt);
    expect(v).toBe(0);
  });
  it('downhill rolls faster than flat at same power', () => {
    expect(steadyState(300, -0.04, false)).toBeGreaterThan(steadyState(300, 0, false));
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./physics`

- [ ] **Step 3：实现 `src/sim/physics.ts`**

```ts
import { PHYS } from './params';

export function stepSpeed(speed: number, power: number, gradient: number, drafting: boolean, dt: number): number {
  const theta = Math.atan(gradient);
  const dragArea = PHYS.CdA * (drafting ? PHYS.draftDrag : 1);
  const fDrive = power / Math.max(speed, PHYS.minSpeed);
  const fRoll = PHYS.Crr * PHYS.mass * PHYS.g * Math.cos(theta);
  const fDrag = 0.5 * PHYS.rho * dragArea * speed * speed;
  const fGrav = PHYS.mass * PHYS.g * Math.sin(theta);
  const a = (fDrive - fRoll - fDrag - fGrav) / PHYS.mass;
  return Math.max(0, speed + a * dt);
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：physics 5 项 PASS

- [ ] **Step 5：提交**

```bash
git add src/sim/physics.ts src/sim/physics.test.ts
git commit -m "✨ feat(sim): add power-to-speed physics with drag, rolling and gradient"
```

### Task 3：体力系统

**Files:**
- Create: `src/sim/energy.ts`
- Test: `src/sim/energy.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { drainRate, stepEnergy, targetPower } from './energy';

describe('energy', () => {
  it('drain scales with squared relative power', () => {
    expect(drainRate(300, 300)).toBe(30);
    expect(drainRate(750, 300)).toBe(187.5);
    expect(drainRate(180, 300)).toBeCloseTo(10.8, 6);
  });
  it('stepEnergy floors at zero', () => {
    expect(stepEnergy(1, 750, 300, 1)).toBe(0);
  });
  it('full tank targets gear power', () => {
    expect(targetPower(3, 300, 1000)).toBe(750);
  });
  it('empty tank caps power at 45% ftp', () => {
    expect(targetPower(3, 300, 0)).toBe(135);
    expect(targetPower(1, 300, 0)).toBe(135);
  });
  it('cruise drains full tank in ~800s', () => {
    let e = 24000;
    let t = 0;
    while (e > 0) {
      e = stepEnergy(e, 300, 300, 1);
      t++;
    }
    expect(t).toBeGreaterThan(700);
    expect(t).toBeLessThan(900);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./energy`

- [ ] **Step 3：实现 `src/sim/energy.ts`**

```ts
import { RACE } from './params';
import type { GearId } from './types';

export function drainRate(power: number, ftp: number): number {
  const r = power / ftp;
  return r * r * RACE.drainBase;
}

export function targetPower(gear: GearId, ftp: number, energy: number): number {
  const raw = ftp * RACE.gearRatios[gear];
  return energy <= 0 ? Math.min(raw, ftp * RACE.emptyCapRatio) : raw;
}

export function stepEnergy(energy: number, power: number, ftp: number, dt: number): number {
  return Math.max(0, energy - drainRate(power, ftp) * dt);
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：energy 5 项 PASS

- [ ] **Step 5：提交**

```bash
git add src/sim/energy.ts src/sim/energy.test.ts
git commit -m "✨ feat(sim): add energy drain, gear power targets and empty-tank cap"
```

### Task 4：赛道样条与弧长采样

**Files:**
- Create: `src/sim/track.ts`
- Test: `src/sim/track.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { Track } from './track';

function circle(r: number, n: number, elev?: (i: number) => number): [number, number, number][] {
  return Array.from({ length: n }, (_, i) => {
    const a = (i / n) * Math.PI * 2;
    return [Math.cos(a) * r, elev ? elev(i) : 0, Math.sin(a) * r] as [number, number, number];
  });
}

describe('Track', () => {
  it('circle circumference ≈ 2πr', () => {
    const t = new Track(circle(100, 8));
    expect(t.length).toBeGreaterThan(610);
    expect(t.length).toBeLessThan(645);
  });
  it('sampleAt(0) equals first control point', () => {
    const s = new Track(circle(100, 8)).sampleAt(0);
    expect(s.x).toBeCloseTo(100, 3);
    expect(s.y).toBeCloseTo(0, 3);
    expect(s.z).toBeCloseTo(0, 3);
  });
  it('wraps around the loop', () => {
    const t = new Track(circle(100, 8));
    const a = t.sampleAt(5);
    const b = t.sampleAt(t.length + 5);
    expect(b.x).toBeCloseTo(a.x, 3);
    expect(b.z).toBeCloseTo(a.z, 3);
  });
  it('reports positive gradient on climb', () => {
    const t = new Track(circle(100, 8, (i) => (i / 8) * 50));
    expect(t.sampleAt(t.length * 0.25).gradient).toBeGreaterThan(0.05);
  });
  it('nearest finds close point', () => {
    const n = new Track(circle(100, 8)).nearest(100, 0);
    expect(n.dist).toBeLessThan(3);
    expect(n.y).toBeCloseTo(0, 3);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./track`

- [ ] **Step 3：实现 `src/sim/track.ts`**

```ts
export interface TrackSample {
  x: number;
  y: number;
  z: number;
  gradient: number;
  heading: number;
}

type Pt = [number, number, number];

function catmull(p0: number, p1: number, p2: number, p3: number, u: number): number {
  const u2 = u * u;
  const u3 = u2 * u;
  return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
}

export class Track {
  readonly length: number;
  private readonly pts: Pt[];
  private readonly cum: number[];

  constructor(control: Pt[], samples = 2400) {
    const n = control.length;
    this.pts = [];
    for (let i = 0; i <= samples; i++) {
      const t = (i / samples) * n;
      const j = Math.floor(t);
      const u = t - j;
      const g = (k: number) => control[(j + k + n) % n];
      const [p0, p1, p2, p3] = [g(-1), g(0), g(1), g(2)];
      this.pts.push([
        catmull(p0[0], p1[0], p2[0], p3[0], u),
        catmull(p0[1], p1[1], p2[1], p3[1], u),
        catmull(p0[2], p1[2], p2[2], p3[2], u),
      ]);
    }
    this.cum = [0];
    for (let i = 1; i <= samples; i++) {
      const a = this.pts[i - 1];
      const b = this.pts[i];
      this.cum.push(this.cum[i - 1] + Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
    }
    this.length = this.cum[samples];
  }

  sampleAt(dist: number): TrackSample {
    const d = ((dist % this.length) + this.length) % this.length;
    let lo = 0;
    let hi = this.cum.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (this.cum[mid] <= d) lo = mid;
      else hi = mid;
    }
    const a = this.pts[lo];
    const b = this.pts[hi];
    const seg = this.cum[hi] - this.cum[lo] || 1;
    const u = (d - this.cum[lo]) / seg;
    const run = Math.hypot(b[0] - a[0], b[2] - a[2]) || 1e-6;
    return {
      x: a[0] + (b[0] - a[0]) * u,
      y: a[1] + (b[1] - a[1]) * u,
      z: a[2] + (b[2] - a[2]) * u,
      gradient: (b[1] - a[1]) / run,
      heading: Math.atan2(b[2] - a[2], b[0] - a[0]),
    };
  }

  densePoints(): readonly Pt[] {
    return this.pts;
  }

  nearest(x: number, z: number): { y: number; dist: number } {
    let best = { y: this.pts[0][1], dist: Infinity };
    for (let i = 0; i < this.pts.length - 1; i += 8) {
      const p = this.pts[i];
      const d = Math.hypot(p[0] - x, p[2] - z);
      if (d < best.dist) best = { y: p[1], dist: d };
    }
    return best;
  }
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：track 5 项 PASS
- [ ] **Step 5：提交** — `git add src/sim/track.ts src/sim/track.test.ts` 然后 `git commit -m "✨ feat(sim): add catmull-rom track with arc-length sampling"`

### Task 5：赛道数据（控制点 + 高程剖面）

**Files:**
- Create: `src/sim/trackData.ts`
- Test: `src/sim/trackData.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack', () => {
  const t = buildTrack();
  it('length ≈ 4.4 km', () => {
    expect(t.length).toBeGreaterThan(4100);
    expect(t.length).toBeLessThan(4700);
  });
  it('max gradient stays below 9%', () => {
    let max = 0;
    for (let d = 0; d < t.length; d += 10) max = Math.max(max, Math.abs(t.sampleAt(d).gradient));
    expect(max).toBeLessThan(0.09);
  });
  it('finish straight is flat', () => {
    for (let d = t.length - 300; d < t.length; d += 10) {
      expect(Math.abs(t.sampleAt(d).gradient)).toBeLessThan(0.02);
    }
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./trackData`

- [ ] **Step 3：实现 `src/sim/trackData.ts`**

```ts
import { Track } from './track';

const PROFILE: [number, number][] = [
  [0, 0], [0.14, 0], [0.26, 28], [0.36, 0], [0.58, 0], [0.66, 16], [0.74, 0], [1, 0],
];

function elevationAt(u: number): number {
  for (let i = 1; i < PROFILE.length; i++) {
    if (u <= PROFILE[i][0]) {
      const [u0, h0] = PROFILE[i - 1];
      const [u1, h1] = PROFILE[i];
      return h0 + ((h1 - h0) * (u - u0)) / (u1 - u0);
    }
  }
  return 0;
}

export function buildTrack(): Track {
  const n = 24;
  const ctrl: [number, number, number][] = [];
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    const r = 700 + Math.sin(a * 3) * 80 + Math.cos(a * 2) * 60;
    ctrl.push([Math.cos(a) * r, elevationAt(i / n), Math.sin(a) * r]);
  }
  return new Track(ctrl);
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：trackData 3 项 PASS
- [ ] **Step 5：提交** — `git add src/sim/trackData.ts src/sim/trackData.test.ts` 然后 `git commit -m "✨ feat(sim): add data-driven race track with two climbs"`

### Task 6：跟车判定

**Files:**
- Create: `src/sim/draft.ts`
- Test: `src/sim/draft.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { isDrafting, nearestAheadIndex } from './draft';
import type { RiderState, RiderType } from './types';

const T: RiderType = { label: 'test', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 };

function rider(id: number, dist: number, lateral = 0, speed = 8): RiderState {
  return { id, name: `r${id}`, isPlayer: false, type: T, dist, lateral, speed, energy: 24000, gear: 1, power: 300, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0 };
}

describe('drafting', () => {
  it('true when 1-15m ahead and laterally aligned', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 110)])).toBe(true);
  });
  it('false when ahead is beyond 15m', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 120)])).toBe(false);
  });
  it('false when lateral offset too large', () => {
    expect(isDrafting(0, [rider(0, 100, 0), rider(1, 110, 2)])).toBe(false);
  });
  it('false for rider behind', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 90)])).toBe(false);
  });
  it('nearestAheadIndex picks closest ahead within 40m', () => {
    const rs = [rider(0, 100), rider(1, 130), rider(2, 108)];
    expect(nearestAheadIndex(rs[0], rs)).toBe(2);
  });
  it('nearestAheadIndex returns -1 when none ahead', () => {
    const rs = [rider(0, 100), rider(1, 90)];
    expect(nearestAheadIndex(rs[0], rs)).toBe(-1);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./draft`

- [ ] **Step 3：实现 `src/sim/draft.ts`**

```ts
import { RACE } from './params';
import type { RiderState } from './types';

export function nearestAheadIndex(self: RiderState, riders: readonly RiderState[]): number {
  let best = -1;
  let bestGap = 40;
  for (let i = 0; i < riders.length; i++) {
    const gap = riders[i].dist - self.dist;
    if (i !== self.id && gap > 0 && gap < bestGap) {
      bestGap = gap;
      best = i;
    }
  }
  return best;
}

export function isDrafting(selfIndex: number, riders: readonly RiderState[]): boolean {
  const self = riders[selfIndex];
  for (let i = 0; i < riders.length; i++) {
    if (i === selfIndex) continue;
    const gap = riders[i].dist - self.dist;
    if (gap >= RACE.draftGapMin && gap <= RACE.draftGapMax
      && Math.abs(riders[i].lateral - self.lateral) <= RACE.draftLateralWidth) {
      return true;
    }
  }
  return false;
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：draft 6 项 PASS
- [ ] **Step 5：提交** — `git add src/sim/draft.ts src/sim/draft.test.ts` 然后 `git commit -m "✨ feat(sim): add drafting detection window"`

### Task 7：AI 决策

**Files:**
- Create: `src/sim/ai.ts`
- Test: `src/sim/ai.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { aiCommand } from './ai';
import type { RiderState, RiderType } from './types';

function rider(over: Partial<RiderState>, type: RiderType = { label: 't', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 }): RiderState {
  return { id: 1, name: 'ai', isPlayer: false, type, dist: 1000, lateral: 0, speed: 9, energy: 20000, gear: 1, power: 300, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0, ...over };
}

describe('aiCommand', () => {
  it('sprints inside sprint distance with energy', () => {
    expect(aiCommand(rider({ energy: 12000 }), null, 200, 0).gear).toBe(3);
  });
  it('no sprint and conserves when nearly empty', () => {
    expect(aiCommand(rider({ energy: 2000 }), null, 200, 0).gear).toBe(0);
  });
  it('responds with hard gear when wheel ahead surges', () => {
    const self = rider({ speed: 9 });
    const ahead = rider({ id: 0, dist: 1010, speed: 10.5 });
    expect(aiCommand(self, ahead, 3000, 0).gear).toBe(2);
  });
  it('climber attacks on steep gradient with fresh legs', () => {
    const climber: RiderType = { label: 'climber', ftp: 315, maxEnergy: 22000, sprintDist: 120, aggression: 0.85 };
    expect(aiCommand(rider({ energy: 20000 }, climber), null, 3000, 0.05).gear).toBe(2);
  });
  it('conserves below 25% energy', () => {
    expect(aiCommand(rider({ energy: 5000 }), null, 3000, 0).gear).toBe(0);
  });
  it('steers toward wheel ahead', () => {
    const self = rider({ lateral: 0 });
    const ahead = rider({ id: 0, dist: 1010, lateral: 1.5 });
    expect(aiCommand(self, ahead, 3000, 0).steer).toBe(1);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./ai`

- [ ] **Step 3：实现 `src/sim/ai.ts`**

```ts
import type { RiderCommand, RiderState } from './types';

export function aiCommand(
  self: RiderState,
  ahead: RiderState | null,
  remaining: number,
  gradient: number,
): RiderCommand {
  const e = self.energy / self.type.maxEnergy;
  let target = self.wanderTarget;
  if (ahead && ahead.dist - self.dist < 25) target = ahead.lateral * 0.8;
  let steer = 0;
  if (Math.abs(target - self.lateral) > 0.15) steer = Math.sign(target - self.lateral);
  if (e <= 0.02) return { gear: 0, steer: 0 };
  if (remaining <= self.type.sprintDist && e > 0.12) return { gear: 3, steer: 0 };
  if (ahead && ahead.dist - self.dist < 25 && ahead.speed > self.speed + 0.8) return { gear: 2, steer };
  if (gradient > 0.045 && e > 0.5 && self.type.aggression > 0.6) return { gear: 2, steer: 0 };
  return { gear: e < 0.25 ? 0 : 1, steer };
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：ai 6 项 PASS
- [ ] **Step 5：提交** — `git add src/sim/ai.ts src/sim/ai.test.ts` 然后 `git commit -m "✨ feat(sim): add behavior-based ai with sprint, respond and climb attack"`

### Task 8：比赛编排（阶段机 / 冲线 / 结算 / 确定性）

**Files:**
- Create: `src/sim/race.ts`
- Test: `src/sim/race.test.ts`

- [ ] **Step 1：写失败测试 `src/sim/race.test.ts`**

```ts
import { describe, expect, it } from 'vitest';
import { createRace, standings, stepRace } from './race';
import { buildTrack } from './trackData';
import { RACE } from './params';
import type { RiderCommand } from './types';

const track = buildTrack();
const cruise: RiderCommand = { gear: 1, steer: 0 };

function run(cmd: RiderCommand, seconds: number) {
  let s = createRace(track);
  for (let i = 0; i < seconds / RACE.dt; i++) s = stepRace(s, track, cmd, RACE.dt);
  return s;
}

describe('race', () => {
  it('countdown counts down to racing', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.phase).toBe('racing');
    expect(s.time).toBe(0);
  });
  it('riders advance after start', () => {
    const s = run(cruise, 10);
    expect(s.riders.every((r) => r.dist > 10)).toBe(true);
  });
  it('records finish time when crossing line', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: track.length - 0.1, speed: 20 })) };
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders.every((r) => r.finishTime !== null)).toBe(true);
  });
  it('deterministic for identical inputs', () => {
    expect(JSON.stringify(run(cruise, 120))).toBe(JSON.stringify(run(cruise, 120)));
  });
  it('standings orders by distance during race', () => {
    const st = standings(run(cruise, 60));
    expect(st[0].dist).toBeGreaterThanOrEqual(st[st.length - 1].dist);
  });
  it('full race completes with all riders finishing and sane avg power', () => {
    let s = createRace(track);
    let steps = 0;
    while (s.phase !== 'finished' && steps < 60 * 1500) {
      s = stepRace(s, track, cruise, RACE.dt);
      steps++;
    }
    expect(s.phase).toBe('finished');
    expect(s.riders.every((r) => r.finishTime !== null)).toBe(true);
    for (const row of s.results) {
      expect(row.avgPower).toBeGreaterThan(150);
      expect(row.avgPower).toBeLessThan(400);
    }
  }, 30000);
  it('reckless sprinting loses to steady pacing', () => {
    const steady = run(cruise, 900).riders[0].finishTime!;
    const reckless = run({ gear: 3, steer: 0 }, 900).riders[0].finishTime!;
    expect(reckless).toBeGreaterThan(steady);
  }, 30000);
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./race`

- [ ] **Step 3：实现 `src/sim/race.ts`（上半：状态与工厂）**

```ts
import { RACE } from './params';
import { rngNext } from './rng';
import { stepEnergy, targetPower } from './energy';
import { stepSpeed } from './physics';
import { isDrafting, nearestAheadIndex } from './draft';
import { aiCommand } from './ai';
import type { Track } from './track';
import type { Phase, ResultRow, RiderCommand, RiderState, RiderType } from './types';

export interface RaceState {
  phase: Phase;
  time: number;
  countdown: number;
  riders: RiderState[];
  trackLength: number;
  rngState: number;
  results: ResultRow[];
}

export const PLAYER_TYPE: RiderType = { label: 'all-rounder', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 };

export const AI_FIELD: RiderType[] = [
  { label: 'climber', ftp: 315, maxEnergy: 22000, sprintDist: 120, aggression: 0.85 },
  { label: 'climber', ftp: 306, maxEnergy: 22500, sprintDist: 130, aggression: 0.7 },
  { label: 'sprinter', ftp: 286, maxEnergy: 26000, sprintDist: 350, aggression: 0.4 },
  { label: 'sprinter', ftp: 293, maxEnergy: 25500, sprintDist: 300, aggression: 0.5 },
  { label: 'rouleur', ftp: 298, maxEnergy: 24000, sprintDist: 200, aggression: 0.55 },
  { label: 'rouleur', ftp: 295, maxEnergy: 24500, sprintDist: 220, aggression: 0.6 },
  { label: 'all-rounder', ftp: 300, maxEnergy: 23500, sprintDist: 240, aggression: 0.5 },
];

const NAMES = ['你', '山神', '穿山甲', '火箭', '冲刺王', '发动机', '老将', '全能手'];

function makeRider(id: number, type: RiderType, isPlayer: boolean, dist: number, lateral: number): RiderState {
  return {
    id, name: NAMES[id], isPlayer, type,
    dist, lateral, speed: 0, energy: type.maxEnergy, gear: 1,
    power: 0, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0,
  };
}

export function createRace(track: Track): RaceState {
  const riders: RiderState[] = [makeRider(0, PLAYER_TYPE, true, 0, 0.8)];
  for (let i = 0; i < AI_FIELD.length; i++) {
    riders.push(makeRider(i + 1, AI_FIELD[i], false, (i + 1) * 1.6, (i % 2 === 0 ? -0.8 : 0.8)));
  }
  return { phase: 'countdown', time: 0, countdown: RACE.countdown, riders, trackLength: track.length, rngState: 20260926, results: [] };
}
```

- [ ] **Step 4：实现 `src/sim/race.ts`（下半：步进与结算）**

```ts
function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

export function buildResults(riders: readonly RiderState[]): ResultRow[] {
  return [...riders]
    .sort((a, b) => (a.finishTime ?? 1e9) - (b.finishTime ?? 1e9) || b.dist - a.dist)
    .map((r) => ({
      id: r.id,
      name: r.name,
      time: r.finishTime ?? r.dist,
      avgPower: r.timeSum > 0 ? r.powerSum / r.timeSum : 0,
    }));
}

export function standings(s: RaceState): RiderState[] {
  return [...s.riders].sort((a, b) => (a.finishTime ?? 1e9) - (b.finishTime ?? 1e9) || b.dist - a.dist);
}

export function stepRace(s: RaceState, track: Track, playerCmd: RiderCommand, dt: number): RaceState {
  if (s.phase === 'finished') return s;
  if (s.phase === 'countdown') {
    const countdown = s.countdown - dt;
    return countdown <= 0 ? { ...s, phase: 'racing', countdown: 0 } : { ...s, countdown };
  }
  let rngState = s.rngState;
  const rng = () => {
    const [v, next] = rngNext(rngState);
    rngState = next;
    return v;
  };
  const riders = s.riders.map((r) => ({ ...r }));
  for (const r of riders) {
    if (!r.isPlayer && rng() < 0.02) r.wanderTarget = (rng() * 2 - 1) * 0.6;
  }
  for (let i = 0; i < riders.length; i++) {
    const r = riders[i];
    const gradient = track.sampleAt(Math.min(r.dist, s.trackLength - 0.01)).gradient;
    let cmd: RiderCommand;
    if (r.finishTime !== null) {
      cmd = { gear: 0, steer: 0 };
    } else if (r.isPlayer) {
      cmd = playerCmd;
    } else {
      const a = nearestAheadIndex(r, riders);
      cmd = aiCommand(r, a >= 0 ? riders[a] : null, Math.max(0, s.trackLength - r.dist), gradient);
    }
    r.gear = cmd.gear;
    r.power = r.finishTime !== null ? 0 : targetPower(cmd.gear, r.type.ftp, r.energy);
    r.speed = r.finishTime !== null ? Math.max(0, r.speed - 2 * dt) : stepSpeed(r.speed, r.power, gradient, isDrafting(i, riders), dt);
    if (r.finishTime === null) {
      r.dist += r.speed * dt;
      r.lateral = clamp(r.lateral + cmd.steer * RACE.lateralSpeed * dt, -RACE.lateralMax, RACE.lateralMax);
      r.energy = stepEnergy(r.energy, r.power, r.type.ftp, dt);
      r.powerSum += r.power * dt;
      r.timeSum += dt;
      if (r.dist >= s.trackLength) {
        r.finishTime = s.time + dt - (r.dist - s.trackLength) / Math.max(r.speed, 0.1);
        r.dist = s.trackLength;
      }
    }
  }
  const next: RaceState = { ...s, time: s.time + dt, riders, rngState };
  if (riders.every((r) => r.finishTime !== null)
    || (riders[0].finishTime !== null && s.time > riders[0].finishTime + RACE.finishWait)) {
    next.results = buildResults(riders);
    next.phase = 'finished';
  }
  return next;
}
```

- [ ] **Step 5：跑测试确认通过** — `npm test`，预期：race 7 项 PASS（两个长测试约数秒）
- [ ] **Step 6：提交** — `git add src/sim/race.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): add race orchestration with phases, finish and deterministic results"`

### Task 9：固定步长主循环

**Files:**
- Create: `src/core/loop.ts`
- Test: `src/core/loop.test.ts`

- [ ] **Step 1：写失败测试**

```ts
import { describe, expect, it } from 'vitest';
import { planFrame } from './loop';

describe('planFrame', () => {
  it('one 60fps frame yields one step', () => {
    const p = planFrame(0, 1 / 60);
    expect(p.steps).toBe(1);
    expect(p.alpha).toBeCloseTo(0, 6);
  });
  it('30fps frame yields two steps', () => {
    expect(planFrame(0, 1 / 30).steps).toBe(2);
  });
  it('accumulates remainder', () => {
    const a = planFrame(0, 1 / 120);
    expect(a.steps).toBe(0);
    expect(planFrame(a.acc, 1 / 120).steps).toBe(1);
  });
  it('clamps huge frame delta to 0.25s', () => {
    const p = planFrame(0, 5);
    expect(p.steps).toBeGreaterThanOrEqual(14);
    expect(p.steps).toBeLessThanOrEqual(15);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./loop`

- [ ] **Step 3：实现 `src/core/loop.ts`**

```ts
import { RACE } from '../sim/params';

export interface FramePlan {
  steps: number;
  acc: number;
  alpha: number;
}

export function planFrame(acc: number, frameDt: number, dt: number = RACE.dt): FramePlan {
  const total = Math.min(acc + frameDt, 0.25);
  const steps = Math.floor(total / dt);
  const rest = total - steps * dt;
  return { steps, acc: rest, alpha: rest / dt };
}

export function createLoop(update: (dt: number) => void, render: (alpha: number, frameDt: number) => void) {
  let raf = 0;
  let last = 0;
  let acc = 0;
  const tick = (now: number) => {
    const frameDt = Math.min((now - last) / 1000, 0.25);
    last = now;
    const plan = planFrame(acc, frameDt);
    acc = plan.acc;
    for (let i = 0; i < plan.steps; i++) update(RACE.dt);
    render(plan.alpha, frameDt);
    raf = requestAnimationFrame(tick);
  };
  return {
    start() {
      last = performance.now();
      raf = requestAnimationFrame(tick);
    },
    stop() {
      cancelAnimationFrame(raf);
    },
  };
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：loop 4 项 PASS
- [ ] **Step 5：提交** — `git add src/core/loop.ts src/core/loop.test.ts` 然后 `git commit -m "✨ feat(core): add fixed-timestep loop with interpolation planning"`

### Task 10：键盘输入

**Files:**
- Create: `src/input.ts`
- Test: `src/input.test.ts`

- [ ] **Step 1：写失败测试 `src/input.test.ts`（文件首行指定 jsdom 环境）**

```ts
// @vitest-environment jsdom
import { describe, expect, it, beforeEach } from 'vitest';
import { InputController } from './input';

function press(key: string, repeat = false) {
  window.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true, repeat }));
}
function release(key: string) {
  window.dispatchEvent(new KeyboardEvent('keyup', { key, bubbles: true }));
}

describe('InputController', () => {
  let c: InputController;
  beforeEach(() => {
    c = new InputController();
    c.attach();
  });

  it('gear up on ArrowUp', () => {
    press('ArrowUp');
    expect(c.command().gear).toBe(2);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().gear).toBe(2);
  });
  it('gear down clamps at 0', () => {
    press('ArrowDown');
    press('ArrowDown');
    press('ArrowDown');
    expect(c.command().gear).toBe(0);
  });
  it('direct gear select with number keys', () => {
    press('4');
    expect(c.command().gear).toBe(3);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./input`

- [ ] **Step 3：实现 `src/input.ts`**

```ts
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private steer = 0;
  private keys = new Set<string>();

  command(): RiderCommand {
    return { gear: this.gear, steer: this.steer };
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.steer = 0;
    this.keys.clear();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.gear = Math.min(3, this.gear + 1) as GearId;
    else if (k === 'arrowdown' || k === 's') this.gear = Math.max(0, this.gear - 1) as GearId;
    else if (k === '1' || k === '2' || k === '3' || k === '4') this.gear = (Number(k) - 1) as GearId;
    else {
      this.keys.add(k);
      this.steer = this.computeSteer();
    }
  };

  private onKeyUp = (e: KeyboardEvent) => {
    this.keys.delete(e.key.toLowerCase());
    this.steer = this.computeSteer();
  };

  private computeSteer(): number {
    const left = this.keys.has('arrowleft') || this.keys.has('a');
    const right = this.keys.has('arrowright') || this.keys.has('d');
    return right && !left ? 1 : left && !right ? -1 : 0;
  }
}
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：input 5 项 PASS
- [ ] **Step 5：提交** — `git add src/input.ts src/input.test.ts` 然后 `git commit -m "✨ feat(input): add keyboard gear switching and lateral steering"`

### Task 11：Three.js 场景与地形

> 渲染层不做单元测试（见 A2），验证方式：`npm run typecheck` + `npm run build`。

**Files:**
- Create: `src/render/scene.ts`、`src/render/terrain.ts`

- [ ] **Step 1：实现 `src/render/scene.ts`**

```ts
import * as THREE from 'three';

export interface SceneCtx {
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  renderer: THREE.WebGLRenderer;
}

export function createScene(canvasHost: HTMLElement): SceneCtx {
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  canvasHost.appendChild(renderer.domElement);
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x9fd4f0);
  scene.fog = new THREE.Fog(0x9fd4f0, 250, 1400);
  const camera = new THREE.PerspectiveCamera(65, window.innerWidth / window.innerHeight, 0.1, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe8ff, 0x5a7a4a, 0.9));
  const sun = new THREE.DirectionalLight(0xfff3d6, 1.4);
  sun.position.set(300, 500, 200);
  scene.add(sun);
  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });
  return { scene, camera, renderer };
}
```

- [ ] **Step 2：实现 `src/render/terrain.ts`**

```ts
import * as THREE from 'three';
import type { Track } from '../sim/track';

export function baseHills(x: number, z: number): number {
  return 6 * Math.sin(x / 430) * Math.cos(z / 380) + 3 * Math.sin(x / 150 + z / 170);
}

export function smoothstep(v: number, e0: number, e1: number): number {
  const t = Math.min(1, Math.max(0, (v - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

export function terrainHeight(track: Track, x: number, z: number): number {
  const near = track.nearest(x, z);
  return near.y + baseHills(x, z) * smoothstep(near.dist, 12, 80);
}

export function buildTerrain(track: Track): THREE.Mesh {
  const pts = track.densePoints();
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const [x, , z] of pts) {
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
  }
  const margin = 300;
  const geo = new THREE.PlaneGeometry(maxX - minX + margin * 2, maxZ - minZ + margin * 2, 150, 150);
  geo.rotateX(-Math.PI / 2);
  const pos = geo.attributes.position as THREE.BufferAttribute;
  const cx = (minX + maxX) / 2;
  const cz = (minZ + maxZ) / 2;
  for (let i = 0; i < pos.count; i++) {
    pos.setY(i, terrainHeight(track, pos.getX(i) + cx, pos.getZ(i) + cz) - 0.15);
  }
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x6fae57 }));
}
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build`，预期：均通过
- [ ] **Step 4：提交** — `git add src/render/scene.ts src/render/terrain.ts` 然后 `git commit -m "✨ feat(render): add scene setup and blended low-poly terrain"`

### Task 12：路面网格与路侧元素

**Files:**
- Create: `src/render/trackMesh.ts`

- [ ] **Step 1：实现 `src/render/trackMesh.ts`（上半：ribbon 与虚线）**

```ts
import * as THREE from 'three';
import { RACE } from '../sim/params';
import { mulberry32 } from '../sim/rng';
import { terrainHeight } from './terrain';
import type { Track } from '../sim/track';

function ribbon(track: Track, left: number, right: number, color: number, dy: number): THREE.Mesh {
  const pts = track.densePoints();
  const n = pts.length - 1;
  const positions = new Float32Array((n + 1) * 6);
  const indices = new Uint32Array(n * 6);
  for (let i = 0; i <= n; i++) {
    const [x, y, z] = pts[i];
    const [x2, , z2] = pts[i === n ? i - 1 : i + 1];
    let dx = x2 - x, dz = z2 - z;
    if (i === n) {
      dx = -dx;
      dz = -dz;
    }
    const len = Math.hypot(dx, dz) || 1;
    dx /= len; dz /= len;
    const nx = -dz, nz = dx;
    const o = i * 6;
    positions[o] = x + nx * left; positions[o + 1] = y + dy; positions[o + 2] = z + nz * left;
    positions[o + 3] = x + nx * right; positions[o + 4] = y + dy; positions[o + 5] = z + nz * right;
  }
  for (let i = 0; i < n; i++) {
    const a = i * 2, b = a + 1, c = a + 2, d = a + 3;
    indices.set([a, c, b, b, c, d], i * 6);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geo.setIndex(new THREE.BufferAttribute(indices, 1));
  geo.computeVertexNormals();
  const mesh = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color, side: THREE.DoubleSide }));
  mesh.frustumCulled = false;
  return mesh;
}

function dashes(track: Track, halfW: number, segLen: number, gap: number, color: number, dy: number): THREE.Mesh {
  const verts: number[] = [];
  for (let d = 0; d < track.length; d += segLen + gap) {
    for (const s of [track.sampleAt(d), track.sampleAt(Math.min(d + segLen, track.length - 0.01))]) {
      const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
      verts.push(s.x + nx * halfW, s.y + dy, s.z + nz * halfW, s.x - nx * halfW, s.y + dy, s.z - nz * halfW);
    }
  }
  const indices: number[] = [];
  for (let q = 0; q < verts.length / 12; q++) {
    const a = q * 4;
    indices.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(verts), 3));
  geo.setIndex(indices);
  return new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide }));
}
```

- [ ] **Step 2：实现 `src/render/trackMesh.ts`（下半：拱门 / 树 / 组装）**

```ts
function finishGates(track: Track): THREE.Group {
  const s = track.sampleAt(0);
  const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
  const g = new THREE.Group();
  const postMat = new THREE.MeshLambertMaterial({ color: 0x30343c });
  for (const side of [-1, 1]) {
    const post = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.25, 6, 8), postMat);
    post.position.set(s.x + nx * side * 4.5, s.y + 3, s.z + nz * side * 4.5);
    g.add(post);
  }
  const top = new THREE.Mesh(new THREE.BoxGeometry(9.6, 0.8, 0.5), new THREE.MeshLambertMaterial({ color: 0xe0533d }));
  top.position.set(s.x, s.y + 6.2, s.z);
  top.rotation.y = -s.heading + Math.PI / 2;
  g.add(top);
  return g;
}

function trees(track: Track): THREE.Group {
  const rng = mulberry32(1234);
  const count = 320;
  const trunks = new THREE.InstancedMesh(
    new THREE.CylinderGeometry(0.35, 0.5, 2.4, 6),
    new THREE.MeshLambertMaterial({ color: 0x7a5230 }),
    count,
  );
  const crowns = new THREE.InstancedMesh(
    new THREE.ConeGeometry(2.2, 5.5, 7),
    new THREE.MeshLambertMaterial({ color: 0x3d7a33, flatShading: true }),
    count,
  );
  const m = new THREE.Matrix4();
  let placed = 0;
  while (placed < count) {
    const s = track.sampleAt(rng() * track.length);
    const side = rng() < 0.5 ? -1 : 1;
    const off = 10 + rng() * 40;
    const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
    const x = s.x + nx * side * off;
    const z = s.z + nz * side * off;
    const sc = 0.7 + rng() * 0.9;
    m.makeScale(sc, sc, sc);
    m.setPosition(x, terrainHeight(track, x, z) + 1.2 * sc, z);
    trunks.setMatrixAt(placed, m);
    m.setPosition(x, terrainHeight(track, x, z) + 5.15 * sc, z);
    crowns.setMatrixAt(placed, m);
    placed++;
  }
  const g = new THREE.Group();
  g.add(trunks, crowns);
  return g;
}

export function buildTrackMesh(track: Track): THREE.Group {
  const half = RACE.trackWidth / 2;
  const group = new THREE.Group();
  group.add(ribbon(track, half + 0.3, -half - 0.3, 0x4a4a50, -0.03));
  group.add(ribbon(track, half, -half, 0x2b2b30, 0));
  group.add(ribbon(track, half - 0.05, half - 0.3, 0xe8e8e8, 0.008));
  group.add(ribbon(track, -half + 0.3, -half + 0.05, 0xe8e8e8, 0.008));
  group.add(dashes(track, 0.09, 3, 8, 0xffffff, 0.012));
  group.add(finishGates(track));
  group.add(trees(track));
  return group;
}
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build`，预期：均通过
- [ ] **Step 4：提交** — `git add src/render/trackMesh.ts` 然后 `git commit -m "✨ feat(render): add road ribbon, markings, finish gate and instanced trees"`

### Task 13：车手网格与追尾相机

**Files:**
- Create: `src/render/riders.ts`、`src/render/camera.ts`

- [ ] **Step 1：实现 `src/render/riders.ts`**

```ts
import * as THREE from 'three';
import type { TrackSample } from '../sim/track';

export function buildRiderMesh(jersey: number): THREE.Group {
  const group = new THREE.Group();
  const frameMat = new THREE.MeshLambertMaterial({ color: 0x22262e });
  const bodyMat = new THREE.MeshLambertMaterial({ color: jersey, flatShading: true });
  const wheelGeo = new THREE.CylinderGeometry(0.34, 0.34, 0.08, 10);
  const wheelMat = new THREE.MeshLambertMaterial({ color: 0x111111 });
  for (const z of [0.52, -0.52]) {
    const wheel = new THREE.Mesh(wheelGeo, wheelMat);
    wheel.rotation.z = Math.PI / 2;
    wheel.position.set(0, 0.34, z);
    group.add(wheel);
  }
  const tube = (len: number, x: number, y: number, z: number, rx: number) => {
    const mesh = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, len, 6), frameMat);
    mesh.rotation.set(rx, 0, 0);
    mesh.position.set(x, y, z);
    group.add(mesh);
  };
  tube(1.04, 0, 0.62, 0, Math.PI / 2);
  tube(0.7, 0, 0.52, 0.45, 0.5);
  tube(0.7, 0, 0.52, -0.45, -0.5);
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.5, 0.56), bodyMat);
  torso.position.set(0, 1.18, -0.08);
  torso.rotation.x = 0.7;
  group.add(torso);
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.14, 8, 8), new THREE.MeshLambertMaterial({ color: 0xf2c89a }));
  head.position.set(0, 1.42, 0.12);
  group.add(head);
  for (const [x, z, rx] of [[0.18, 0.35, -0.9], [-0.18, 0.35, -0.9], [0.18, -0.2, 0.9], [-0.18, -0.2, 0.9]] as const) {
    const limb = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.75, 6), bodyMat);
    limb.position.set(x, 0.78, z);
    limb.rotation.x = rx;
    group.add(limb);
  }
  return group;
}

export function placeRider(mesh: THREE.Object3D, s: TrackSample, lateral: number): void {
  const nx = -Math.sin(s.heading);
  const nz = Math.cos(s.heading);
  mesh.position.set(s.x + nx * lateral, s.y, s.z + nz * lateral);
  mesh.rotation.y = Math.PI / 2 - s.heading;
  mesh.rotation.x = -Math.atan(s.gradient) * 0.8;
}
```

- [ ] **Step 2：实现 `src/render/camera.ts`**

```ts
import * as THREE from 'three';

export class ChaseCamera {
  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.camera.position.x += (target.x - fx * 8.5 - this.camera.position.x) * k;
    this.camera.position.y += (target.y + 3.2 - this.camera.position.y) * k;
    this.camera.position.z += (target.z - fz * 8.5 - this.camera.position.z) * k;
    this.camera.lookAt(target.x + fx * 12, target.y + 1.2 + gradient * 6, target.z + fz * 12);
  }
}
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build`，预期：均通过
- [ ] **Step 4：提交** — `git add src/render/riders.ts src/render/camera.ts` 然后 `git commit -m "✨ feat(render): add low-poly rider mesh and chase camera"`

### Task 14：HUD 与样式

**Files:**
- Create: `src/ui/hud.ts`、`src/style.css`

- [ ] **Step 1：实现 `src/ui/hud.ts`**

```ts
import type { Phase, ResultRow } from '../sim/types';
import type { Track } from '../sim/track';

export interface HudView {
  speedKmh: number;
  gearName: string;
  energyFrac: number;
  gradientPct: number;
  remainingKm: number;
  position: number;
  fieldSize: number;
  progress: number;
  countdown: number | null;
  phase: Phase;
  results: ResultRow[];
}

export class Hud {
  private el = new Map<string, HTMLElement>();
  private heights: number[] = [];

  constructor() {
    for (const id of ['speed', 'gear', 'gradient', 'position', 'remaining', 'energy-bar', 'energy-text', 'elev', 'countdown', 'results']) {
      this.el.set(id, document.querySelector(`#${id}`) as HTMLElement);
    }
  }

  setProfile(track: Track): void {
    const n = 240;
    this.heights = [];
    for (let i = 0; i < n; i++) this.heights.push(track.sampleAt((i / (n - 1)) * track.length).y);
  }

  clearResults(): void {
    this.el.get('results')!.style.display = 'none';
  }

  update(v: HudView): void {
    const g = (id: string) => this.el.get(id)!;
    g('speed').textContent = `${v.speedKmh.toFixed(1)} km/h`;
    g('gear').textContent = `档位 ${v.gearName}`;
    g('gradient').textContent = `坡度 ${v.gradientPct.toFixed(1)}%`;
    g('position').textContent = `第 ${v.position} / ${v.fieldSize} 位`;
    g('remaining').textContent = `剩余 ${v.remainingKm.toFixed(2)} km`;
    g('energy-bar').style.width = `${Math.max(0, v.energyFrac * 100).toFixed(1)}%`;
    g('energy-text').textContent = `体力 ${(v.energyFrac * 100).toFixed(0)}%`;
    this.drawElev(v.progress);
    g('countdown').textContent = v.countdown === null ? '' : v.countdown > 0 ? String(v.countdown) : 'GO!';
    if (v.phase === 'finished') this.showResults(v.results);
  }

  private drawElev(progress: number): void {
    const canvas = this.el.get('elev') as HTMLCanvasElement;
    const ctx = canvas.getContext('2d');
    if (!ctx || this.heights.length === 0) return;
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    const min = Math.min(...this.heights);
    const span = Math.max(...this.heights) - min || 1;
    ctx.strokeStyle = '#8fd0ff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    this.heights.forEach((yv, i) => {
      const x = (i / (this.heights.length - 1)) * w;
      const y = h - 5 - ((yv - min) / span) * (h - 10);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.fillStyle = '#ffd54a';
    ctx.fillRect(progress * w - 1, 0, 2, h);
  }

  private showResults(rows: ResultRow[]): void {
    const box = this.el.get('results')!;
    box.style.display = 'block';
    box.innerHTML = `<h2>比赛结果</h2>` + rows.map((r, i) =>
      `<div class="row${r.id === 0 ? ' me' : ''}"><span>${i + 1}. ${r.name}</span><span>${r.time.toFixed(1)}s</span><span>${r.avgPower.toFixed(0)}W</span></div>`
    ).join('') + `<p>按 R 重新开始</p>`;
  }
}
```

- [ ] **Step 2：实现 `src/style.css`**

```css
* { margin: 0; padding: 0; box-sizing: border-box; }
body { overflow: hidden; font-family: 'Segoe UI', system-ui, sans-serif; background: #9fd4f0; }
#app canvas { display: block; }
#hud { position: fixed; inset: 0; pointer-events: none; color: #fff; text-shadow: 0 1px 3px rgba(0,0,0,.6); }
#top-left { position: absolute; top: 16px; left: 16px; font-size: 20px; line-height: 1.6; }
#top-right { position: absolute; top: 16px; right: 16px; font-size: 20px; line-height: 1.6; text-align: right; }
#speed { font-size: 34px; font-weight: 700; font-variant-numeric: tabular-nums; }
#bottom-left { position: absolute; bottom: 20px; left: 16px; width: 260px; }
.bar { height: 14px; background: rgba(0,0,0,.35); border-radius: 7px; overflow: hidden; }
#energy-bar { height: 100%; width: 100%; background: linear-gradient(90deg,#4dbd8a,#ffd54a,#e0533d); }
#energy-text { font-size: 14px; margin-top: 4px; }
#elev { position: absolute; bottom: 20px; right: 16px; background: rgba(0,0,0,.3); border-radius: 6px; }
#countdown { position: fixed; inset: 0; display: flex; align-items: center; justify-content: center; font-size: 120px; font-weight: 800; pointer-events: none; }
#results { position: fixed; top: 50%; left: 50%; transform: translate(-50%,-50%); background: rgba(12,18,28,.92); padding: 28px 36px; border-radius: 12px; color: #fff; min-width: 340px; display: none; }
#results .row { display: flex; gap: 18px; justify-content: space-between; padding: 4px 0; font-variant-numeric: tabular-nums; }
#results .row.me { color: #ffd54a; font-weight: 700; }
#overlay-start { position: fixed; inset: 0; background: rgba(12,18,28,.85); display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 20px; color: #fff; z-index: 10; }
#overlay-start h1 { font-size: 44px; }
#overlay-start p { opacity: .8; }
#btn-start { font-size: 20px; padding: 10px 32px; border-radius: 8px; border: 0; background: #ffd54a; cursor: pointer; }
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build`，预期：均通过
- [ ] **Step 4：提交** — `git add src/ui/hud.ts src/style.css` 然后 `git commit -m "✨ feat(ui): add dom hud with energy bar, elevation profile and results"`

### Task 15：游戏组装与验收

**Files:**
- Create: `src/game.ts`
- Modify: `src/main.ts`

- [ ] **Step 1：实现 `src/game.ts`**

```ts
import * as THREE from 'three';
import { createRace, standings, stepRace } from './sim/race';
import type { RaceState } from './sim/race';
import { buildTrack } from './sim/trackData';
import type { TrackSample } from './sim/track';
import { GEARS } from './sim/params';
import { createLoop } from './core/loop';
import { InputController } from './input';
import { createScene } from './render/scene';
import { buildTerrain } from './render/terrain';
import { buildTrackMesh } from './render/trackMesh';
import { buildRiderMesh, placeRider } from './render/riders';
import { ChaseCamera } from './render/camera';
import { Hud } from './ui/hud';

const JERSEYS = [0xffd54a, 0xe0533d, 0x4d8fd6, 0x8a5cd6, 0x4dbd8a, 0xd68a4d, 0xd64d9e, 0x5a6a7a];

export class Game {
  private track = buildTrack();
  private state = createRace(this.track);
  private prev = this.state;
  private meshes: THREE.Object3D[] = [];
  private ctx: ReturnType<typeof createScene>;
  private cam: ChaseCamera;
  private loop: ReturnType<typeof createLoop>;
  private tmp = new THREE.Vector3();

  constructor(private hud: Hud, private input: InputController) {
    this.ctx = createScene(document.querySelector<HTMLElement>('#app')!);
    this.ctx.scene.add(buildTerrain(this.track));
    this.ctx.scene.add(buildTrackMesh(this.track));
    for (let i = 0; i < 8; i++) {
      const m = buildRiderMesh(JERSEYS[i]);
      this.meshes.push(m);
      this.ctx.scene.add(m);
    }
    this.cam = new ChaseCamera(this.ctx.camera);
    this.hud.setProfile(this.track);
    this.loop = createLoop((dt) => this.update(dt), (a, fdt) => this.render(a, fdt));
    window.addEventListener('keydown', (e) => {
      if (e.key.toLowerCase() === 'r' && this.state.phase === 'finished') this.start();
    });
  }

  start(): void {
    this.state = createRace(this.track);
    this.prev = this.state;
    this.input.reset();
    this.hud.clearResults();
    this.loop.stop();
    this.loop.start();
  }

  private update(dt: number): void {
    this.prev = this.state;
    this.state = stepRace(this.state, this.track, this.input.command(), dt);
  }

  private render(alpha: number, frameDt: number): void {
    const s = this.state;
    for (let i = 0; i < s.riders.length; i++) {
      const p = this.prev.riders[i];
      const c = s.riders[i];
      placeRider(this.meshes[i], this.track.sampleAt(p.dist + (c.dist - p.dist) * alpha), p.lateral + (c.lateral - p.lateral) * alpha);
    }
    const ps = this.track.sampleAt(this.interp(this.prev.riders[0].dist, s.riders[0].dist, alpha));
    this.tmp.set(ps.x, ps.y, ps.z);
    this.cam.update(this.tmp, ps.heading, ps.gradient, frameDt);
    this.hud.update(this.view(ps));
    this.ctx.renderer.render(this.ctx.scene, this.ctx.camera);
  }

  private interp(a: number, b: number, alpha: number): number {
    return a + (b - a) * alpha;
  }

  private view(ps: TrackSample) {
    const s = this.state;
    const p = s.riders[0];
    return {
      speedKmh: p.speed * 3.6,
      gearName: GEARS[p.gear],
      energyFrac: p.energy / p.type.maxEnergy,
      gradientPct: ps.gradient * 100,
      remainingKm: Math.max(0, s.trackLength - p.dist) / 1000,
      position: standings(s).findIndex((r) => r.isPlayer) + 1,
      fieldSize: s.riders.length,
      progress: Math.min(1, p.dist / s.trackLength),
      countdown: s.phase === 'countdown' ? Math.ceil(s.countdown) : null,
      phase: s.phase,
      results: s.results,
    };
  }
}
```

- [ ] **Step 2：用最终内容替换 `src/main.ts`**

```ts
import './style.css';
import { Game } from './game';
import { Hud } from './ui/hud';
import { InputController } from './input';

const hud = new Hud();
const input = new InputController();
const game = new Game(hud, input);
document.querySelector('#btn-start')!.addEventListener('click', () => {
  document.querySelector<HTMLElement>('#overlay-start')!.style.display = 'none';
  input.attach();
  game.start();
});
```

- [ ] **Step 3：全量验证**

Run: `npm test` — 预期：全部 PASS（约 40+ 项）
Run: `npm run typecheck` — 预期：0 错误
Run: `npm run build` — 预期：构建成功
Run: `npm run dev` — 手动验收（见 Part C 清单）

- [ ] **Step 4：提交**

```bash
git add src/game.ts src/main.ts
git commit -m "✨ feat(game): wire sim, render and hud into playable race loop"
```

---

## Part C 验收与风险

### C1 规格覆盖映射（设计文档 P0 → 任务）

| 规格条目 | 实现任务 |
|---|---|
| 骑行物理（功率→速度纯函数） | Task 2 |
| 体力系统（4 档 + 透支限制） | Task 1、3 |
| 跟车（约 30% 风阻收益） | Task 2（draftDrag）、6（判定） |
| 赛道（spline + 数据驱动高程） | Task 4、5 |
| 车手控制（档位 + 左右占位） | Task 10 |
| AI 车手（跟轮/响应/掉队/冲刺/分型） | Task 7 |
| 比赛流程（倒计时→比赛→结算） | Task 8、15 |
| HUD（速度/档位/体力/坡度/距离/名次/高程图） | Task 14 |
| 摄像机（追尾第三人称） | Task 13 |
| 60fps / 单元测试验收 | Task 9（固定步长）、各 sim 任务、C2 手动清单 |

### C2 手动验收清单（`npm run dev`）

1. 点击"开始比赛"，3-2-1 倒计时后出发，HUD 全部字段实时刷新。
2. 平路巡航速度约 38~40 km/h；爬坡掉至约 20 km/h；坡度显示随地形变化。
3. 跟在前车正后方时同样档位速度明显更高（风阻收益可感知）。
4. 持续冲刺档 → 体力条快速下降 → 耗尽后速度被压到约 26 km/h 且名次下滑。
5. AI 表现：有人爬坡进攻、被进攻者跟轮响应、体力不足者掉队、终点前发起冲刺。
6. 冲线后弹出结算（名次/时间/均功率），按 R 可重开，结果与上局不同但机制一致。
7. 浏览器渲染稳定 60fps（DevTools Performance 面板抽查）。

### C3 风险与对策

| 风险 | 对策 |
|---|---|
| 体力/功率参数失衡（AI 集体崩盘或过于轻松） | 参数集中在 `params.ts` / `AI_FIELD`；race 长测锁定行为回归 |
| 赛道局部坡度尖峰（CR 过冲） | trackData 测试断言 max gradient < 9% |
| 每帧 `standings()` 排序与 DOM 写入开销 | 8 个元素排序 + 十几个 textContent，微不足道；如成为瓶颈再降频 |
| 地形/树加载耗时（nearest 线性扫描） | 仅启动时执行一次（约 23k 顶点 × 粗步扫描），可接受 |

### C4 后续（P1，不在本计划内）

音效、摄像机切换、赛后回放（固定步长确定性已为此铺路：录制玩家输入即可完整重演）。

