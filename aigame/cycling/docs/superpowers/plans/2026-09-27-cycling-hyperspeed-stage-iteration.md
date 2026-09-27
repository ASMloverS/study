# 公路车迭代实现计划：极速 150km 赛段 / 全解锁操控 / 道具箱补给

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `docs/superpowers/specs/2026-09-27-cycling-hyperspeed-stage-design.md`：速度层街机化（巡航 ~400/冲刺 ~530/下坡 650+ km/h）、150km 点对点赛段（15-25min 完赛）、操控全解锁（齿比 1T~∞、踏频 0~∞、效率系统删除）、马里奥式道具箱补给、体力几乎无压力、AI 稍弱、速度感视觉（FOV/速度线/玩家车高亮/模型细化）、条带地形+雾、俯视小地图、踏频仪表盘。

**Architecture:** 顺序依赖链——先解锁操控删效率（Task 1，避免效率曲线与后续超高速踏频冲突），再提速+体力经济（Task 2，两者必须同任务否则骑手立即撞墙），然后换 150km 赛道（Task 3）→ AI 适配（4）→ 道具箱 sim（5）与渲染（6）→ 视觉（7）→ 地形（8）→ 小地图（9）→ HUD 仪表盘（10）→ 回归（11）。确定性保持贯穿：道具拾取用 per-rider 游标 + boolean[]，全部可 JSON 序列化。

**Tech Stack:** 不变（TypeScript strict + Vite + Three.js + Vitest）。

---

## Part A 关键决策与预演数值

### A1 决策落点

| 项 | 决策 |
|---|---|
| 顺序 | 解锁删效率 → 提速+体力 → 赛道 → AI → 道具 → 视觉 → 地形 → 小地图 → HUD → 回归 |
| 操控 | cog `Math.max(1, ...)` 无上限；cadTarget `Math.max(0, ...)` 无上限；删 `cadenceEfficiency` 与全部曲线参数、race 乘子与 sprint 分支 |
| 物理 | `gearRatios [3.6,6,9,15]`、`CdA 0.002`、`maxDriveForce 450`（`fDrive = min(P/max(v,minSpeed), 450)`） |
| 体力 | `drainRatios [0.6,1,1.5,2.5]`、`drainBase 12`（drain=drainRatios[gear]²×12，与 ftp 解耦）、`regenRate 0.0015`（gear≤1 且 effort>0 时 +0.15%/s maxEnergy）；effort=0（滑行/完赛）时不耗不回 |
| 赛道 | 点对点 200 控制点 × 750m ≈ 150km；采样 8000；gradient 中心差分 ±2 采样平滑；高程关键点分段（含 8-15% 陡坡与 −3~−7.5% 长下坡）；2 个 π 掉头弯（跨 4 控制点 ≈ 3km，半径 ~480m） |
| 道具箱 | 簇中心 5,10,…,145km（29 簇）× 横向 [−1.7,0,+1.7]，半径 0.75，每箱 +10% maxEnergy；per-rider `boxCursor`（首个 d>dist 的箱索引，O(1) 摊销）+ `collected: boolean[]`（渲染隐藏用） |
| AI | `AI_FIELD` ftp ×0.9、sprintDist ×15；解析换挡自限 [10,36] 不变 |
| 视觉 | 相机 6m/2.4m、FOV `65+min(23, speed/7)`；速度线 48 条（speed>30 m/s 显现）；玩家车 1.15×亮红+backface 描边；轮辐×8/弯把/管型/前倾 |
| 地形 | 条带：每 3 个采样（~56m）一截面 × 横向 13 列（−400..400m）；雾 1500-2800、far 3000 |
| HUD | 踏频仪表盘弧 0-240rpm（目标钉端点）+数字；速度整数；剩余 km 0 小数；最后 5km 提示；音乐 sprint ≤5km |

### A2 预演数值（测试期望依据）

- 平路终速：1800W ≈ 104.5 m/s（376 km/h）；4500W ≈ 147.5 m/s（531 km/h）
- 牵引上限：v=1 时 `fDrive=min(4500,450)=450`，a=(450−3.826−0.001226)/78≈5.72 m/s²
- 下坡 −5% + 4500W 终速 ≈ 215 m/s（774 km/h）；−3% ≈ 188 m/s（677 km/h）
- drain：gear0 4.32 / gear1 12 / gear2 27 / gear3 75 J/s；regen gear≤1 = 60 J/s（40000 池）→ 巡航净 +48 J/s
- 赛道：length ∈ [148.5, 151.5]km；max 高程 ∈ [900,1000]m；|gradient| ≤ 0.16 且存在 ≥0.07 与 ≤−0.05 段
- 完赛预估：~110 m/s 均速 ≈ 22-23min；reckless（全程 gear3）< 1500s 契约
- 现有 4.4km 环道在 Task 2-4 期间：全场完赛 ~40-90s（全绿中间态）

### A3 文件清单

```
修改  src/sim/params.ts         解锁/效率删/提速/体力/道具箱/AI 常量
修改  src/sim/physics.ts        maxDriveForce
修改  src/sim/energy.ts         gear 键 drain + regen
重写  src/sim/drivetrain.ts     删 cadenceEfficiency
重写  src/sim/trackData.ts      150km 点对点生成
修改  src/sim/track.ts          gradient 平滑（预计算 grad 数组）
修改  src/sim/race.ts           钳位删/乘子删/道具拾取/体力接线/AI 适配
修改  src/sim/types.ts          RiderState +collected +boxCursor
修改  src/input.ts              无（交互不变）
修改  src/render/camera.ts      距离/高度/FOV
重写  src/render/riders.ts      模型细化+玩家高亮
重写  src/render/terrain.ts     条带地形
修改  src/render/scene.ts       雾+far
修改  src/render/trackMesh.ts   删补给门+道具箱网格
新增  src/render/speedLines.ts  速度线粒子
新增  src/render/minimap.ts     俯视小地图
修改  src/ui/hud.ts             仪表盘/文案/道具刻度/浮字
修改  src/audio.ts              拾取 blip
修改  src/game.ts               接线（FOV/速度线/小地图/道具/音乐5km/仪表盘）
修改  index.html                仪表盘/小地图 canvas + 帮助行 + 浮字样式
测试  drivetrain/physics/energy/trackData/race 重写相应 describe；新增 minimap.test.ts
```

---

## Part B 实施任务（TDD，按序执行）

### Task 0：基线

- [ ] `npm test` 全绿（91/91，记录）；`npm run typecheck` 0 错误；`npm run build` 成功——不绿先停下排查

