# 公路车迭代实现计划：齿比/踏频连续化 + 冲刺踏频无上限

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `docs/superpowers/specs/2026-09-27-cycling-continuous-drivetrain-sprint-design.md`：飞轮齿数连续 [10,36]T 任意切换（单击 ±1T + 按住 6T/s 连发）、踏频目标 [80,150]（单击 ±5 + 按住 30rpm/s 连发）、功率档 4 冲刺期间踏频无上限（目标钳位解除 + 效率恒 1）。

**Architecture:** 语义原子切换——`cog` 从飞轮索引改为齿数浮点，Task 1 一次贯通 params/drivetrain/race/input/game（拆开会产生物理错误的中间态、测试不绿）；Task 2 输入层引入"脉冲 + 持续积分"模型（注入时钟保证可测）；Task 3 HUD/帮助文案。AI 换挡改解析解，行为近似等价。

**Tech Stack:** 不变（TypeScript strict + Vite + Three.js + Vitest）。

---

## Part A 关键决策与预演数值

### A1 决策落点

| 项 | 决策 |
|---|---|
| cog 语义 | 齿数浮点 [10,36]，默认 16T；`gearRatio(cog)=52/cog`；Q=+T（变轻）E=−T（变重）——E 仍为变重，方向语义与索引版一致 |
| cadTarget 钳位 | `clamp(cadTarget + cadDelta, 80, gear===3 ? Infinity : max(150, cadTarget))`：冲刺解锁上限；切出后 >150 保留，上调不回落、下调平滑递减（spec 修正版公式） |
| 冲刺效率 | `cadenceEfficiency(cad, sprint)`：sprint=true 恒返 1；仅玩家 gear 3 触发（AI 的 gear 3 不触发） |
| 输入模型 | 脉冲（keydown !repeat：Q/E ±1T、W/S ±5rpm）+ 持续积分（按住：Q/E ±6T/s、W/S ±30rpm/s，单次 dt 上限 100ms）；`InputController` 构造注入 `now()` 时钟（默认 performance.now），测试可控 |
| AI | 恒 95rpm；`aiShift` 解析解 `ideal = clamp(target×52×2.096/(speed×60), 10, 36)`，speed<0.5 m/s 保持当前齿，滞回 3rpm 防抖 |
| 保留不动 | `terrainCadence` 及其参数、`cadOffsetMax`（死参数，外科手术原则不删）；效率曲线常量全部不变 |

### A2 预演数值（测试期望依据）

- `gearRatio(36)=1.4444`、`gearRatio(16)=3.25`、`gearRatio(10)=5.2`
- `cadence(10.95,16)=96.45`、`cadence(5.4,28)=83.23`、`cadence(20,10)=110.10`、`cadence(20,16)=176.16`
- `aiShift`：`(10.95,16)→16`（err 1.45<3 滞回保持）；`(5.4,16)→31.9575`；`(20,16)→10`（ideal 8.63 clamp 10）；`(0,16)→16`；`(0.3,16)→16`；`(10.95,16,110)→18.2483`；`(5.4,16,90)→30.2755`
- 冲刺解锁链：110 + 5×20 = 210（无钳）；切出档 4 一帧 cadDelta 0 → 210 保留；再 −5 → 205（平滑递减）
- 冲刺效率：speed 20 / cog 16 → cad 176.2：gear 3 power = 750（eff 恒 1）；gear 1 power = 300×0.55 = 165
- reckless（恒 gear 3、cadTarget 110）：soft-pedal 上限 12.5 m/s 不变，仅 <80rpm 段效率提升 → 完赛时间与现行接近，<500s 契约保持

### A3 文件清单

```
修改  src/sim/params.ts         删 cassette/defaultCog；增 cogMin/cogMax/cogStep/cogHoldRate/cadHoldRate/defaultCogTeeth/aiShiftMinSpeed；cadTargetMin 80
重写  src/sim/drivetrain.ts     齿数语义 + sprint 效率 + 解析 aiShift（terrainCadence 原样保留）
修改  src/sim/race.ts           齿数钳位、cadTarget 新钳位、玩家 sprint 标志、AI 解析换挡、defaultCogTeeth
重写  src/input.ts              脉冲 + 积分模型 + 时钟注入（Task 2）
修改  src/game.ts               Task 1 去 cassette 引用；Task 3 目标踏频取整
修改  index.html                帮助行更新（Task 3）
测试  drivetrain.test / race.test / input.test 相应 describe 重写；draft.test / ai.test 工厂 cog 6→16
```

