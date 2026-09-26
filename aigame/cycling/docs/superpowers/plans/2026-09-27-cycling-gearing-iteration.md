# 公路车迭代实现计划：真实变速 / 补给系统 / 三档音乐

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `docs/superpowers/specs/2026-09-27-cycling-gearing-feed-music-design.md`：12 速真实变速（52T × 10-36T，踏频软惩罚）、双补给区体力恢复（玩家体力 +33%）、外部 mp3 三档联动音乐。

**Architecture:** 在现有确定性仿真核心上扩展——`sim/drivetrain.ts` 新模块承载齿比/踏频/效率/AI 变速（纯函数）；race 步进接入效率乘子、AI 每 0.5s 变速、补给区恢复；输入层 W/S 语义改为换档；HUD 与渲染层增量更新；audio.ts 重写为三文件交叉淡变。

**Tech Stack:** 不变（TypeScript strict + Vite + Three.js + Vitest）。新增素材目录 `public/music/`（用户提供 mp3，缺文件优雅降级）。

---

## Part A 设计要点与预演数值

### A1 与规格的差异/落地决策

| 项 | 规格 | 落地 | 原因 |
|---|---|---|---|
| 变速数据 | 12 速 10-36T | `CASSETTE = [36,32,28,24,21,18,16,14,13,12,11,10]`（索引 0=36T 最轻 → 11=10T 最重），W=cog+1 变重 | 轻重序与 +1 语义一致 |
| AI 变速 | 每 0.5s 选最接近 95rpm | `aiShift(speed, currentCog)` 带 3rpm 滞回（改进量 >3rpm 才换），`shiftTick` 由 `Math.floor(time/0.5)` 变化判定 | 防边界抖动；无随机，确定性保持 |
| 效率乘子接入点 | effectivePower = 目标 × eff | race 步进中 `r.power = targetPower(...) × cadenceEfficiency(cadence(r.speed, cmd.cog))`；energy/physics 均消费 `r.power` | energy.ts/physics.ts 零改动 |
| 补给区判定 | d ∈ 两区 | `RACE.feedZones: [[560,610],[2640,2690]]` + `RACE.feedRegenRate: 0.04`（每秒 4% × maxEnergy），race 内联判定 | 参数集中可调 |
| 音乐文件 | 用户提供 | `public/music/{calm,intense,sprint}.mp3` + `public/music/SOURCES.md` 说明；fetch 失败/404 → 该层静音 | 后补文件即生效 |
| 音乐档位推送 | 状态映射 | Game 持有 Music 引用，render 帧内 `setTier`（内部同档 no-op） | 无需事件系统 |

### A2 数值预演（测试期望值依据）

- `gearRatio(6) = 52/16 = 3.25`；`gearRatio(0) = 1.4444`；`gearRatio(11) = 5.2`
- `cadence(10.95, 6) = 10.95/(3.25×2.096)×60 ≈ 96.4 rpm`
- `cadence(5.4, 2) ≈ 83.2 rpm`（52×28 爬坡满效）；`cadence(5.4, 1) ≈ 95.1 rpm`（AI 爬坡选 32T）
- `cadence(20, 11) ≈ 110.1 rpm`（52×10 顶档 @72km/h 满效）
- 效率边界：eff(50)=0.775、eff(70)=1、eff(100)=1、eff(120)=0.91、eff(125)=0.82、eff(40)=0.55、eff(140)=0.55、eff(20)=0.55、eff(160)=0.55
- `aiShift(10.95, 6)=6`（保持）；`aiShift(5.4, 6)=1`；`aiShift(20, 6)=11`；`aiShift(0, 6)=6`（静止全平局→滞回保持）
- 补给：玩家 32000×4% = 1280 J/s，净回约 1250/s（扣除巡航消耗 30/s）
- reckless 回归预估：固定 cog 6 全程冲刺（750W×eff），32000J 池约 170s 耗尽 → 之后 135W×eff ≈ 6.8m/s → 总成绩 ≈527s vs 匀速 ≈444s， reckless 仍输 ✓（实现时以实测为准，若方向翻转报告数值而非盲目调参）