### Task 1：操控全解锁 + 效率系统删除

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/race.ts`
- Rewrite: `src/sim/drivetrain.ts`
- Test: `src/sim/drivetrain.test.ts`、`src/sim/race.test.ts`

- [ ] **Step 1：更新 `src/sim/drivetrain.test.ts`**——删除整个 `cadenceEfficiency` describe（sprint 用例一并删除），其余不动
- [ ] **Step 2：更新 `src/sim/race.test.ts`**：
  1. 用例 `cog shifts in continuous teeth via cogDelta and clamps to 10..36` 替换为：

```ts
  it('cog shifts freely with floor 1 and no upper clamp', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const half: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0.5 };
    s = stepRace(s, track, half, RACE.dt);
    expect(s.riders[0].cog).toBeCloseTo(16.5, 6);
    const heavy: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: -100 };
    s = stepRace(s, track, heavy, RACE.dt);
    expect(s.riders[0].cog).toBe(1);
    const light: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 500 };
    s = stepRace(s, track, light, RACE.dt);
    expect(s.riders[0].cog).toBe(501);
  });
```

  2. 用例 `cadence target adjusts via cadDelta and clamps to 80..150` 替换为：

```ts
  it('cadence target adjusts freely with floor 0 and no upper clamp', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const up: RiderCommand = { gear: 1, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, up, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    for (let i = 0; i < 100; i++) s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(0);
  });
```

  3. 删除三个用例：`sprint gear unlocks cadence target above 150 and keeps it after exit`、`sprint gear keeps full efficiency at extreme cadence`、`ai gear 3 does not trigger sprint efficiency`（语义随效率系统删除）

- [ ] **Step 3：跑测试确认失败** — `npm test`：cog/cadTarget 新钳位用例失败（仍钳 10..36 / 80..150）
- [ ] **Step 4：`src/sim/params.ts`**——`DRIVETRAIN` 中：删 `cogMin/cogMax/cadFullLo/cadFullHi/cadFloor/cadCeil/effMin/cadTargetMin/cadTargetMax`，增 `aiCogMin: 10, aiCogMax: 36, cogFloor: 1`；其余（chainring/cogStep/cogHoldRate/cadHoldRate/defaultCogTeeth/wheelCirc/aiTargetCadence/aiShiftHysteresis/aiShiftMinSpeed/terrain 系列/cadenceStep/cadOffsetMax）不动
- [ ] **Step 5：重写 `src/sim/drivetrain.ts`**（删 cadenceEfficiency，aiShift 用新常量）：

```ts
import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / cog;
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function terrainCadence(gradient: number): number {
  if (gradient > D.climbGradient) return D.climbCadence;
  if (gradient < D.descentGradient) return D.descentCadence;
  return D.flatCadence;
}

export function aiShift(speed: number, currentCog: number, target: number = D.aiTargetCadence): number {
  if (speed < D.aiShiftMinSpeed) return currentCog;
  const ideal = Math.min(D.aiCogMax, Math.max(D.aiCogMin, (target * D.chainring * D.wheelCirc) / (speed * 60)));
  const err = (cog: number) => Math.abs(cadence(speed, cog) - target);
  return err(ideal) < err(currentCog) - D.aiShiftHysteresis ? ideal : currentCog;
}
```

- [ ] **Step 6：`src/sim/race.ts`**——玩家块与功率块替换为：

```ts
    if (r.isPlayer) {
      r.cog = Math.max(DRIVETRAIN.cogFloor, r.cog + cmd.cogDelta);
      r.cadTarget = Math.max(0, r.cadTarget + cmd.cadDelta);
    } else if (shiftTick) {
      r.cog = aiShift(r.speed, r.cog);
    }
    const cad = cadence(r.speed, r.cog);
    const softPedal = r.isPlayer && cad >= r.cadTarget;
    const effort = r.finishTime !== null || softPedal ? 0 : targetPower(cmd.gear, r.type.ftp, r.energy);
    r.power = effort;
```

- [ ] **Step 7：验证** — `npm test` 全绿（91−5=86：删 5 个效率/解锁用例）；`npm run typecheck` 0 错误（确认无 cadenceEfficiency 残留引用）
- [ ] **Step 8：提交** — `git add src/sim/params.ts src/sim/drivetrain.ts src/sim/drivetrain.test.ts src/sim/race.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): remove all player control clamps and delete cadence efficiency system"`

### Task 2：物理速度层 + 体力经济

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/physics.ts`、`src/sim/energy.ts`、`src/sim/race.ts`
- Test: `src/sim/physics.test.ts`（重写数值）、`src/sim/energy.test.ts`（重写）、`src/sim/race.test.ts`（avgPower 区间）

- [ ] **Step 1：重写 `src/sim/physics.test.ts` 数值用例**（保留 describe 结构，替换期望值）：

```ts
import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';
import { PHYS } from './params';