---

## Part B 实施任务（TDD，按序执行）

### Task 0：基线

- [ ] `npm test` 全绿（记录用例总数，Task 4 对比用）；`npm run typecheck` 0 错误——不绿先停下排查

### Task 1：sim 齿数语义原子切换（params / drivetrain / race / input 方向 / game 编译修复）

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/race.ts`、`src/input.ts`（仅 Q/E 方向翻转）、`src/game.ts`、`src/sim/draft.test.ts`、`src/sim/ai.test.ts`
- Rewrite: `src/sim/drivetrain.ts`、`src/sim/drivetrain.test.ts`
- Test: `src/sim/race.test.ts`（2 处改写 + 2 新增）、`src/input.test.ts`（2 处断言翻转）

- [ ] **Step 1：重写 `src/sim/drivetrain.test.ts`**

```ts
import { describe, expect, it } from 'vitest';
import { aiShift, cadence, cadenceEfficiency, gearRatio, terrainCadence } from './drivetrain';

describe('gearRatio', () => {
  it('maps cog teeth to 52T ratios', () => {
    expect(gearRatio(36)).toBeCloseTo(52 / 36, 6);
    expect(gearRatio(16)).toBeCloseTo(3.25, 6);
    expect(gearRatio(10)).toBeCloseTo(5.2, 6);
  });
});

describe('cadence', () => {
  it('cruise 10.95 m/s on 52x16 is ~96 rpm', () => {
    expect(cadence(10.95, 16)).toBeCloseTo(96.4, 1);
  });
  it('climb 5.4 m/s on 52x28 is ~83 rpm', () => {
    expect(cadence(5.4, 28)).toBeCloseTo(83.2, 1);
  });
  it('top gear at 20 m/s is ~110 rpm', () => {
    expect(cadence(20, 10)).toBeCloseTo(110.1, 1);
  });
  it('zero speed is zero cadence', () => {
    expect(cadence(0, 16)).toBe(0);
  });
});

describe('cadenceEfficiency', () => {
  it('full power inside [80,125] rpm', () => {
    expect(cadenceEfficiency(80)).toBe(1);
    expect(cadenceEfficiency(100)).toBe(1);
    expect(cadenceEfficiency(120)).toBe(1);
    expect(cadenceEfficiency(125)).toBe(1);
  });
  it('linear falloff to 0.55 between 50-80 and 125-150', () => {
    expect(cadenceEfficiency(65)).toBeCloseTo(0.775, 6);
    expect(cadenceEfficiency(70)).toBeCloseTo(0.85, 6);
    expect(cadenceEfficiency(140)).toBeCloseTo(0.73, 6);
  });
  it('clamps at 0.55 beyond the range', () => {
    expect(cadenceEfficiency(50)).toBe(0.55);
    expect(cadenceEfficiency(30)).toBe(0.55);
    expect(cadenceEfficiency(150)).toBe(0.55);
    expect(cadenceEfficiency(170)).toBe(0.55);
  });
  it('sprint flag gives full efficiency at any cadence', () => {
    expect(cadenceEfficiency(0, true)).toBe(1);
    expect(cadenceEfficiency(40, true)).toBe(1);
    expect(cadenceEfficiency(150, true)).toBe(1);
    expect(cadenceEfficiency(200, true)).toBe(1);
  });
});

describe('terrainCadence', () => {
  it('climb above +2%, descent below -2%, flat otherwise', () => {
    expect(terrainCadence(0.025)).toBe(90);
    expect(terrainCadence(0.02)).toBe(110);
    expect(terrainCadence(0)).toBe(110);
    expect(terrainCadence(-0.02)).toBe(110);
    expect(terrainCadence(-0.025)).toBe(120);
  });
});

