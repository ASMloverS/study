# 公路车迭代实现计划：踏频控制 / 体力强化 / 音乐嵌入

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 `docs/superpowers/specs/2026-09-27-cycling-cadence-control-design.md`：齿比全自动跟随目标踏频（地形默认 90/110/120，W/S ±5 微调、换地形清零）、玩家池 40000 + 补给 25%/区、FreePD CC0 音乐三首嵌入。

**Architecture:** 控制反转——玩家不再直接选齿比，`RiderCommand.cog` 替换为一次性事件 `cadDelta`；`RiderState` 增加 `cadOffset`/`cadTerrain`（sim 拥有状态与地形重置逻辑，确定性保持）；`aiShift` 目标参数化后玩家与 AI 共用同一自动变速路径（玩家目标=地形+偏移，AI 恒 95）。

**Tech Stack:** 不变。音乐素材经 https://freepd.com（CC0）下载至 `public/music/`。

---

## Part A 关键决策与预演数值

### A1 决策落点

| 项 | 决策 |
|---|---|
| 控制模型 | 齿比全自动（0.5s 节拍，含 3rpm 滞回）；W/S=一次性 `cadDelta ±5` 事件（command() 消费即清零，tick 间多次按键累加）；偏移钳 ±30 在 sim 侧 |
| 地形判定 | `terrainCadence(gradient)`：`> +0.02` → 90；`< −0.02` → 120；否则 110。存 `RiderState.cadTerrain`，变化时 `cadOffset` 清零 |
| 效率曲线 | 满效 [80,125]；线性 50–80 / 125–150 → 0.55；≤50 / ≥150 钳 0.55 |
| AI | 恒 95rpm（不随地形）；`aiShift(speed, cog, target = 95)` 参数化 |
| 体力 | 玩家 40000（纯池冲刺 160s，加双区 25% 共 240s）；`feedZoneGain 0.25`（玩家每区 +10000J） |
| HUD | `52×18 · 96/110rpm · 巡航`（实际/目标，差 ≤3 只显示实际） |

### A2 预演数值（测试期望依据）

- 效率边界：eff(50)=0.55、eff(65)=0.775、eff(70)=0.85、eff(80)=1、eff(100)=1、eff(120)=1、eff(125)=1、eff(140)=0.55+(150−140)/25×0.45=0.73、eff(150)=0.55、eff(170)=0.55
- `terrainCadence`：0.025→90、0.02→110（严格大于才爬坡）、0→110、−0.02→110、−0.025→120
- `aiShift` 目标参数化：`aiShift(10.95, 6, 110)=5`（18T，cad 108.5 err 1.5 胜任 16T 的 err 13.6）；`aiShift(5.4, 6, 90)=1`（32T err 5.1）；默认 95 的四例不变（6/1/11/6）
- 爬坡自动变速：5.4 m/s 目标 90 → 32T（index 1）；平路 10.95 m/s 目标 110 → 18T（index 5）
- reckless 红线预估：40000+20000=60000J ÷ 250J/s = 240s 冲刺 ≈ 3100m，其后 105W×eff 爬行 → ≈530-590s vs 匀速自动挡 ≈440s（实测为准，翻转即 BLOCKED 上报）
- 补给：0.25×40000×(11/60×dt)/50 = 36.7 J/tick，120 tick ≈ +4340J

### A3 文件清单

```
修改  src/sim/params.ts        DRIVETRAIN 窗口/地形/步进常量；RACE.feedZoneGain 0.25
修改  src/sim/drivetrain.ts    terrainCadence；aiShift 目标参数化
修改  src/sim/types.ts         RiderCommand.cog→cadDelta；RiderState +cadOffset/+cadTerrain
重写  src/input.ts             W/S→cadDelta 事件；重写 input.test.ts
修改  src/sim/race.ts          统一自动变速、地形重置、玩家 40000
修改  src/sim/race.test.ts     cmd 形态、删 heavy-cog、reckless 简化、新增地形/变速用例、补给断言
修改  src/sim/drivetrain.test.ts 效率边界新值 + terrainCadence/aiShift(target) 用例
修改  src/game.ts              HUD 实际/目标踏频
下载  public/music/{calm,intense,sprint}.mp3 + 更新 SOURCES.md
```

---

## Part B 实施任务（TDD，按序执行）

### Task 0：基线

- [ ] 分支 `feature/cycling-cadence-iteration`（已建）；`npm test`（72/72）、`npm run typecheck` 全绿