describe('stepSpeed', () => {
  it('caps drive force at maxDriveForce at low speed', () => {
    const v = stepSpeed(1, 4500, 0, false, 1 / 60);
    expect(v).toBeCloseTo(1 + (450 - 3.826 - 0.001226) / 78 / 60, 4);
  });
  it('converges to ~104 m/s at 1800W on flat', () => {
    let v = 50;
    for (let i = 0; i < 6000; i++) v = stepSpeed(v, 1800, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(100);
    expect(v).toBeLessThan(107);
  });
  it('converges to ~147 m/s at 4500W on flat', () => {
    let v = 50;
    for (let i = 0; i < 6000; i++) v = stepSpeed(v, 4500, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(143);
    expect(v).toBeLessThan(150);
  });
  it('reaches ~200 m/s on -5% descent with sprint power', () => {
    let v = 50;
    for (let i = 0; i < 6000; i++) v = stepSpeed(v, 4500, -0.05, false, 1 / 60);
    expect(v).toBeGreaterThan(195);
    expect(v).toBeLessThan(235);
  });
  it('never returns negative', () => {
    expect(stepSpeed(0.5, 0, 0.08, false, 1 / 60)).toBeGreaterThanOrEqual(0);
  });
});
```

- [ ] **Step 2：重写 `src/sim/energy.test.ts`**：

```ts
import { describe, expect, it } from 'vitest';
import { drainRate, stepEnergy } from './energy';

describe('drainRate', () => {
  it('drains by gear ratio squared times base', () => {
    expect(drainRate(0)).toBeCloseTo(0.36 * 12, 6);
    expect(drainRate(1)).toBeCloseTo(12, 6);
    expect(drainRate(2)).toBeCloseTo(27, 6);
    expect(drainRate(3)).toBeCloseTo(75, 6);
  });
});

describe('stepEnergy', () => {
  it('drains sprint at 75 J/s', () => {
    expect(stepEnergy(40000, 3, 40000, 1)).toBeCloseTo(40000 - 75, 6);
  });
  it('regens while cruising at low gear', () => {
    expect(stepEnergy(30000, 1, 40000, 1)).toBeCloseTo(30000 - 12 + 60, 6);
  });
  it('caps at max energy', () => {
    expect(stepEnergy(39995, 0, 40000, 1)).toBe(40000);
  });
  it('never goes below zero', () => {
    expect(stepEnergy(10, 3, 40000, 1)).toBe(0);
  });
});
```

- [ ] **Step 3：跑测试确认失败** — physics 新数值、energy 新签名均挂
- [ ] **Step 4：`src/sim/params.ts`**：
  - `PHYS.CdA: 0.32` → `0.002`；`PHYS` 增 `maxDriveForce: 450`
  - `RACE.gearRatios: [0.6, 1, 1.5, 2.5]` → `[3.6, 6, 9, 15]`
  - `RACE.drainBase: 40` → `12`；`RACE` 增 `drainRatios: [0.6, 1, 1.5, 2.5], regenRate: 0.0015`
- [ ] **Step 5：`src/sim/physics.ts`**——`fDrive` 行替换：

```ts
  const fDrive = Math.min(power / Math.max(speed, PHYS.minSpeed), PHYS.maxDriveForce);
```

- [ ] **Step 6：重写 `src/sim/energy.ts`**：

```ts
import { RACE } from './params';
import type { GearId } from './types';

export function drainRate(gear: GearId): number {
  const r = RACE.drainRatios[gear];
  return r * r * RACE.drainBase;
}

export function targetPower(gear: GearId, ftp: number, energy: number): number {
  const raw = ftp * RACE.gearRatios[gear];
  return energy <= 0 ? Math.min(raw, ftp * RACE.emptyCapRatio) : raw;
}

export function stepEnergy(energy: number, gear: GearId, maxEnergy: number, dt: number): number {
  let e = energy - drainRate(gear) * dt;
  if (gear <= 1) e += RACE.regenRate * maxEnergy * dt;
  return Math.min(maxEnergy, Math.max(0, e));
}
```

- [ ] **Step 7：`src/sim/race.ts`**——补给块内 `r.energy = stepEnergy(r.energy, effort, r.type.ftp, dt);` 替换为（滑行/完赛不耗不回）：

```ts
      if (effort > 0) r.energy = stepEnergy(r.energy, cmd.gear, r.type.maxEnergy, dt);
```

- [ ] **Step 8：`src/sim/race.test.ts`**——full-race 用例 avgPower 区间 `toBeGreaterThan(150)`/`toBeLessThan(400)` 改为 `(500)`/`(2500)`（新功率层 1080-4500W，均功率含滑行段）
- [ ] **Step 9：验证** — `npm test` 全绿、typecheck 0 错误；记录：现 4.4km 环道上全场完赛时间（预估 40-90s）与 reckless 完赛时间（<500s 契约暂保持）
- [ ] **Step 10：提交** — `git add src/sim/params.ts src/sim/physics.ts src/sim/physics.test.ts src/sim/energy.ts src/sim/energy.test.ts src/sim/race.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): hyperspeed physics with traction cap and gear-keyed energy economy"`（正文附实测）

### Task 3：150km 点对点赛道

**Files:**
- Rewrite: `src/sim/trackData.ts`
- Modify: `src/sim/track.ts`
- Test: `src/sim/trackData.test.ts`（重写）、`src/sim/track.test.ts`（gradient 平滑用例）、`src/sim/race.test.ts`（时长契约）

- [ ] **Step 1：重写 `src/sim/trackData.test.ts`**：

```ts
import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack hyperspeed stage', () => {
  const track = buildTrack();
  it('is a ~150km point-to-point course', () => {
    expect(track.length).toBeGreaterThan(148500);
    expect(track.length).toBeLessThan(151500);
  });
  it('has mountain profile between 900 and 1000m max elevation', () => {
    let max = -Infinity;
    for (let d = 0; d < track.length; d += 100) max = Math.max(max, track.sampleAt(d).y);
    expect(max).toBeGreaterThan(900);
    expect(max).toBeLessThan(1000);
  });
  it('keeps gradients within ±16% with steep climbs and descents', () => {
    let maxG = 0;
    let minG = 0;
    for (let d = 0; d < track.length; d += 20) {
      const g = track.sampleAt(d).gradient;
      maxG = Math.max(maxG, g);
      minG = Math.min(minG, g);
    }
    expect(maxG).toBeLessThanOrEqual(0.16);
    expect(minG).toBeGreaterThanOrEqual(-0.16);
    expect(maxG).toBeGreaterThanOrEqual(0.07);
    expect(minG).toBeLessThanOrEqual(-0.05);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — 现环道 ~4.4km、无高山
- [ ] **Step 3：重写 `src/sim/trackData.ts`**：

```ts
import { Track } from './track';

const SPACING = 750;
const COUNT = 200;

const PROFILE_KM: [number, number][] = [
  [0, 0], [10, 50], [20, 20], [30, 80], [35, 300], [38, 520], [41, 540], [44, 700],
  [47, 680], [50, 830], [53, 850], [56, 950], [60, 930], [65, 900], [72, 880], [80, 700],
  [85, 420], [90, 300], [95, 150], [100, 90], [105, 80], [112, 160], [120, 60], [128, 150],
  [135, 40], [142, 30], [150, 0],
];

function elevationAtKm(km: number): number {
  if (km <= 0 || km >= 150) return km <= 0 ? PROFILE_KM[0][1] : PROFILE_KM[PROFILE_KM.length - 1][1];
  for (let i = 1; i < PROFILE_KM.length; i++) {
    if (km <= PROFILE_KM[i][0]) {
      const [k0, h0] = PROFILE_KM[i - 1];
      const [k1, h1] = PROFILE_KM[i];
      const t = (km - k0) / (k1 - k0);
      const s = 0.5 - 0.5 * Math.cos(Math.PI * t);
      return h0 + (h1 - h0) * s;
    }
  }
  return 0;
}

function turn(u: number, lo: number, hi: number): number {
  const t = Math.min(1, Math.max(0, (u - lo) / (hi - lo)));
  return t * t * (3 - 2 * t);
}

export function buildTrack(): Track {
  const ctrl: [number, number, number][] = [];
  let x = 0;
  let z = 0;
  const headingAt = (u: number) =>
    0.6 * Math.sin(u * Math.PI * 6) + Math.PI * turn(u, 0.42, 0.45) + Math.PI * turn(u, 0.78, 0.81);
  for (let i = 0; i < COUNT; i++) {
    ctrl.push([x, elevationAtKm((i / COUNT) * 150), z]);
    const mid = headingAt((i + 0.5) / COUNT);
    x += Math.cos(mid) * SPACING;
    z += Math.sin(mid) * SPACING;
  }
  return new Track(ctrl, 8000);
}
```

  （heading 直接取累积函数值：正弦项 ±0.6rad 蛇形摆动、两个 turn 窗口各转 π；若可视化出现平面自交，缩小正弦幅度 0.6→0.4 或拉宽 turn 窗口——以 trackData.test 通过与无自交为准，容差内微调不算偏离计划。）

- [ ] **Step 4：`src/sim/track.ts` gradient 平滑**——构造器 `cum` 计算后追加预计算，`sampleAt` 改用插值：

```ts
  private readonly grad: number[];

  // 构造器内（cum 之后）：
  this.grad = [];
  for (let i = 0; i <= samples; i++) {
    const a = Math.max(0, i - 2);
    const b = Math.min(samples, i + 2);
    const pa = this.pts[a];
    const pb = this.pts[b];
    const run = Math.hypot(pb[0] - pa[0], pb[2] - pa[2]) || 1e-6;
    this.grad.push((pb[1] - pa[1]) / run);
  }
```

  `sampleAt` 返回值 `gradient` 改为：

```ts
      gradient: this.grad[lo] + (this.grad[hi] - this.grad[lo]) * u,
```

- [ ] **Step 5：`src/sim/track.test.ts`**——如已有 gradient 用例改为断言平滑性（相邻 50m 采样 |Δgradient| < 0.03）；无则追加：

```ts
  it('gradient is smooth between nearby samples', () => {
    const t = new Track([[0, 0, 0], [100, 10, 0], [200, 10, 100], [300, 0, 100]], 200);
    let maxDelta = 0;
    let prev = t.sampleAt(0).gradient;
    for (let d = 5; d < t.length; d += 5) {
      const g = t.sampleAt(d).gradient;
      maxDelta = Math.max(maxDelta, Math.abs(g - prev));
      prev = g;
    }
    expect(maxDelta).toBeLessThan(0.03);
  });
```

- [ ] **Step 6：`src/sim/race.test.ts` 时长契约**——reckless 用例 `run(..., 900)` → `run(..., 1600)` 且断言 `< 1500`；full-race 守卫 `steps < 60 * 1500` → `60 * 1700`
- [ ] **Step 7：验证** — `npm test` 全绿（全程仿真 ~83000 步，单测总时长升至 ~3-5s，可接受）；typecheck 0 错误；记录全场完赛时间（预估 1250-1500s）
- [ ] **Step 8：提交** — `git add src/sim/trackData.ts src/sim/trackData.test.ts src/sim/track.ts src/sim/track.test.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): 150km point-to-point mountain stage with smoothed gradients"`