describe('aiShift', () => {
  it('keeps cog near 95 rpm at cruise via hysteresis', () => {
    expect(aiShift(10.95, 16)).toBe(16);
  });
  it('shifts analytically to ~32T on climbs', () => {
    expect(aiShift(5.4, 16)).toBeCloseTo(31.96, 1);
  });
  it('clamps to 10T at speed', () => {
    expect(aiShift(20, 16)).toBe(10);
  });
  it('keeps current cog at standstill', () => {
    expect(aiShift(0, 16)).toBe(16);
  });
  it('keeps current cog below min speed threshold', () => {
    expect(aiShift(0.3, 16)).toBe(16);
  });
  it('honors custom target 110 on flat', () => {
    expect(aiShift(10.95, 16, 110)).toBeCloseTo(18.25, 1);
  });
  it('honors custom target 90 on climb', () => {
    expect(aiShift(5.4, 16, 90)).toBeCloseTo(30.28, 1);
  });
});
```

- [ ] **Step 2：改写 `src/sim/race.test.ts` 两用例并追加两用例**

  1. 用例 `manual cog shifts via cogDelta and clamps to cassette range`（83-93 行）整体替换为：

```ts
  it('cog shifts in continuous teeth via cogDelta and clamps to 10..36', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const half: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0.5 };
    s = stepRace(s, track, half, RACE.dt);
    expect(s.riders[0].cog).toBeCloseTo(16.5, 6);
    const heavy: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: -100 };
    s = stepRace(s, track, heavy, RACE.dt);
    expect(s.riders[0].cog).toBe(10);
    const light: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 100 };
    s = stepRace(s, track, light, RACE.dt);
    expect(s.riders[0].cog).toBe(36);
  });
```

  2. 用例 `cadence target adjusts via cadDelta and clamps to 60..150`（105-115 行）替换为（末尾断言 80）：

```ts
  it('cadence target adjusts via cadDelta and clamps to 80..150', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const up: RiderCommand = { gear: 1, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, up, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(150);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    for (let i = 0; i < 40; i++) s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(80);
  });
```

  3. describe 末尾追加：

```ts
  it('sprint gear unlocks cadence target above 150 and keeps it after exit', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const sprintUp: RiderCommand = { gear: 3, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, sprintUp, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(205);
  });
  it('sprint gear keeps full efficiency at extreme cadence', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 200, speed: 20, cadTarget: 250 })) };
    s = stepRace(s, track, { gear: 3, steer: 0, cadDelta: 0, cogDelta: 0 }, RACE.dt);
    expect(s.riders[0].power).toBeCloseTo(750, 5);
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].power).toBeCloseTo(165, 5);
  });
```

- [ ] **Step 3：翻转 `src/input.test.ts` Q/E 断言**——两用例替换为：

```ts
  it('E queues -1 cog delta once', () => {
    press('e');
    expect(c.command().cogDelta).toBe(-1);
    expect(c.command().cogDelta).toBe(0);
  });
  it('Q queues +1 cog delta', () => {
    press('q');
    expect(c.command().cogDelta).toBe(1);
  });
```

- [ ] **Step 4：跑测试确认失败** — `npm test`，预期：drivetrain 齿数用例 NaN/数值失败、sprint 效率失败、race 四用例失败、input 两用例失败；terrainCadence 与效率旧边界仍绿

- [ ] **Step 5：修改 `src/sim/params.ts`**——`DRIVETRAIN` 整体替换为（删 `cassette`/`defaultCog`，增连续齿数参数，`cadTargetMin 60→80`；terrain/cadOffsetMax 原样保留）：

```ts
export const DRIVETRAIN = {
  chainring: 52,
  cogMin: 10,
  cogMax: 36,
  cogStep: 1,
  cogHoldRate: 6,
  cadHoldRate: 30,
  defaultCogTeeth: 16,
  wheelCirc: 2.096,
  cadFullLo: 80,
  cadFullHi: 125,
  cadFloor: 50,
  cadCeil: 150,
  effMin: 0.55,
  aiTargetCadence: 95,
  aiShiftHysteresis: 3,
  aiShiftMinSpeed: 0.5,
  climbCadence: 90,
  flatCadence: 110,
  descentCadence: 120,
  climbGradient: 0.02,
  descentGradient: -0.02,
  cadenceStep: 5,
  cadOffsetMax: 30,
  cadTargetMin: 80,
  cadTargetMax: 150,
} as const;
```

- [ ] **Step 6：重写 `src/sim/drivetrain.ts`**

```ts
import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / cog;
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function cadenceEfficiency(cad: number, sprint = false): number {
  if (sprint) return 1;
  if (cad >= D.cadFullLo && cad <= D.cadFullHi) return 1;
  if (cad <= D.cadFloor || cad >= D.cadCeil) return D.effMin;
  if (cad < D.cadFullLo) {
    return D.effMin + ((cad - D.cadFloor) / (D.cadFullLo - D.cadFloor)) * (1 - D.effMin);
  }
  return D.effMin + ((D.cadCeil - cad) / (D.cadCeil - D.cadFullHi)) * (1 - D.effMin);
}