### Task 1：drivetrain 曲线窗口与地形函数

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/drivetrain.ts`、`src/sim/drivetrain.test.ts`

- [ ] **Step 1：更新测试**——`drivetrain.test.ts` 三处：
  1. `cadenceEfficiency` 两个用例替换为：

```ts
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
});
```

  2. 新增 describe：

```ts
describe('terrainCadence', () => {
  it('climb above +2%, descent below -2%, flat otherwise', () => {
    expect(terrainCadence(0.025)).toBe(90);
    expect(terrainCadence(0.02)).toBe(110);
    expect(terrainCadence(0)).toBe(110);
    expect(terrainCadence(-0.02)).toBe(110);
    expect(terrainCadence(-0.025)).toBe(120);
  });
});
```

  3. `aiShift` describe 追加两例：

```ts
  it('honors custom target 110 on flat', () => {
    expect(aiShift(10.95, 6, 110)).toBe(5);
  });
  it('honors custom target 90 on climb', () => {
    expect(aiShift(5.4, 6, 90)).toBe(1);
  });
```

  import 行补 `terrainCadence`。

- [ ] **Step 2：跑测试确认失败** — 效率新边界、terrainCadence、aiShift 目标参数均失败
- [ ] **Step 3：params** — `DRIVETRAIN` 中 `cadFullLo: 60→80`、`cadFullHi: 115→125`、`cadFloor: 40→50`、`cadCeil: 140→150`，并追加：

```ts
  climbCadence: 90,
  flatCadence: 110,
  descentCadence: 120,
  climbGradient: 0.02,
  descentGradient: -0.02,
  cadenceStep: 5,
  cadOffsetMax: 30,
```

  `RACE.feedZoneGain: 0.18` → `0.25`（实现期修正：该值与 32000 中间态组合会翻转 reckless 红线——实测 437.3s < 445.6s，二分定位后移至 Task 3 与 40000 池同批落地）。

- [ ] **Step 4：drivetrain.ts**——新增：

```ts
export function terrainCadence(gradient: number): number {
  if (gradient > D.climbGradient) return D.climbCadence;
  if (gradient < D.descentGradient) return D.descentCadence;
  return D.flatCadence;
}
```

  `aiShift` 签名改 `(speed: number, currentCog: number, target: number = D.aiTargetCadence)`，函数体内两处 `D.aiTargetCadence` 替换为 `target`。

- [ ] **Step 5：验证** — `npm test`（drivetrain 全绿，其余不动仍绿）、`npm run typecheck` 0 错误
- [ ] **Step 6：提交** — `git add src/sim/params.ts src/sim/drivetrain.ts src/sim/drivetrain.test.ts` 然后 `git commit -m "✨ feat(sim): widen cadence window and add terrain default cadences"`

### Task 2：types 与输入事件模型

**Files:**
- Modify: `src/sim/types.ts`、`src/sim/ai.ts`、`src/sim/race.ts`（编译适配）、`src/sim/draft.test.ts`、`src/sim/ai.test.ts`、`src/sim/race.test.ts`
- Rewrite: `src/input.ts`、`src/input.test.ts`

SCOPE：类型与通路切换（cog→cadDelta），race 本任务只做编译适配（cog 决策移到 Task 3），行为与既有测试全绿。

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
  beforeEach(() => {
    c = new InputController();
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
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cadDelta).toBe(5);
  });
  it('number keys set power gear and leave cadence untouched', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cadDelta).toBe(0);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
```

- [ ] **Step 2：跑测试确认失败** — cadDelta 断言全挂（旧字段 cog）
- [ ] **Step 3：`src/sim/types.ts`**——`RiderCommand` 的 `cog: number` 改为 `cadDelta: number`；`RiderState` 在 `cog: number` 之后追加：

```ts
  cadOffset: number;
  cadTerrain: number;
```

- [ ] **Step 4：重写 `src/input.ts`**

```ts
import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cadDelta = 0;
  private steer = 0;
  private keys = new Set<string>();

  command(): RiderCommand {
    const cmd = { gear: this.gear, steer: this.steer, cadDelta: this.cadDelta };
    this.cadDelta = 0;
    return cmd;
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.cadDelta = 0;
    this.steer = 0;
    this.keys.clear();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cadDelta += DRIVETRAIN.cadenceStep;
    else if (k === 'arrowdown' || k === 's') this.cadDelta -= DRIVETRAIN.cadenceStep;
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

- [ ] **Step 5：编译适配**
  - `src/sim/race.ts`：`makeRider` 在 `cog: DRIVETRAIN.defaultCog,` 后追加 `cadOffset: 0, cadTerrain: DRIVETRAIN.flatCadence,`；cmds map 三分支删除 cog 组装（完赛 `{ gear: 0, steer: 0, cadDelta: 0 }`、AI `{ ...aiCommand(...), cadDelta: 0 }`、玩家 `return playerCmd`）；删除更新循环中 `r.cog = cmd.cog;`；在 `r.gear = cmd.gear;` 之后、`effort` 之前插入 AI 变速（行为保持，否则全场完赛测试会因 AI 卡 52×16 而挂）：

```ts
    if (!r.isPlayer && shiftTick) {
      r.cog = aiShift(r.speed, r.cog);
    }