### Task 4：AI 适配（稍弱）

**Files:**
- Modify: `src/sim/race.ts`
- Test: `src/sim/race.test.ts`（数值记录，无新用例）

- [ ] **Step 1：`race.ts` `AI_FIELD`**——ftp 全部 ×0.9（315→283.5、306→275.4、286→257.4、293→263.7、298→268.2、295→265.5、300→270）；sprintDist 全部 ×15（120→1800、130→1950、350→5250、300→4500、200→3000、220→3300、240→3600）
- [ ] **Step 2：验证** — `npm test` 全绿；记录：玩家 cruise 完赛名次（应在 AI 之前或至少不落后最快 AI 超过 60s——若玩家 cruise 仍稳定垫底，报告数值，不调参）
- [ ] **Step 3：提交** — `git add src/sim/race.ts` 然后 `git commit -m "✨ feat(sim): scale ai field to 90 percent ftp and stage-length sprints"`

### Task 5：道具箱补给（sim 层）

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/types.ts`、`src/sim/race.ts`、`src/sim/draft.test.ts`、`src/sim/ai.test.ts`
- Test: `src/sim/race.test.ts`（删 2 补给区用例 + 增 4 道具用例）

- [ ] **Step 1：`src/sim/race.test.ts`**：
  1. 删除用例 `feed zone restores energy while passing through` 与 `energy caps at max inside feed zone`
  2. 追加：

```ts
  it('item box picked up when crossing at matching lateral', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 4999.5, speed: 100, lateral: 0, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].collected[1]).toBe(true);
    expect(s.riders[0].energy).toBeCloseTo(before + 4000, -1);
  });
  it('no pickup when lateral misses all boxes', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 4999.5, speed: 100, lateral: 0.85, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].collected.every((c) => !c)).toBe(true);
    expect(s.riders[0].energy).toBe(before);
  });
  it('each box is evaluated exactly once per rider', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 4999.9, speed: 2, lateral: 0, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBeCloseTo(before + 4000, -1);
  });
  it('ai riders also pick up boxes', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r, i) => (i === 4 ? { ...r, dist: 4999.5, speed: 100, lateral: 0, energy: 10000 } : r)) };
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[4].collected.some((c) => c)).toBe(true);
    expect(s.riders[4].energy).toBeCloseTo(10000 + 0.1 * s.riders[4].type.maxEnergy, -1);
  });
```

  说明：箱序按簇排序——簇 k 的三箱索引 3k/3k+1/3k+2 对应横向 −1.7/0/+1.7，第一簇（5km）中间箱索引 1；cadTarget 0 使 soft-pedal 生效（effort=0 → 无消耗无恢复，能量断言纯净）；第 3 例速度 2 m/s 确保跨越 5000 恰好发生在一个 tick 且后续 tick 不再重复计能量。

- [ ] **Step 2：跑测试确认失败** — collected 字段不存在（编译期 vitest 报错或断言失败）、能量无跳变
- [ ] **Step 3：`src/sim/params.ts`**——`RACE` 删 `feedZones`、`feedZoneGain`，增：

```ts
  boxClusterEvery: 5000,
  boxFirst: 5000,
  boxLast: 145000,
  boxOffsets: [-1.7, 0, 1.7],
  boxRadius: 0.75,
  boxGain: 0.1,
```

  文件末尾新增（模块级常量，确定性）：

```ts
export interface ItemBox {
  d: number;
  lat: number;
}

export const ITEM_BOXES: ItemBox[] = (() => {
  const boxes: ItemBox[] = [];
  for (let d = RACE.boxFirst; d <= RACE.boxLast; d += RACE.boxClusterEvery) {
    for (const lat of RACE.boxOffsets) boxes.push({ d, lat });
  }
  return boxes;
})();
```

- [ ] **Step 4：`src/sim/types.ts`**——`RiderState` 增（`cog: number;` 之后）：

```ts
  collected: boolean[];
  boxCursor: number;
```

- [ ] **Step 5：`src/sim/race.ts`**：
  1. import 增 `ITEM_BOXES`（params）
  2. `makeRider` 增：`collected: new Array<boolean>(ITEM_BOXES.length).fill(false), boxCursor: 0,`
  3. 补给块：删除 `for (const [lo, hi] of RACE.feedZones) {...}` 整段，替换为（`const prevDist = r.dist;` 需在 `r.dist += r.speed * dt;` 之前捕获）：

```ts
      const prevDist = r.dist;
      r.dist += r.speed * dt;
      while (r.boxCursor < ITEM_BOXES.length && ITEM_BOXES[r.boxCursor].d <= r.dist) {
        const box = ITEM_BOXES[r.boxCursor];
        if (
          !r.collected[r.boxCursor] &&
          prevDist < box.d &&
          Math.abs(r.lateral - box.lat) < RACE.boxRadius
        ) {
          r.collected[r.boxCursor] = true;
          r.energy = Math.min(r.type.maxEnergy, r.energy + RACE.boxGain * r.type.maxEnergy);
        }
        r.boxCursor++;
      }