### A3 文件清单

```
修改  src/sim/params.ts        CASSETTE/CHAINRING/WHEEL_CIRC/踏频区间/feedZones/feedRegenRate
修改  src/sim/types.ts         RiderCommand.cog / RiderState.cog
新增  src/sim/drivetrain.ts    gearRatio/cadence/cadenceEfficiency/aiShift（+测试）
修改  src/input.ts             W/S 换档、1-4 功率档、command 含 cog（测试重写）
修改  src/sim/ai.ts            返回类型改为 {gear, steer}（cog 由 race 组合）
修改  src/sim/race.ts          cog 初始化/传递、eff 乘子、AI 变速、补给恢复、玩家 32000
修改  src/render/trackMesh.ts  提取 gate() 复用：终点红门 + 双蓝补给门
修改  src/ui/hud.ts            gearLine 档位行、高程图补给刻度
重写  src/audio.ts             三文件交叉淡变 + 静音 + 降级
修改  src/game.ts              Music 注入、tier 推送、gearLine/cadence、setProfile 带补给区
修改  src/main.ts              Music 构造传入 Game
新增  public/music/SOURCES.md  素材路径与授权说明
修改  src/input.test.ts / src/sim/race.test.ts；新增 src/sim/drivetrain.test.ts
```

---

## Part B 实施任务（TDD，按序执行）

### Task 0：分支与基线

- [ ] `git checkout -b feature/cycling-gearing-iteration`
- [ ] 基线验证：`npm test`（56/56）、`npm run typecheck`（0 错误）——不绿先停下排查

### Task 1：传动模块（types / params / drivetrain）

**Files:**
- Modify: `src/sim/types.ts`、`src/sim/params.ts`
- Create: `src/sim/drivetrain.ts`
- Test: `src/sim/drivetrain.test.ts`

- [ ] **Step 1：写失败测试 `src/sim/drivetrain.test.ts`**

```ts
import { describe, expect, it } from 'vitest';
import { aiShift, cadence, cadenceEfficiency, gearRatio } from './drivetrain';

describe('gearRatio', () => {
  it('maps cog index to 52T ratios', () => {
    expect(gearRatio(0)).toBeCloseTo(52 / 36, 6);
    expect(gearRatio(6)).toBe(3.25);
    expect(gearRatio(11)).toBeCloseTo(52 / 10, 6);
  });
});

describe('cadence', () => {
  it('cruise 10.95 m/s on 52x16 is ~96 rpm', () => {
    expect(cadence(10.95, 6)).toBeCloseTo(96.4, 1);
  });
  it('climb 5.4 m/s on 52x28 is ~83 rpm', () => {
    expect(cadence(5.4, 2)).toBeCloseTo(83.2, 1);
  });
  it('top gear at 20 m/s is ~110 rpm', () => {
    expect(cadence(20, 11)).toBeCloseTo(110.1, 1);
  });
  it('zero speed is zero cadence', () => {
    expect(cadence(0, 6)).toBe(0);
  });
});

describe('cadenceEfficiency', () => {
  it('full power inside [60,115] rpm', () => {
    expect(cadenceEfficiency(60)).toBe(1);
    expect(cadenceEfficiency(70)).toBe(1);
    expect(cadenceEfficiency(100)).toBe(1);
    expect(cadenceEfficiency(115)).toBe(1);
  });
  it('linear falloff to 0.55 between 40-60 and 115-140', () => {
    expect(cadenceEfficiency(50)).toBeCloseTo(0.775, 6);
    expect(cadenceEfficiency(120)).toBeCloseTo(0.91, 6);
    expect(cadenceEfficiency(125)).toBeCloseTo(0.82, 6);
  });
  it('clamps at 0.55 beyond the range', () => {
    expect(cadenceEfficiency(40)).toBe(0.55);
    expect(cadenceEfficiency(20)).toBe(0.55);
    expect(cadenceEfficiency(140)).toBe(0.55);
    expect(cadenceEfficiency(160)).toBe(0.55);
  });
});

describe('aiShift', () => {
  it('keeps cog near 95 rpm at cruise', () => {
    expect(aiShift(10.95, 6)).toBe(6);
  });
  it('shifts to 32T on climbs', () => {
    expect(aiShift(5.4, 6)).toBe(1);
  });
  it('shifts to 10T at speed', () => {
    expect(aiShift(20, 6)).toBe(11);
  });
  it('keeps current cog at standstill tie', () => {
    expect(aiShift(0, 6)).toBe(6);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：无法解析 `./drivetrain`

- [ ] **Step 3：修改 `src/sim/params.ts`** — 在 `RACE` 对象内追加两个补给字段（`trackWidth: 6,` 之后）：

```ts
  feedZones: [[560, 610], [2640, 2690]],
  feedRegenRate: 0.04,