```

  （`r.power` 行中的 `cmd.cog` 同步改为 `r.cog`；玩家 cog 本任务恒为 defaultCog，与既有测试固定 cog 6 一致。）
  - `src/sim/draft.test.ts`、`src/sim/ai.test.ts`：rider 工厂 `cog: 6,` 后追加 `cadOffset: 0, cadTerrain: 110,`
  - `src/sim/race.test.ts`：`cruise` 与 reckless 的 cmd 改为 `{ gear: 1|3, steer: 0, cadDelta: 0 }`；`paced` 用例删除手动 aiShift 循环与相关 import（paced 与 cruise 现在等价，直接 `run(cruise, 900)`）
- [ ] **Step 6：验证** — `npm test`（73/73 = 72 − 6 旧 input + 7 新 input）、`npm run typecheck` 0 错误
- [ ] **Step 7：提交** — `git add src/sim/types.ts src/input.ts src/input.test.ts src/sim/race.ts src/sim/draft.test.ts src/sim/ai.test.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(input): replace cog control with one-shot cadence delta events"`

### Task 3：race 集成（玩家自动变速 / 地形重置 / 40000 / 补给 25%）

**Files:**
- Modify: `src/sim/race.ts`、`src/sim/race.test.ts`

- [ ] **Step 1：更新 `src/sim/race.test.ts`**
  1. reckless 用例简化（paced 已在 Task 2 与 cruise 合并）：

```ts
  it('reckless sprinting loses to steady pacing', () => {
    const steady = run(cruise, 900).riders[0].finishTime!;
    const reckless = run({ gear: 3, steer: 0, cadDelta: 0 }, 900).riders[0].finishTime!;
    expect(reckless).toBeGreaterThan(steady);
  }, 30000);
```

  2. 删除 `heavy cog over the full lap loses to default cog` 用例（手动齿比机制已移除）。
  3. 补给两用例：cap 断言 `toBe(32000)` → `toBe(40000)`，teleport energy `31900` → `39900`（regen 断言 `> before + 2000` 不变，25% 下 ≈ +4340）。
  4. 追加两用例：

```ts
  it('terrain switch resets cadence offset', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 900, speed: 5.4 })) };
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].cadTerrain).toBe(90);
    expect(s.riders[0].cadOffset).toBe(0);
  });
  it('cog follows terrain target on climb', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 900, speed: 5.4 })) };
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].cog).toBeLessThanOrEqual(2);
  });
```

- [ ] **Step 2：跑测试确认失败** — cap 40000、terrain/cog 新用例失败（逻辑未接入）
- [ ] **Step 3：`src/sim/race.ts`**
  1. `PLAYER_TYPE.maxEnergy: 32000` → `40000`；`RACE.feedZoneGain: 0.18` → `0.25`（自 Task 1 移入；若 reckless 红线翻转（<30s 余量），按预授权阶梯下调 `emptyCapRatio` 0.35→0.30→0.25 直至余量 ≥30s 并在提交正文记录所选值——池与补给不动，只惩罚失控管理）
  2. import 增加 `terrainCadence`（drivetrain）
  3. 更新循环内，把 Task 2 的临时 AI 变速块替换为统一逻辑：

```ts
    const terrain = terrainCadence(gradients[i]);
    if (terrain !== r.cadTerrain) {
      r.cadTerrain = terrain;
      r.cadOffset = 0;
    }
    if (r.isPlayer) {
      r.cadOffset = clamp(r.cadOffset + cmd.cadDelta, -DRIVETRAIN.cadOffsetMax, DRIVETRAIN.cadOffsetMax);
    }
    const cadTarget = r.isPlayer ? r.cadTerrain + r.cadOffset : DRIVETRAIN.aiTargetCadence;
    if (shiftTick) {
      r.cog = aiShift(r.speed, r.cog, cadTarget);
    }
```

  （位置：`r.gear = cmd.gear;` 之后、`effort` 之前；`gradients[i]` 与 `shiftTick` 均已在作用域内。）
- [ ] **Step 4：跑测试确认通过并记录数值** — 全绿（75/75 = 73 − 1 删 heavy-cog + 2 新 + reckless 保持）。记录：全场完赛区间（玩家自动挡预估 ≈440s）、reckless vs steady（红线：reckless 必须更慢；预估 ≈530-590s vs ≈440s，翻转即 BLOCKED 上报实测）、AI 油箱
- [ ] **Step 5：提交** — `git add src/sim/race.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): auto-shift player by terrain cadence with offset reset and 40k tank"`（正文附实测）