export function terrainCadence(gradient: number): number {
  if (gradient > D.climbGradient) return D.climbCadence;
  if (gradient < D.descentGradient) return D.descentCadence;
  return D.flatCadence;
}

export function aiShift(speed: number, currentCog: number, target: number = D.aiTargetCadence): number {
  if (speed < D.aiShiftMinSpeed) return currentCog;
  const ideal = Math.min(D.cogMax, Math.max(D.cogMin, (target * D.chainring * D.wheelCirc) / (speed * 60)));
  const err = (cog: number) => Math.abs(cadence(speed, cog) - target);
  return err(ideal) < err(currentCog) - D.aiShiftHysteresis ? ideal : currentCog;
}
```

- [ ] **Step 7：修改 `src/sim/race.ts`**——三处：
  1. `makeRider`（38 行）：`cog: DRIVETRAIN.defaultCog` → `cog: DRIVETRAIN.defaultCogTeeth`
  2. 玩家块（100-105 行）替换为：

```ts
    if (r.isPlayer) {
      r.cog = clamp(r.cog + cmd.cogDelta, DRIVETRAIN.cogMin, DRIVETRAIN.cogMax);
      const cadHi = cmd.gear === 3 ? Infinity : Math.max(DRIVETRAIN.cadTargetMax, r.cadTarget);
      r.cadTarget = clamp(r.cadTarget + cmd.cadDelta, DRIVETRAIN.cadTargetMin, cadHi);
    } else if (shiftTick) {
      r.cog = aiShift(r.speed, r.cog);
    }
```

  3. 功率块（106-109 行）替换为：

```ts
    const cad = cadence(r.speed, r.cog);
    const sprint = r.isPlayer && cmd.gear === 3;
    const softPedal = r.isPlayer && cad >= r.cadTarget;
    const effort = r.finishTime !== null || softPedal ? 0 : targetPower(cmd.gear, r.type.ftp, r.energy);
    r.power = effort * cadenceEfficiency(cad, sprint);
```

- [ ] **Step 8：修改 `src/input.ts`**——Q/E 两行翻转（36-37 行）：

```ts
    else if (k === 'e') this.cogDelta -= DRIVETRAIN.cogStep;
    else if (k === 'q') this.cogDelta += DRIVETRAIN.cogStep;
```

- [ ] **Step 9：修改 `src/game.ts`**——import 行（6 行）`{ DRIVETRAIN, GEARS, RACE }` → `{ GEARS, RACE }`；gearLine 内（93 行）替换为：

```ts
        return `52×${Math.round(p.cog)} · ${rpm}rpm · ${GEARS[p.gear]}`;