```

并在文件末尾新增导出常量（types 的 cog 字段推迟到 Task 2，保证本提交 typecheck 全绿）：

```ts
export const DRIVETRAIN = {
  chainring: 52,
  cassette: [36, 32, 28, 24, 21, 18, 16, 14, 13, 12, 11, 10],
  wheelCirc: 2.096,
  cadFullLo: 60,
  cadFullHi: 115,
  cadFloor: 40,
  cadCeil: 140,
  effMin: 0.55,
  aiTargetCadence: 95,
  aiShiftHysteresis: 3,
  defaultCog: 6,
} as const;
```

- [ ] **Step 4：实现 `src/sim/drivetrain.ts`**

```ts
import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / D.cassette[cog];
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function cadenceEfficiency(cad: number): number {
  if (cad >= D.cadFullLo && cad <= D.cadFullHi) return 1;
  if (cad <= D.cadFloor || cad >= D.cadCeil) return D.effMin;
  if (cad < D.cadFullLo) {
    return D.effMin + ((cad - D.cadFloor) / (D.cadFullLo - D.cadFloor)) * (1 - D.effMin);
  }
  return D.effMin + ((D.cadCeil - cad) / (D.cadCeil - D.cadFullHi)) * (1 - D.effMin);
}

export function aiShift(speed: number, currentCog: number): number {
  let best = currentCog;
  let bestErr = Math.abs(cadence(speed, currentCog) - D.aiTargetCadence);
  for (let c = 0; c < D.cassette.length; c++) {
    const err = Math.abs(cadence(speed, c) - D.aiTargetCadence);
    if (err < bestErr - D.aiShiftHysteresis) {
      best = c;
      bestErr = err;
    }
  }
  return best;
}
```

- [ ] **Step 5：跑测试与类型检查** — `npm test`（预期 71/71 = 56 + 15）、`npm run typecheck`（0 错误——本任务不触碰 types，保持全绿）
- [ ] **Step 6：提交** — `git add src/sim/params.ts src/sim/drivetrain.ts src/sim/drivetrain.test.ts` 然后 `git commit -m "✨ feat(sim): add 12-speed drivetrain with cadence efficiency and ai shifting"`

### Task 2：cog 字段贯通与输入重映射

**Files:**
- Modify: `src/sim/types.ts`、`src/input.ts`、`src/sim/ai.ts`、`src/sim/race.ts`、`src/sim/draft.test.ts`、`src/sim/ai.test.ts`、`src/sim/race.test.ts`
- Test: `src/input.test.ts`（重写）

本任务只让 `cog` 字段**贯通**（类型/构造/传递），不改变任何行为（效率、AI 变速、补给都在 Task 3），保证提交全绿。

- [ ] **Step 1：重写失败测试 `src/input.test.ts`**

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

  it('shifts cassette heavier on ArrowUp', () => {
    press('ArrowUp');
    expect(c.command().cog).toBe(7);
  });
  it('shifts cassette lighter on ArrowDown', () => {
    press('ArrowDown');
    expect(c.command().cog).toBe(5);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cog).toBe(7);
  });
  it('cog clamps to 0..11', () => {
    for (let i = 0; i < 20; i++) press('ArrowDown');
    expect(c.command().cog).toBe(0);
    for (let i = 0; i < 20; i++) press('ArrowUp');
    expect(c.command().cog).toBe(11);
  });
  it('number keys set power gear', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cog).toBe(6);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：cog 相关断言失败（ArrowUp 仍改 gear）

- [ ] **Step 3：修改 `src/sim/types.ts`** — `RiderCommand` 与 `RiderState` 各增加 `cog: number;`（`gear` 字段之后）

- [ ] **Step 4：实现 `src/input.ts`**

```ts
import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cog: number = DRIVETRAIN.defaultCog;
  private steer = 0;
  private keys = new Set<string>();

  command(): RiderCommand {
    return { gear: this.gear, steer: this.steer, cog: this.cog };
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.cog = DRIVETRAIN.defaultCog;
    this.steer = 0;
    this.keys.clear();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cog = Math.min(DRIVETRAIN.cassette.length - 1, this.cog + 1);
    else if (k === 'arrowdown' || k === 's') this.cog = Math.max(0, this.cog - 1);
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

- [ ] **Step 5：修复全部类型引用**
  - `src/sim/ai.ts`：返回类型从 `RiderCommand` 改为 `{ gear: GearId; steer: number }`（import 调整为 `import type { GearId, RiderState } from './types';`，函数内三处 return 不变）
  - `src/sim/race.ts`：
    - `makeRider` 增加 `cog: DRIVETRAIN.defaultCog,`（`gear: 1,` 之后），并 `import { DRIVETRAIN, RACE } from './params';`
    - 完赛分支 `cmd = { gear: 0, steer: 0, cog: r.cog };`
    - AI 分支 `cmd = { ...aiCommand(r, a >= 0 ? riders[a] : null, Math.max(0, s.trackLength - r.dist), gradients[i]), cog: r.cog };`
    - 更新循环内 `r.cog = cmd.cog;`（`r.gear = cmd.gear;` 之后）
  - `src/sim/draft.test.ts` 与 `src/sim/ai.test.ts`：rider 工厂各增加 `cog: 6,`
  - `src/sim/race.test.ts`：`const cruise: RiderCommand = { gear: 1, steer: 0, cog: 6 };`；reckless 用例改为 `run({ gear: 3, steer: 0, cog: 6 }, 900)`

- [ ] **Step 6：跑测试与类型检查** — `npm test`（预期 72/72 = 71 + input 6 项替换原 5 项）、`npm run typecheck`（0 错误）；行为与之前完全一致（cog 仅存储未参与物理）
- [ ] **Step 7：提交** — `git add src/sim/types.ts src/input.ts src/input.test.ts src/sim/ai.ts src/sim/race.ts src/sim/draft.test.ts src/sim/ai.test.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(input): map W S to cassette shifting and thread cog through riders"`

### Task 3：race 行为集成（效率乘子 / AI 变速 / 补给 / 体力增强）

**Files:**
- Modify: `src/sim/race.ts`
- Test: `src/sim/race.test.ts`

- [ ] **Step 1：在 `src/sim/race.test.ts` 追加失败测试**（放在 describe 内末尾）

```ts
  it('feed zone restores energy while passing through', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 565, speed: 11, energy: 10000 })) };
    const before = s.riders[0].energy;
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBeGreaterThan(before + 2000);
  });
  it('energy caps at max inside feed zone', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 565, speed: 11, energy: 31900 })) };
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBe(32000);
  });
  it('heavy cog over the full lap loses to default cog', () => {
    const heavy = run({ gear: 1, steer: 0, cog: 8 }, 900).riders[0].finishTime!;
    const base = run({ gear: 1, steer: 0, cog: 6 }, 900).riders[0].finishTime!;
    expect(heavy).toBeGreaterThan(base);
  }, 30000);
```

- [ ] **Step 2：跑测试确认失败** — `npm test`，预期：补给测试失败（能量只降不升，恢复量 0）；重档测试失败或险过（当前无效率乘子）；cap 测试失败（玩家池仍是 24000，`toBe(32000)` 不成立）

- [ ] **Step 3：修改 `src/sim/race.ts`**

3a. 导入：`import { cadence, cadenceEfficiency, aiShift } from './drivetrain';`

3b. `PLAYER_TYPE.maxEnergy: 24000` → `32000`

3c. 冻結快照段——cmd 生成增加 AI 变速（0.5s 节拍判定）：

```ts
  const shiftTick = Math.floor((s.time + dt) / 0.5) > Math.floor(s.time / 0.5);
```

AI 分支的 cog 变为：

```ts
      const cog = shiftTick ? aiShift(r.speed, r.cog) : r.cog;
      cmd = { ...aiCommand(r, a >= 0 ? riders[a] : null, Math.max(0, s.trackLength - r.dist), gradients[i]), cog };
```

3d. 功率计算——`r.power` 行替换为（效率乘子）：

```ts
    r.power = r.finishTime !== null ? 0 : targetPower(cmd.gear, r.type.ftp, r.energy) * cadenceEfficiency(cadence(r.speed, cmd.cog));
```

3e. 补给恢复——`r.energy = stepEnergy(...)` 行替换为：

```ts
      r.energy = stepEnergy(r.energy, r.power, r.type.ftp, dt);
      if (RACE.feedZones.some(([lo, hi]) => r.dist >= lo && r.dist <= hi)) {
        r.energy = Math.min(r.type.maxEnergy, r.energy + RACE.feedRegenRate * r.type.maxEnergy * dt);
      }
```

- [ ] **Step 4：跑测试确认通过** — `npm test`，预期：全绿。重点核对三个既有长测的实测数值并记录在提交说明：全场完赛各车手成绩（AI 带变速应 ≈414-445s）、reckless vs steady（预估 reckless ≈520-540s 仍输）、avgPower ∈ (150,400)。若 reckless 反转或完赛超时：**不要调参**，报告 BLOCKED 与实测数据
- [ ] **Step 5：提交** — `git add src/sim/race.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): apply cadence efficiency, ai auto-shift, feed zones and bigger player tank"`

### Task 4：蓝色补给拱门

**Files:**
- Modify: `src/render/trackMesh.ts`

> 渲染层无单测（既定偏差），门禁：`npm run typecheck` + `npm run build` + `npm test` 保持全绿。

- [ ] **Step 1：把 `finishGates` 泛化为 `gate(track, dist, color)`**——posts 与横杆逻辑不变，位置取 `sampleAt(dist)`，横杆颜色由参数传入；`buildTrackMesh` 中：

```ts
  group.add(gate(track, 0, 0xe0533d));
  for (const [lo] of RACE.feedZones) {
    group.add(gate(track, lo, 0x4d8fd6));
  }
```

（`RACE` 已在该文件导入；终点门红色不变，补给门蓝色，位于各区入口 d=560/2640。）

- [ ] **Step 2：验证** — `npm run typecheck && npm run build && npm test`（全绿）
- [ ] **Step 3：提交** — `git add src/render/trackMesh.ts` 然后 `git commit -m "✨ feat(render): add blue feed zone gates"`

### Task 5：HUD 档位行与补给刻度

**Files:**
- Modify: `src/ui/hud.ts`、`src/game.ts`

- [ ] **Step 1：`src/ui/hud.ts`**
  - `HudView.gearName: string` 改名为 `gearLine: string`
  - `update` 中 `g('gear').textContent = \`档位 ${v.gearName}\`;` 改为 `g('gear').textContent = v.gearLine;`
  - `setProfile(track: Track)` 改为 `setProfile(track: Track, zones: readonly (readonly [number, number])[])`，保存 `this.zones = zones.map((z) => z[0]); this.profileLength = track.length;`
  - `drawElev` 在描完剖面线之后、画进度标之前追加补给刻度：

```ts
    ctx.fillStyle = '#4dd2ff';
    for (const lo of this.zones) {
      ctx.fillRect((lo / this.profileLength) * w - 1, 0, 2, h);
    }
```

（新增两个私有字段 `zones: number[] = []`、`profileLength = 1`。）

- [ ] **Step 2：`src/game.ts`**
  - 导入 `import { cadence } from './sim/drivetrain';` 与 `DRIVETRAIN, RACE`（params）
  - `this.hud.setProfile(this.track)` → `this.hud.setProfile(this.track, RACE.feedZones)`
  - `view()` 中 `gearName: GEARS[p.gear]` 替换为：

```ts
      gearLine: `52×${DRIVETRAIN.cassette[p.cog]} · ${cadence(p.speed, p.cog).toFixed(0)}rpm · ${GEARS[p.gear]}`,
```

- [ ] **Step 3：验证** — `npm run typecheck && npm run build && npm test`（全绿）
- [ ] **Step 4：提交** — `git add src/ui/hud.ts src/game.ts` 然后 `git commit -m "✨ feat(ui): show gear ratio, cadence and feed zone markers"`

### Task 6：三档外部音乐与接线

**Files:**
- Create: `public/music/SOURCES.md`
- Rewrite: `src/audio.ts`
- Modify: `src/game.ts`、`src/main.ts`

- [ ] **Step 1：创建 `public/music/SOURCES.md`**

```md
# 音乐素材

放置以下 mp3 文件（建议无缝循环、单个 ≤2MB）：

- calm.mp3    倒计时与完赛（平静）
- intense.mp3 比赛主体（轻快激烈）
- sprint.mp3  最后 800 米（冲刺）

缺失文件对应音乐层静音，游戏不受影响；补齐文件即生效，无需改代码。
请在下方登记素材来源与授权信息。
```

- [ ] **Step 2：重写 `src/audio.ts`**

```ts
export type MusicTier = 'calm' | 'intense' | 'sprint';

const FILES: Record<MusicTier, string> = {
  calm: '/music/calm.mp3',
  intense: '/music/intense.mp3',
  sprint: '/music/sprint.mp3',
};
const TIERS: MusicTier[] = ['calm', 'intense', 'sprint'];

export class Music {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private gains = new Map<MusicTier, GainNode>();
  private tier: MusicTier = 'calm';
  private volume = 0.35;
  private started = false;

  start(): void {
    if (this.started) return;
    this.started = true;
    this.ctx = new AudioContext();
    this.master = this.ctx.createGain();
    this.master.gain.value = this.volume;
    this.master.connect(this.ctx.destination);
    void this.load();
  }

  toggle(): boolean {
    if (!this.ctx || !this.master) return false;
    this.volume = this.volume > 0 ? 0 : 0.35;
    this.master.gain.setTargetAtTime(this.volume, this.ctx.currentTime, 0.1);
    return this.volume === 0;
  }

  setTier(tier: MusicTier): void {
    if (tier === this.tier) return;
    this.tier = tier;
    this.applyTier();
  }

  private applyTier(): void {
    const now = this.ctx?.currentTime ?? 0;
    this.gains.forEach((g, tier) => {
      g.gain.setTargetAtTime(tier === this.tier ? 1 : 0, now, 0.3);
    });
  }

  private async load(): Promise<void> {
    const ctx = this.ctx!;
    for (const tier of TIERS) {
      try {
        const res = await fetch(FILES[tier]);
        if (!res.ok) continue;
        const buf = await ctx.decodeAudioData(await res.arrayBuffer());
        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.loop = true;
        const g = ctx.createGain();
        g.gain.value = 0;
        src.connect(g);
        g.connect(this.master!);
        src.start();
        this.gains.set(tier, g);
        this.applyTier();
      } catch {
        continue;
      }
    }
  }
}
```

- [ ] **Step 3：接线**
  - `src/game.ts`：构造函数第三参数 `private music: Music`（导入 `import type { Music } from './audio';`——Game 只调用 `setTier`，用具体导入非 type-only 亦可，选 type-only 以保持渲染层纯净）；`render` 开头加：

```ts
    const remaining = s.trackLength - s.riders[0].dist;
    this.music.setTier(s.phase === 'racing' ? (remaining > 800 ? 'intense' : 'sprint') : 'calm');
```

  - `src/main.ts`：`const music = new Music();` 已有——把 `new Game(hud, input)` 改为 `new Game(hud, input, music)`

- [ ] **Step 4：验证** — `npm run typecheck && npm run build && npm test`（全绿；dist 应包含 music/SOURCES.md）
- [ ] **Step 5：提交** — `git add public/music/SOURCES.md src/audio.ts src/game.ts src/main.ts` 然后 `git commit -m "✨ feat(audio): three-tier external music with crossfade and graceful fallback"`

### Task 7：全量验证与手动验收

- [ ] **Step 1：全量门禁** — `npm test`（预期 75/75 = 72 + race 新增 3）、`npm run typecheck`（0 错误）、`npm run build`（成功）
- [ ] **Step 2：手动验收清单**（`npm run dev`，需人工）
  1. W/S 换档：HUD 显示 `52×T · rpm` 实时变化；1-4 切功率档名称联动
  2. 重档爬坡不降档：踏频掉出 60 以下、速度明显劣化；降档后恢复
  3. 两座蓝门（~560m / ~2640m）通过时体力条肉眼可见回升；高程图两处青色刻度
  4. 音乐三档随倒计时→比赛→最后 800m 切换、交叉淡变；M 静音；`public/music/` 无文件时游戏正常无声
  5. R 重开后 cog 归位 52×16
- [ ] **Step 3：回归数值记录**——把 reckless/steady、全场完赛区间、heavy-cog 差值写入交付说明

---

## Part C 覆盖与风险

### C1 规格覆盖映射

| 规格条目（2026-09-27 spec） | 任务 |
|---|---|
| 传动参数 / 踏频物理 / 效率乘子 | Task 1、3 |
| 操作映射（W/S 档位 + 1-4 功率） | Task 2 |
| AI 自动变速（0.5s / 95rpm / 滞回） | Task 1（aiShift）、3（节拍接入） |
| HUD `52×14 · rpm` | Task 5 |
| 玩家体力 32000 | Task 3 |
| 补给区 [560,610] [2640,2690]、4%/s、封顶 | Task 1（参数）、3（逻辑）、4（蓝门）、5（刻度） |
| 三档音乐 / 交叉淡变 / M 静音 / 缺文件降级 | Task 6 |
| 确定性保持 | Task 3（shiftTick 由 time 推导，无随机） |

### C2 风险与对策

| 风险 | 对策 |
|---|---|
| reckless 回归数值反转（效率乘子改变战术平衡） | Task 3 Step 4：实测报告，方向翻转即 BLOCKED 上报，不盲目调参 |
| 音乐文件尚未提供 | 缺文件静音降级（Task 6 已内建），用户补 `public/music/*.mp3` 即生效 |
| AI 变速在速度震荡时来回换档 | 3rpm 滞回 + 0.5s 节拍；Task 3 全场完赛回归观测 |
| cog=6 起步在倒计时结束瞬间重档 | eff 下限 0.55，实测加速正常；如观感差再调 defaultCog |

### C3 后续（不在本迭代内）

风声/链条声随速度（P1）、换档延迟与掉链、变速手感（自动补档提示）、回放。