```

- [ ] **Step 6：测试工厂补字段**——`src/sim/draft.test.ts` 与 `src/sim/ai.test.ts` 的 rider 工厂在 `cog: 16,` 后追加 `collected: [], boxCursor: 0,`（保持 typecheck 绿）
- [ ] **Step 7：验证** — `npm test` 全绿；typecheck 0 错误；确定性用例（JSON.stringify）确认 collected/boxCursor 序列化正常
- [ ] **Step 8：提交** — `git add src/sim/params.ts src/sim/types.ts src/sim/race.ts src/sim/race.test.ts src/sim/draft.test.ts src/sim/ai.test.ts` 然后 `git commit -m "✨ feat(sim): mario-style item boxes replace feed zones with per-rider pickup state"`

### Task 6：道具箱渲染 + 音效 + HUD 刻度

**Files:**
- Modify: `src/render/trackMesh.ts`、`src/audio.ts`、`src/ui/hud.ts`、`src/game.ts`、`index.html`、`src/style.css`

> 渲染层无单测（既定偏差），门禁：typecheck + build + test 全绿。

- [ ] **Step 1：`src/render/trackMesh.ts`**——删除补给门循环（`for (const [lo] of RACE.feedZones) ...`），保留终点门；新增导出：

```ts
export function buildItemBoxes(): { group: THREE.Group; meshes: THREE.Mesh[] } {
  const group = new THREE.Group();
  const meshes: THREE.Mesh[] = [];
  ITEM_BOXES.forEach((box, i) => {
    const cluster = Math.floor(i / 3);
    const mat = new THREE.MeshPhongMaterial({
      color: new THREE.Color().setHSL((cluster % 29) / 29, 0.85, 0.6),
      transparent: true,
      opacity: 0.55,
    });
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.9, 0.9), mat);
    mesh.userData.d = box.d;
    mesh.userData.lat = box.lat;
    group.add(mesh);
    meshes.push(mesh);
  });
  return { group, meshes };
}
```

  import 增 `ITEM_BOXES`（params）。箱子摆放位置在 game.ts 每帧用 track.sampleAt 对齐（跟随地形）。
- [ ] **Step 2：`src/audio.ts`**——Music 类内新增公共方法（复用现有 ctx/master，未 start 时静默跳过）：

```ts
  blip(): void {
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.frequency.setValueAtTime(880, this.ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(1760, this.ctx.currentTime + 0.12);
    gain.gain.setValueAtTime(0.25, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.15);
    osc.connect(gain);
    gain.connect(this.master!);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.16);
  }
```

- [ ] **Step 3：`src/ui/hud.ts`**——`setProfile` 的 zones 语义改为道具簇位置（不改签名）；`update` 增拾取浮字方法：

```ts
  flashPickup(text: string): void {
    const host = document.querySelector('#hud')!;
    const el = document.createElement('div');
    el.className = 'pickup-float';
    el.textContent = text;
    host.appendChild(el);
    setTimeout(() => el.remove(), 900);
  }
```

  体力条闪白：`flashPickup` 内同步 `this.el.get('energy-bar')!.classList.add('flash'); setTimeout(() => this.el.get('energy-bar')!.classList.remove('flash'), 250);`
- [ ] **Step 4：`src/game.ts`**：
  1. 构造：`const boxes = buildItemBoxes(); this.ctx.scene.add(boxes.group); this.boxMeshes = boxes.meshes;`；私有字段 `private boxMeshes: THREE.Mesh[] = [];`、`private pickedCount = 0;`
  2. render 内：每帧对每个 mesh `sampleAt(mesh.userData.d)` 摆位（y +1.2 悬浮）、`mesh.rotation.y += frameDt * 2; mesh.rotation.x += frameDt;`、玩家已拾取则 `mesh.visible = !s.riders[0].collected[i]`
  3. 拾取检测：`const count = s.riders[0].collected.filter(Boolean).length; if (count > this.pickedCount) { this.pickedCount = count; this.hud.flashPickup('+10%'); this.music.blip(); }`
  4. 音乐冲刺窗：`remaining > 800` → `remaining > 5000`
  5. `setProfile` 第二参数改为道具簇位置数组：`Array.from({ length: 29 }, (_, i) => (i + 1) * 5000)`
- [ ] **Step 5：`index.html` + `src/style.css`**——增 `.pickup-float`（绝对定位中央偏上、上浮渐隐动画 0.9s、亮青色粗体）与 `#energy-bar.flash`（白色高亮 250ms 过渡）
- [ ] **Step 6：验证** — `npm run typecheck && npm run build && npm test` 全绿
- [ ] **Step 7：提交** — `git add src/render/trackMesh.ts src/audio.ts src/ui/hud.ts src/game.ts index.html src/style.css` 然后 `git commit -m "✨ feat(render): rainbow item boxes with pickup flash, float text and blip"`

### Task 7：速度感视觉（相机/FOV/速度线）与玩家车（高亮/模型细化）

**Files:**
- Modify: `src/render/camera.ts`、`src/render/riders.ts`、`src/game.ts`
- Create: `src/render/speedLines.ts`

> 渲染层无单测，门禁：typecheck + build + test 全绿。

- [ ] **Step 1：`src/render/camera.ts`**——距离 8.5→6、高度 3.2→2.4、前瞻 12→18、动态 FOV：

```ts
import * as THREE from 'three';

export class ChaseCamera {
  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number, speed: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.camera.position.x += (target.x - fx * 6 - this.camera.position.x) * k;
    this.camera.position.y += (target.y + 2.4 - this.camera.position.y) * k;
    this.camera.position.z += (target.z - fz * 6 - this.camera.position.z) * k;
    this.camera.lookAt(target.x + fx * 18, target.y + 1.2 + gradient * 6, target.z + fz * 18);
    this.camera.fov = 65 + Math.min(23, speed / 7);
    this.camera.updateProjectionMatrix();
  }
}
```

  `game.ts` 调用处补第五参 `s.riders[0].speed`（用插值前的当前状态速度即可）。

- [ ] **Step 2：新建 `src/render/speedLines.ts`**：

```ts
import * as THREE from 'three';

const COUNT = 48;

export class SpeedLines {
  private lines: THREE.LineSegments;
  private pos: Float32Array;
  private seeds: { x: number; y: number; z: number }[] = [];

  constructor(scene: THREE.Scene) {
    this.pos = new Float32Array(COUNT * 6);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3));
    const mat = new THREE.LineBasicMaterial({
      color: 0xffffff,
      transparent: true,
      opacity: 0,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.lines = new THREE.LineSegments(geo, mat);
    this.lines.frustumCulled = false;
    scene.add(this.lines);
    for (let i = 0; i < COUNT; i++) {
      this.seeds.push({ x: 0, y: 0, z: 0 });
      this.respawn(i, true);
    }
  }

  private respawn(i: number, anywhere: boolean): void {
    const r = 14 + Math.random() * 26;
    const a = Math.random() * Math.PI * 2;
    this.seeds[i] = {
      x: Math.cos(a) * r,
      y: Math.abs(Math.sin(a)) * r * 0.55 + 1,
      z: anywhere ? (Math.random() - 0.5) * 220 : 90 + Math.random() * 60,
    };
  }

  update(speed: number, frameDt: number, camera: THREE.Camera): void {
    const mat = this.lines.material as THREE.LineBasicMaterial;
    mat.opacity = Math.max(0, Math.min(0.5, (speed - 30) / 120));
    if (mat.opacity <= 0.01) return;
    const len = Math.min(34, speed * 0.16);
    this.lines.position.copy(camera.position);
    this.lines.quaternion.copy(camera.quaternion);
    for (let i = 0; i < COUNT; i++) {
      const s = this.seeds[i];
      s.z -= speed * frameDt;
      if (s.z < -70) this.respawn(i, false);
      const o = i * 6;
      this.pos[o] = s.x;
      this.pos[o + 1] = s.y;
      this.pos[o + 2] = s.z;
      this.pos[o + 3] = s.x;
      this.pos[o + 4] = s.y;
      this.pos[o + 5] = s.z + len;
    }
    (this.lines.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;
  }
}
```