### Task 4：HUD 实际/目标踏频

**Files:**
- Modify: `src/game.ts`

- [ ] **Step 1：`view()` 中 gearLine 替换为**（import 增加 `terrainCadence` 不需要——目标直接取 rider 状态）：

```ts
      gearLine: (() => {
        const cad = cadence(p.speed, p.cog);
        const target = p.cadTerrain + p.cadOffset;
        const rpm = Math.abs(cad - target) > 3 ? `${cad.toFixed(0)}/${target}` : cad.toFixed(0);
        return `52×${DRIVETRAIN.cassette[p.cog]} · ${rpm}rpm · ${GEARS[p.gear]}`;
      })(),
```

- [ ] **Step 2：验证** — typecheck / build / `npm test`（75/75）全绿
- [ ] **Step 3：提交** — `git add src/game.ts` 然后 `git commit -m "✨ feat(ui): show actual and target cadence in gear line"`

### Task 5：FreePD CC0 音乐下载嵌入

**Files:**
- Create: `public/music/calm.mp3`、`public/music/intense.mp3`、`public/music/sprint.mp3`
- Modify: `public/music/SOURCES.md`

- [ ] **Step 1：勘察目录页** — `curl -s https://freepd.com/upbeat.php` 等（upbeat/epic/piano/comedy 等分类页），提取曲目名、作者与 `music/*.mp3` 直链；选曲取向（用户要求"轻快活泼"，忌阴郁）：calm=轻柔钢琴/原声、intense=明快律动、sprint=高能推进
- [ ] **Step 2：下载** — `curl -L -o public/music/<tier>.mp3 "https://freepd.com/music/<Track>.mp3"`；校验：文件 > 300KB、非 HTML（前 3 字节 ID3 或 0xFF）、时长合理（1-4 分钟）
- [ ] **Step 3：SOURCES.md 登记** — 每首：曲名 / 作者 / 来源 URL / CC0 授权声明
- [ ] **Step 4：验证** — `npm run build` 后 `dist/music/*.mp3` 三件齐全；`npm test` 仍 75/75
- [ ] **Step 5：提交** — `git add public/music` 然后 `git commit -m "✨ feat(audio): embed three CC0 tracks from FreePD"`

### Task 6：全量验证与收尾

- [ ] **Step 1：全量门禁** — `npm test`（75/75）、`npm run typecheck`、`npm run build`
- [ ] **Step 2：最终整体审查**（子代理 code-reviewer，范围 = 本分支全部提交，对照 spec）
- [ ] **Step 3：手动验收清单**（`npm run dev`）
  1. W/S 即时调目标踏频，HUD 显示 实际/目标，换地形（爬坡/下坡）默认切换且偏移清零
  2. 齿比全程自动：爬坡 HUD 大飞轮（28-36T）、平路 14-18T、下坡 10-12T
  3. 冲刺体感宽裕（40000 池），全程无脑冲刺仍输
  4. 开箱即有三档音乐且随比赛状态切换；M 静音；SOURCES.md 登记完整
- [ ] **Step 4：合并回 main（fast-forward）+ 删除分支**

---

## Part C 覆盖与风险

### C1 规格覆盖映射

| 规格条目 | 任务 |
|---|---|
| 齿比全自动 + 0.5s/3rpm 滞回 | Task 3 |
| 地形默认 90/110/120（±2% 阈值）+ 偏移清零 | Task 1（terrainCadence）、3（重置） |
| W/S ±5 一次性事件、±30 钳位 | Task 2、3 |
| 效率窗口 [80,125]/50-150 | Task 1 |
| AI 恒 95 | Task 3（cadTarget 分支） |
| 玩家 40000 + 补给 25% | Task 3 |
| HUD 实际/目标 | Task 4 |
| FreePD 三首嵌入 + SOURCES.md | Task 5 |

### C2 风险与对策

| 风险 | 对策 |
|---|---|
| reckless 红线翻转（池加大后无脑冲刺变最优） | Task 3 Step 4 实测红线，翻转即 BLOCKED 上报（预估余量 ~90-150s） |
| FreePD 直链失效/防爬 | curl 失败换曲或换分类页重试；全败则上报人工介入（不引入非 CC0 素材） |
| 玩家变速在起步 v≈0 时抖动 | aiShift 静止平局保持当前档（已测）；eff 0.55 下限保底 |
| 音乐文件偏大（>5MB） | 选曲时控制在 ≤5MB；FreePD 多为 1-3MB |