```

- [ ] **Step 10：测试工厂齿数化**——`src/sim/draft.test.ts`（8 行）与 `src/sim/ai.test.ts`（6 行）工厂 `cog: 6,` → `cog: 16,`

- [ ] **Step 11：跑测试与类型检查** — `npm test` 全绿、`npm run typecheck` 0 错误；记录：全场完赛区间（AI 解析换挡后各车手 finishTime）、reckless 完赛时间（<500s 契约）。reckless 或 avgPower 越界 → **不要调参**，报告 BLOCKED 与实测数据

- [ ] **Step 12：提交** — `git add src/sim/params.ts src/sim/drivetrain.ts src/sim/drivetrain.test.ts src/sim/race.ts src/sim/race.test.ts src/input.ts src/input.test.ts src/game.ts src/sim/draft.test.ts src/sim/ai.test.ts` 然后 `git commit -m "✨ feat(sim): continuous cog teeth, sprint cadence unlock and analytical ai shifting"`（正文附实测数值）

### Task 2：输入连发模型（脉冲 + 持续积分）

**Files:**
- Rewrite: `src/input.ts`、`src/input.test.ts`

- [ ] **Step 1：重写 `src/input.test.ts`**

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
  let t = 0;
  beforeEach(() => {
    t = 0;
    c = new InputController(() => t);
    c.attach();
  });

  it('W queues +5 cadence delta once', () => {
    press('ArrowUp');
    expect(c.command().cadDelta).toBe(5);
    expect(c.command().cadDelta).toBe(0);
  });
  it('presses accumulate between ticks', () => {
    press('ArrowUp');
    press('W');
    expect(c.command().cadDelta).toBe(10);
  });
  it('S queues -5 cadence delta', () => {
    press('ArrowDown');
    expect(c.command().cadDelta).toBe(-5);
  });
  it('E queues -1 cog delta once', () => {
    press('e');
    expect(c.command().cogDelta).toBe(-1);
    expect(c.command().cogDelta).toBe(0);
  });
  it('Q queues +1 cog delta', () => {
    press('q');
    expect(c.command().cogDelta).toBe(1);
  });
  it('holding Q integrates 6T/s on top of the pulse', () => {
    press('q');
    t += 0.05;
    expect(c.command().cogDelta).toBeCloseTo(1 + 6 * 0.05, 5);
    t += 0.05;
    expect(c.command().cogDelta).toBeCloseTo(6 * 0.05, 5);
  });
  it('holding W integrates 30rpm/s', () => {
    press('w');
    t += 0.05;
    expect(c.command().cadDelta).toBeCloseTo(5 + 30 * 0.05, 5);
    release('w');
    t += 0.05;
    expect(c.command().cadDelta).toBe(0);
  });
  it('caps integration dt at 100ms', () => {
    press('q');
    t += 10;
    expect(c.command().cogDelta).toBeCloseTo(1 + 6 * 0.1, 5);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cadDelta).toBe(5);
  });
  it('number keys set power gear and leave deltas untouched', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cadDelta).toBe(0);
    expect(c.command().cogDelta).toBe(0);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：积分三用例（holding Q / holding W / caps dt）失败（现行无积分逻辑，脉冲值不匹配）；其余脉冲/steer 用例仍绿

- [ ] **Step 3：重写 `src/input.ts`**

```ts
import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cadPulse = 0;
  private cogPulse = 0;
  private steer = 0;
  private keys = new Set<string>();
  private lastNow: number;

  constructor(private now: () => number = () => performance.now()) {
    this.lastNow = now();
  }

  command(): RiderCommand {
    const held = Math.min(0.1, Math.max(0, (this.now() - this.lastNow) / 1000));
    this.lastNow = this.now();
    let cogDelta = this.cogPulse;
    let cadDelta = this.cadPulse;
    if (this.keys.has('q')) cogDelta += DRIVETRAIN.cogHoldRate * held;
    if (this.keys.has('e')) cogDelta -= DRIVETRAIN.cogHoldRate * held;
    if (this.keys.has('w') || this.keys.has('arrowup')) cadDelta += DRIVETRAIN.cadHoldRate * held;
    if (this.keys.has('s') || this.keys.has('arrowdown')) cadDelta -= DRIVETRAIN.cadHoldRate * held;
    this.cogPulse = 0;
    this.cadPulse = 0;
    return { gear: this.gear, steer: this.steer, cadDelta, cogDelta };
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.cadPulse = 0;
    this.cogPulse = 0;
    this.steer = 0;
    this.keys.clear();
    this.lastNow = this.now();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cadPulse += DRIVETRAIN.cadenceStep;
    else if (k === 'arrowdown' || k === 's') this.cadPulse -= DRIVETRAIN.cadenceStep;
    else if (k === 'q') this.cogPulse += DRIVETRAIN.cogStep;
    else if (k === 'e') this.cogPulse -= DRIVETRAIN.cogStep;
    else if (k === '1' || k === '2' || k === '3' || k === '4') this.gear = (Number(k) - 1) as GearId;
    this.keys.add(k);
    this.steer = this.computeSteer();
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

- [ ] **Step 4：跑测试确认通过** — `npm test` 全绿、`npm run typecheck` 0 错误
- [ ] **Step 5：提交** — `git add src/input.ts src/input.test.ts` 然后 `git commit -m "✨ feat(input): pulse plus hold-repeat integration for cog and cadence control"`

### Task 3：HUD 与帮助文案

**Files:**
- Modify: `src/game.ts`、`index.html`

> 渲染/文案层无单测（既定偏差），门禁：`npm run typecheck` + `npm run build` + `npm test` 保持全绿。

- [ ] **Step 1：`src/game.ts` gearLine 目标取整**——89-94 行替换为：

```ts
      gearLine: (() => {
        const cad = cadence(p.speed, p.cog);
        const target = Math.round(p.cadTarget);
        const rpm = Math.abs(cad - target) > 3 ? `${cad.toFixed(0)}/${target}` : cad.toFixed(0);
        return `52×${Math.round(p.cog)} · ${rpm}rpm · ${GEARS[p.gear]}`;
      })(),
```

- [ ] **Step 2：`index.html` 帮助行（30 行）替换为**：

```html
    <p>W/S 或 ↑/↓ 调踏频目标(80-150·冲刺档无上限·按住连发) · Q/E 换齿比(10-36T·按住连发) · 1-4 功率档 · A/D 或 ←/→ 左右占位 · M 音乐开关</p>
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build && npm test`（全绿）
- [ ] **Step 4：提交** — `git add src/game.ts index.html` 然后 `git commit -m "✨ feat(ui): integer teeth display and updated help line"`

### Task 4：全量验证与手动验收

- [ ] **Step 1：全量门禁** — `npm test`（用例数 = Task 0 基线 + 7：drivetrain +2、race +2、input +3）、`npm run typecheck`、`npm run build`
- [ ] **Step 2：回归数值记录**——与 main 基线对比写入交付说明：全场完赛各车手成绩（AI 解析换挡漂移量）、reckless 完赛时间（契约 <500s）、确定性用例绿
- [ ] **Step 3：手动验收清单**（`npm run dev`，需人工）
  1. Q/E 单击 HUD `52×16→52×17/52×15`；按住 ~1s 跨约 6T；R 重开归位 52×16 · 110rpm
  2. W/S 单击 ±5、按住连发；目标钳位 [80,150]；HUD 实际/目标联动
  3. 档 4 下 W 可推目标 >150（HUD 显示如 `96/180rpm`）；切回档 1 保留；S 平滑降回 150 以下
  4. 下坡高档 4 + 高目标：功率不再随高踏频跳水（对比档 1 同时速明显掉功率）
  5. AI 行为观感与现行一致（爬坡大飞轮、平路 ~16T、冲刺段加速）
- [ ] **Step 4：最终整体审查**（子代理 code-reviewer，范围 = 本迭代全部提交，对照 spec §7 影响面与 §8 验收标准）

---

## Part C 覆盖与风险

### C1 规格覆盖映射

| 规格条目（2026-09-27 continuous-drivetrain spec） | 任务 |
|---|---|
| 连续齿数 [10,36] + Q轻/E重 + 默认 16T | Task 1 |
| 齿比单击 ±1T / 连发 6T/s（dt 钳 100ms） | Task 1（脉冲）/ Task 2（积分） |
| cadTarget [80,150] 单击 ±5 / 连发 30rpm/s | Task 1（钳位与脉冲）/ Task 2（积分） |
| 冲刺 gear 3：目标上限 ∞ + 切出保留（max 公式） | Task 1 |
| 冲刺效率恒 1（玩家专属，AI gear 3 不触发） | Task 1 |
| AI 解析换挡 95rpm、近零保持、滞回 3rpm | Task 1 |
| HUD `52×整数` + 冲刺目标 >150 显示 + 帮助行 | Task 1（齿数）/ Task 3 |
| 回归红线（reckless <500s / 全场完赛 / 确定性） | Task 1 Step 11、Task 4 |

### C2 风险与对策

| 风险 | 对策 |
|---|---|
| AI 成绩漂移（解析解 vs 12 档遍历） | Task 1 Step 11 记录区间；avgPower 越界 (150,400) 或完赛异常 → BLOCKED 上报，不调参 |
| reckless 契约翻转 | 实测 <500s；soft-pedal 110rpm 上限未变，仅低速段效率提升，预估漂移 <5s；翻转上报 |
| 连发与 loop 固定步长节拍错位 | command() 以真实时间积分、60Hz 消费；dt 钳 100ms 防切页跳变（低帧率下调节变慢，可接受） |
| sprint 效率测试对地形敏感 | cadTarget 250 留足余量（单帧 20→~24 m/s 内 cadence 均低于 250） |
| terrainCadence/cadOffsetMax 死代码 | 外科手术原则保留不动，不影响行为 |