- [ ] **Step 3：`src/render/riders.ts`**——`buildRiderMesh(jersey: number, isPlayer: boolean)`，核心增量代码：

```ts
export function buildRiderMesh(jersey: number, isPlayer: boolean): THREE.Group {
  const group = new THREE.Group();
  const frameMat = new THREE.MeshLambertMaterial({ color: isPlayer ? 0xff2a2a : 0x22262e });
  const bodyMat = new THREE.MeshLambertMaterial({ color: jersey, flatShading: true });
  const wheelGeo = new THREE.CylinderGeometry(0.34, 0.34, 0.08, 12);
  const wheelMat = new THREE.MeshLambertMaterial({ color: 0x111111 });
  for (const z of [0.52, -0.52]) {
    const wheel = new THREE.Mesh(wheelGeo, wheelMat);
    wheel.rotation.z = Math.PI / 2;
    wheel.position.set(0, 0.34, z);
    group.add(wheel);
    for (let s = 0; s < 8; s++) {
      const spoke = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 0.66, 4), frameMat);
      spoke.rotation.set(0, (s / 8) * Math.PI, Math.PI / 2);
      spoke.position.set(0, 0.34, z);
      group.add(spoke);
    }
    if (isPlayer) {
      const shell = new THREE.Mesh(
        new THREE.CylinderGeometry(0.4, 0.4, 0.14, 12),
        new THREE.MeshBasicMaterial({ color: 0xff2a2a, side: THREE.BackSide }),
      );
      shell.rotation.z = Math.PI / 2;
      shell.position.set(0, 0.34, z);
      group.add(shell);
    }
  }
  const tube = (len: number, r: number, x: number, y: number, z: number, rx: number, rz = 0) => {
    const mesh = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len, 6), frameMat);
    mesh.rotation.set(rx, 0, rz);
    mesh.position.set(x, y, z);
    group.add(mesh);
  };
  tube(1.04, 0.04, 0, 0.62, 0, Math.PI / 2);
  tube(0.7, 0.035, 0, 0.52, 0.45, 0.5);
  tube(0.7, 0.035, 0, 0.52, -0.45, -0.5);
  tube(0.5, 0.03, 0, 0.85, 0.18, 0.35);
  tube(0.42, 0.03, 0, 1.06, 0.3, 1.1);
  tube(0.36, 0.025, 0.26, 1.12, 0.3, 0, Math.PI / 2);
  tube(0.36, 0.025, -0.26, 1.12, 0.3, 0, Math.PI / 2);
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.5, 0.56), bodyMat);
  torso.position.set(0, 1.12, -0.02);
  torso.rotation.x = 0.95;
  group.add(torso);
  if (isPlayer) {
    const outline = new THREE.Mesh(
      new THREE.BoxGeometry(0.4, 0.55, 0.61),
      new THREE.MeshBasicMaterial({ color: 0xff2a2a, side: THREE.BackSide }),
    );
    outline.position.copy(torso.position);
    outline.rotation.copy(torso.rotation);
    group.add(outline);
  }
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.14, 10, 10), new THREE.MeshLambertMaterial({ color: 0xf2c89a }));
  head.position.set(0, 1.32, 0.22);
  group.add(head);
  for (const [x, z, rx] of [[0.18, 0.35, -1.05], [-0.18, 0.35, -1.05], [0.18, -0.2, 0.95], [-0.18, -0.2, 0.95]] as const) {
    const limb = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.75, 6), bodyMat);
    limb.position.set(x, 0.78, z);
    limb.rotation.x = rx;
    group.add(limb);
  }
  if (isPlayer) group.scale.setScalar(1.15);
  return group;
}
```

  （新增：8 轮辐、弯把两段 tube、下管/前叉差异化半径、前倾 torso/head、玩家红框+双轮/躯干描边壳+1.15 缩放；`game.ts` 构造循环改 `buildRiderMesh(JERSEYS[i], i === 0)`。视觉微调自由，不改变接口。）
- [ ] **Step 4：`src/game.ts` 接线**——私有字段 `private speedLines!: SpeedLines;`，构造函数 `this.speedLines = new SpeedLines(this.ctx.scene);`，render 内 `this.speedLines.update(c.speed, frameDt, this.ctx.camera);`
- [ ] **Step 5：验证** — `npm run typecheck && npm run build && npm test` 全绿
- [ ] **Step 6：提交** — `git add src/render/camera.ts src/render/riders.ts src/render/speedLines.ts src/game.ts` 然后 `git commit -m "✨ feat(render): speed fov, speed lines, detailed bike and player highlight"`

### Task 8：条带地形 + 远雾

**Files:**
- Rewrite: `src/render/terrain.ts`
- Modify: `src/render/scene.ts`
- Test: `src/render/terrain.test.ts`（重写）

- [ ] **Step 1：重写 `src/render/terrain.test.ts`**：

```ts
import { describe, expect, it } from 'vitest';
import { buildTerrain, baseHills, smoothstep } from './terrain';
import { buildTrack } from '../sim/trackData';
import type { Track } from '../sim/track';

describe('ribbon terrain', () => {
  const track: Track = buildTrack();
  const mesh = buildTerrain(track);
  it('keeps baseHills and smoothstep exports', () => {
    expect(typeof baseHills).toBe('function');
    expect(smoothstep(0.5, 0, 1)).toBeCloseTo(0.5, 6);
  });
  it('builds a ribbon grid mesh', () => {
    const geo = mesh.geometry;
    const count = geo.attributes.position.count;
    expect(count).toBeGreaterThan(2000);
    expect(count).toBeLessThan(120000);
  });
  it('center vertices follow track elevation', () => {
    const geo = mesh.geometry.attributes.position;
    const pos = geo.array as Float32Array;
    for (let i = 0; i < pos.length; i += 3 * 13) {
      const d = track.nearest(pos[i], pos[i + 2]).dist;
      expect(d).toBeLessThan(400);
    }
  });
});
```

- [ ] **Step 2：跑测试确认失败**——现全局地块中心顶点距赛道可达数百米
- [ ] **Step 3：重写 `src/render/terrain.ts`**（保留 baseHills/smoothstep 导出与签名）：

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

const LATERAL = [-400, -250, -150, -80, -40, -15, 0, 15, 40, 80, 150, 250, 400];

export function buildTerrain(track: Track): THREE.Mesh {
  const pts = track.densePoints();
  const sections: [number, number, number, number, number][] = [];
  for (let i = 0; i < pts.length - 1; i += 3) {
    const [x, y, z] = pts[i];
    const [nx, nz] = [pts[i + 1][0] - x, pts[i + 1][2] - z];
    const len = Math.hypot(nx, nz) || 1;
    sections.push([x, y, z, -nz / len, nx / len]);
  }
  const rows = sections.length;
  const cols = LATERAL.length;
  const pos = new Float32Array(rows * cols * 3);
  for (let r = 0; r < rows; r++) {
    const [x, y, z, lx, lz] = sections[r];
    for (let c = 0; c < cols; c++) {
      const off = LATERAL[c];
      const px = x + lx * off;
      const pz = z + lz * off;
      const o = (r * cols + c) * 3;
      pos[o] = px;
      pos[o + 1] = y + baseHills(px, pz) * smoothstep(off, 12, 80) - 0.15;
      pos[o + 2] = pz;
    }
  }
  const idx: number[] = [];
  for (let r = 0; r < rows - 1; r++) {
    for (let c = 0; c < cols - 1; c++) {
      const a = r * cols + c;
      idx.push(a, a + cols, a + 1, a + 1, a + cols, a + cols + 1);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  geo.setIndex(idx);
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x6fae57 }));
}
```

  （`terrainHeight`/`nearest` 老接口删除前先全局 grep 确认无引用；`track.nearest` 仍保留在 Track 类。）

- [ ] **Step 4：`src/render/scene.ts`**——场景加雾与天空同色背景、相机 far：

```ts
  scene.fog = new THREE.Fog(0x9fc7e8, 1500, 2800);
  scene.background = new THREE.Color(0x9fc7e8);
  camera.far = 3000;
```

  （按 scene.ts 实际结构调整放置位置；若已有背景/雾则替换。）
- [ ] **Step 5：验证** — `npm test` 全绿、typecheck/build 成功
- [ ] **Step 6：提交** — `git add src/render/terrain.ts src/render/terrain.test.ts src/render/scene.ts` 然后 `git commit -m "✨ feat(render): track-following ribbon terrain with distance fog"`

### Task 9：俯视轮廓小地图

**Files:**
- Create: `src/render/minimap.ts`、`src/render/minimap.test.ts`
- Modify: `src/game.ts`、`index.html`

- [ ] **Step 1：新建 `src/render/minimap.test.ts`**：

```ts
import { describe, expect, it } from 'vitest';
import { fitTrack, project } from './minimap';
import { buildTrack } from '../sim/trackData';

describe('minimap projection', () => {
  const track = buildTrack();
  const fit = fitTrack(track.densePoints(), 200, 140, 8);
  it('fits all track points inside the canvas with padding', () => {
    for (const [x, , z] of track.densePoints()) {
      const [px, py] = project(x, z, fit);
      expect(px).toBeGreaterThanOrEqual(8);
      expect(px).toBeLessThanOrEqual(192);
      expect(py).toBeGreaterThanOrEqual(8);
      expect(py).toBeLessThanOrEqual(132);
    }
  });
  it('preserves relative order along the route', () => {
    const pts = track.densePoints();
    const head = Math.hypot(project(pts[pts.length - 1][0], pts[pts.length - 1][2], fit)[0] - 8, 0);
    expect(head).toBeGreaterThanOrEqual(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — 模块不存在
- [ ] **Step 3：新建 `src/render/minimap.ts`**：

```ts
import type { Track } from '../sim/track';
import type { RiderState } from '../sim/types';
import { ITEM_BOXES } from '../sim/params';

export interface Fit {
  scale: number;
  cx: number;
  cz: number;
  w: number;
  h: number;
  pad: number;
}

type Pt = [number, number, number];

export function fitTrack(pts: readonly Pt[], w: number, h: number, pad: number): Fit {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const [x, , z] of pts) {
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
  }
  const scale = Math.min((w - pad * 2) / (maxX - minX || 1), (h - pad * 2) / (maxZ - minZ || 1));
  return { scale, cx: (minX + maxX) / 2, cz: (minZ + maxZ) / 2, w, h, pad };
}

export function project(x: number, z: number, f: Fit): [number, number] {
  return [f.w / 2 + (x - f.cx) * f.scale, f.h / 2 + (z - f.cz) * f.scale];
}

const COLORS = ['#ffd54a', '#e0533d', '#4d8fd6', '#8a5cd6', '#4dbd8a', '#d68a4d', '#d64d9e', '#5a6a7a'];

export class Minimap {
  private ctx: CanvasRenderingContext2D | null;
  private fit: Fit;
  private route: [number, number][] = [];
  private boxes: [number, number][] = [];

  constructor(private canvas: HTMLCanvasElement, track: Track) {
    this.ctx = canvas.getContext('2d');
    const pts = track.densePoints();
    this.fit = fitTrack(pts, canvas.width, canvas.height, 8);
    for (let i = 0; i < pts.length; i += 20) this.route.push(project(pts[i][0], pts[i][2], this.fit));
    const seen = new Set<number>();
    for (const b of ITEM_BOXES) {
      if (seen.has(b.d)) continue;
      seen.add(b.d);
      const s = track.sampleAt(b.d);
      this.boxes.push(project(s.x, s.z, this.fit));
    }
  }

  draw(riders: readonly RiderState[], track: Track): void {
    const ctx = this.ctx;
    if (!ctx) return;
    const { width: w, height: h } = this.canvas;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.75)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    this.route.forEach(([x, y], i) => (i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)));
    ctx.stroke();
    ctx.fillStyle = '#4dd2ff';
    for (const [x, y] of this.boxes) ctx.fillRect(x - 1, y - 1, 2, 2);
    for (const r of riders) {
      const s = track.sampleAt(r.dist);
      const [x, y] = project(s.x, s.z, this.fit);
      ctx.fillStyle = COLORS[r.id % COLORS.length];
      ctx.beginPath();
      ctx.arc(x, y, r.isPlayer ? 4.5 : 3, 0, Math.PI * 2);
      ctx.fill();
      if (r.isPlayer) {
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(x, y, 6.5, 0, Math.PI * 2);
        ctx.stroke();
      }
    }
  }
}
```

- [ ] **Step 4：验证** — `npm test` 全绿（新增 minimap 投影 2 用例）
- [ ] **Step 5：接线**——`index.html` `#hud` 内 `<canvas id="minimap" width="200" height="140"></canvas>`（style.css 定位右上角 position/背景半透明）；`game.ts` 构造 `private minimap = new Minimap(document.querySelector('#minimap')!, this.track);`，render 末尾 `this.minimap.draw(s.riders, this.track);`
- [ ] **Step 6：验证** — `npm run typecheck && npm run build && npm test` 全绿
- [ ] **Step 7：提交** — `git add src/render/minimap.ts src/render/minimap.test.ts src/game.ts index.html src/style.css` 然后 `git commit -m "✨ feat(render): top-down minimap with rider dots and item box markers"`

### Task 10：HUD 踏频仪表盘与文案

**Files:**
- Modify: `src/ui/hud.ts`、`src/game.ts`、`index.html`

> 渲染层无单测，门禁：typecheck + build + test 全绿。

- [ ] **Step 1：`index.html`**——`#top-left` 内速度行下新增 `<canvas id="cad-gauge" width="150" height="88"></canvas>`；帮助行改为：

```html
    <p>W/S 或 ↑/↓ 踏频目标(无限制·按住连发) · Q/E 齿比(1T起·无上限·按住连发) · 1-4 功率档 · A/D 或 ←/→ 左右占位 · M 音乐开关</p>
```

- [ ] **Step 2：`src/ui/hud.ts`**——`HudView` 增 `cadence: number; cadTarget: number; finalSprint: boolean;`；构造器 id 列表加 `'cad-gauge'`；`update` 内：
  1. 速度行 `toFixed(1)` → `toFixed(0)`；剩余 `toFixed(2)` → `toFixed(0)`，`finalSprint` 时剩余行文本改 `最后 ${...} km`（红色 class）
  2. 新增 `this.drawGauge(v.cadence, v.cadTarget);`
  3. 私有方法：

```ts
  private drawGauge(cad: number, target: number): void {
    const canvas = this.el.get('cad-gauge') as HTMLCanvasElement;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const w = canvas.width, h = canvas.height;
    const cx = w / 2, cy = h - 10, r = Math.min(w / 2 - 8, h - 16);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.lineWidth = 7;
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.PI, 0);
    ctx.stroke();
    const MAX = 240;
    const ang = (v: number) => Math.PI + (Math.min(v, MAX) / MAX) * Math.PI;
    ctx.strokeStyle = '#4dd2ff';
    ctx.lineWidth = 3;
    for (let t = 0; t <= MAX; t += 60) {
      const a = ang(t);
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(a) * (r - 6), cy + Math.sin(a) * (r - 6));
      ctx.lineTo(cx + Math.cos(a) * (r + 4), cy + Math.sin(a) * (r + 4));
      ctx.stroke();
    }
    const ta = ang(target);
    ctx.strokeStyle = '#ff5a5a';
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(ta) * (r - 8), cy + Math.sin(ta) * (r - 8));
    ctx.lineTo(cx + Math.cos(ta) * (r + 6), cy + Math.sin(ta) * (r + 6));
    ctx.stroke();
    const ca = ang(cad);
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(ca) * (r - 10), cy + Math.sin(ca) * (r - 10));
    ctx.stroke();
    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 13px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(`${Math.round(cad)} / ${Math.round(target)}`, cx, cy - 4);
  }
```

- [ ] **Step 3：`src/game.ts` `view()`**——增字段：`cadence: cadence(p.speed, p.cog), cadTarget: p.cadTarget, finalSprint: s.phase === 'racing' && remaining <= 5000`（`remaining` 变量已在 render 中，提到 view 或重算）；gearLine 简化为 `52×${Math.round(p.cog)} · ${GEARS[p.gear]}`
- [ ] **Step 4：验证** — `npm run typecheck && npm run build && npm test` 全绿
- [ ] **Step 5：提交** — `git add src/ui/hud.ts src/game.ts index.html` 然后 `git commit -m "✨ feat(ui): cadence gauge dial and hyperspeed hud copy"`

### Task 11：全量回归与手动验收

- [ ] **Step 1：全量门禁** — `npm test` 全绿（预期 86 + trackData 3 重写 + track 1 + race 净变化（−5 解锁删 −2 补给删 +4 道具）+ minimap 2 ≈ 89±2，以实际计数记录）、typecheck、build
- [ ] **Step 2：回归数值记录**——全场完赛时间（预估 1250-1500s）、玩家 cruise vs 最快 AI（AI ftp×0.9 后玩家应不落后 60s+）、reckless（全程 gear3）<1500s、下坡峰值速度（遥测或测试脚本取全程 max(speed)，应 >180 m/s）
- [ ] **Step 3：手动验收清单**（`npm run dev`，需人工）
  1. 静止起步 15s 内到 300km/h；平路巡航 HUD ~370-400km/h；FOV 随速度变宽、速度线出现
  2. 爬坡段明显掉速（巡航档 100km/h 以下）、降档或发力恢复；长下坡 600km/h+
  3. Q/E 齿比 1T~∞ 无钳位（HUD 52×1 与 52×400 均可达）；W/S 踏频 0~∞ 无钳位；仪表盘指针与目标红标实时联动
  4. 道具箱：驶过横向对齐即拾取（闪白+浮字+音效），玩家拾过的箱消失，AI 也会拾取；小地图蓝点对应簇位置
  5. 小地图：右上角全路线轮廓+8 骑手点，玩家白色大点清晰
  6. 玩家车放大亮红+描边，模型细节（轮辐/弯把）可见；远雾平滑遮蔽条带地形边缘
  7. 体力：全程冲刺不掉空（几乎无压力），巡航缓慢回满
- [ ] **Step 4：最终整体审查**（子代理 code-reviewer，范围 = 本迭代全部提交，对照 spec §11/§12）

---

## Part C 覆盖与风险

### C1 规格覆盖映射

| 规格条目（hyperspeed-stage spec） | 任务 |
|---|---|
| §2 物理（gearRatios×6/CdA/牵引上限/坡度放大） | Task 2（参数）、3（坡度） |
| §3 操控全解锁 + 效率删除 | Task 1 |
| §4 150km 点对点赛道 | Task 3 |
| §5 道具箱（判定/一次性/确定性） | Task 5（sim）、6（渲染反馈） |
| §6 体力经济（drain/regen） | Task 2 |
| §7 AI 适配 | Task 4 |
| §8 相机/FOV/速度线/玩家车/模型/条带地形/雾 | Task 7、8 |
| §9 小地图 | Task 9 |
| §10 HUD 仪表盘/文案/最后5km | Task 6（浮字）、10 |
| §11/§12 测试与验收 | 各任务 + Task 11 |

### C2 风险与对策

| 风险 | 对策 |
|---|---|
| 赛道平面自交/掉头弯过锐 | Task 3 容差内微调（正弦幅度/turn 窗口），以测试+可视化为准 |
| 全程仿真测试变慢（~83000 步） | 单次 ~1-2s 可接受；断言复用同一次 run 结果 |
| Task 2-4 中间态（4.4km 环道+超高速）测试数值偏移 | avgPower 区间在 Task 2 重标定；时长契约 Task 3 收紧 |
| 条带地形边缘穿帮 | 雾 1500m 遮蔽 + LATERAL 400m 宽度；手动验收确认 |
| 道具箱拾取在高速下漏判（一 tick 跨 3.5m） | 判定用跨越式（prevDist < d ≤ dist）不依赖窗口宽度；速度 2 m/s 慢速用例钉住单次语义 |
| HUD 仪表盘目标超 240rpm | 钉在端点（ang() 内 clamp）+ 数字行显示精确值 |






