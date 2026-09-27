# 公路车迭代实现计划：真实速度档 + 全感官速度感

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现设计文档 `docs/superpowers/specs/2026-09-27-cycling-realistic-speed-feel-design.md`：速度回调至真实职业档（巡航 ~50km/h、冲刺 63、下坡 110-150）、赛道参数化 15km 默认（150km 保留）、AI/冲刺窗/道具簇等比缩放、全套感官速度感（风噪/相机微抖/环境加密/低速速度线/HUD 弹跳/过弯倾斜/轮组模糊/FOV 重标）。

**Architecture:** 顺序依赖——先缩短赛道（Task 1，超高速中间态下测试仍快），再回调物理（Task 2，真实速度下 15km 全程仿真 ~1300-1600s 仍在测试预算内）；感官层 Task 3-6 相互独立、纯渲染/音频层无单测走门禁；Task 7 回归。sim 确定性不变。

**Tech Stack:** 不变（TypeScript strict + Vite + Three.js + Vitest）。

---

## Part A 关键决策与预演数值

### A1 决策落点

| 项 | 决策 |
|---|---|
| 顺序 | 赛道参数化（超高速中间态 ~150s 完赛）→ 物理回调（真实速度 15km ~25min）→ 感官层 |
| 物理 | `gearRatios [0.6,1,1.5,2.5]`（180-750W）、`CdA 0.13`、`maxDriveForce 450` 保留 |
| 赛道 | `buildTrack(stageKm = RACE.stageKm=15)`：SPACING=stageKm×1000/200，剖面 u 归一化关键点 × 幅度 `min(1000, 22×stageKm)`，蛇形+双掉头不变 |
| 缩放 | sprintDist ×(trackLength/150000)（createRace 处）；音乐/HUD 冲刺窗 = 4% 赛段长；道具簇 = 赛段 10%-90% 每 10% 一簇共 8 簇 |
| 速度线 | 阈值 8 m/s、透明度 (speed−8)/35 封顶 0.55、长度 speed×0.55 封顶 28m |
| FOV | `65 + min(20, speed×1.5)` |
| 风噪 | 白噪声 bandpass（300+speed×30 Hz、Q 0.8）→ gain min(0.5,(speed/40)²)，共用 master |
| 环境 | 树每 22m 双侧（确定性伪随机横向 8-35m、尺度 0.7-1.6）InstancedMesh ≈1362 棵；每 500m 里程桩 |
| 相机抖 | 幅度 min(0.22,(speed−8)/70)，多频正弦叠加到位置（速度 <8 无抖） |
| 倾斜/模糊 | roll = 钳(±0.35, 帧间 lateral 差 ×6)；踏频 >120rpm 轮辐↔模糊盘切换 |

### A2 预演数值（测试期望依据）

- CdA 0.13 → 阻力系数 0.0797：巡航 300W ≈ 13.7 m/s（49.3km/h）；冲刺 750W ≈ 17.6（63.4）；跟车冲刺 ≈ 18.7（67）；−12% 滑行 32.7（118）；−15% 踩踏 ~41（148）
- 赛道 15km：长度 14.4-15.6km；幅度 330m（测试带 280-380）；坡度带：爬升段均值 7.4%（cosine 峰 ~11.6%）、下坡段均值 8.2%（峰 ~12.9%）→ |grad| ≤ 0.16 且含 ≥+7%/≤−5%
- 道具簇：d = 1500, 3000, …, 13500（8 簇 24 箱，索引 3k/3k+1/3k+2，横向 −1.7/0/+1.7）
- sprintDist 缩放 0.1×：climber 180m、sprinter 525m 等
- 物理测试带：300W 收敛 (13.5,15.5)、750W (17,19)、−12%+750W (32,40)、起步单步 `1+(450−3.826−0.0797)/78/60`
- Task 2 后全程 ~1300-1700s（cruise 玩家 soft-pedal 交替 ~45km/h 平路 + 300W 爬坡 ~17km/h）；reckless 实测后定契约（预估 1100-1600s）

### A3 文件清单

```
修改  src/sim/params.ts         gearRatios/CdA 回调；stageKm；道具簇按赛段推导
重写  src/sim/trackData.ts      参数化剖面 + stageKm 参数
修改  src/sim/race.ts           createRace sprintDist 缩放
修改  src/audio.ts              风噪层 + setWind
修改  src/render/camera.ts      微抖 + FOV 新曲线
修改  src/render/speedLines.ts  阈值/曲线重标
修改  src/render/trackMesh.ts   树 InstancedMesh 重排 + 里程桩
修改  src/render/riders.ts      模糊盘 + spokes 引出；placeRider lean 参数
修改  src/ui/hud.ts + style.css 速度弹跳
修改  src/game.ts               接线（setWind/lean/blur/4% 冲刺窗）
测试  trackData.test 重写；race.test（道具位置/cruise 回归/时长/缩放用例）；physics.test 重标
```

---

## Part B 实施任务（TDD，按序执行）

### Task 0：基线

- [ ] `npm test` 全绿（89/89，记录）；`npm run typecheck` 0 错误；`npm run build` 成功——不绿先停下排查

### Task 1：赛道参数化 15km + 等比缩放

**Files:**
- Modify: `src/sim/params.ts`、`src/sim/race.ts`、`src/game.ts`、`src/render/trackMesh.ts`
- Rewrite: `src/sim/trackData.ts`
- Test: `src/sim/trackData.test.ts`（重写）、`src/sim/race.test.ts`（道具位置 + 缩放用例）

- [ ] **Step 1：重写 `src/sim/trackData.test.ts`**：

```ts
import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack parameterized stage', () => {
  const track = buildTrack();
  it('is a ~15km point-to-point course', () => {
    expect(track.length).toBeGreaterThan(14400);
    expect(track.length).toBeLessThan(15600);
  });
  it('has mountain profile between 280 and 380m max elevation', () => {
    let max = -Infinity;
    for (let d = 0; d < track.length; d += 50) max = Math.max(max, track.sampleAt(d).y);
    expect(max).toBeGreaterThan(280);
    expect(max).toBeLessThan(380);
  });
  it('keeps gradients within ±16% with steep climbs and descents', () => {
    let maxG = 0;
    let minG = 0;
    for (let d = 0; d < track.length; d += 10) {
      const g = track.sampleAt(d).gradient;
      maxG = Math.max(maxG, g);
      minG = Math.min(minG, g);
    }
    expect(maxG).toBeLessThanOrEqual(0.16);
    expect(minG).toBeGreaterThanOrEqual(-0.16);
    expect(maxG).toBeGreaterThanOrEqual(0.07);
    expect(minG).toBeLessThanOrEqual(-0.05);
  });
  it('supports a 150km endurance configuration', () => {
    const long = buildTrack(150);
    expect(long.length).toBeGreaterThan(148500);
    expect(long.length).toBeLessThan(151500);
    let max = -Infinity;
    for (let d = 0; d < long.length; d += 500) max = Math.max(max, long.sampleAt(d).y);
    expect(max).toBeGreaterThan(900);
    expect(max).toBeLessThanOrEqual(1000);
  });
});
```

- [ ] **Step 2：`src/sim/race.test.ts` 两处道具用例位置 + 一处新增**：
  1. `item box picked up when crossing at matching lateral` 与 `no pickup when lateral misses all boxes` 与 `each box is evaluated exactly once per rider` 中 `dist: 4999.5` → `dist: 1499.5`、`dist: 4999.9` → `dist: 1499.9`（首簇移至 1500m，`collected[1]` 断言不变）
  2. `ai riders also pick up boxes` 中 `dist: 4999.5` → `dist: 1499.5`
  3. describe 末尾追加：

```ts
  it('scales sprint distance to stage length', () => {
    const s = createRace(track);
    const scale = track.length / 150000;
    expect(s.riders[1].type.sprintDist).toBeCloseTo(AI_FIELD[0].sprintDist * scale, 0);
  });
```

  （import 行补 `AI_FIELD`：`import { AI_FIELD, createRace, standings, stepRace } from './race';`）

- [ ] **Step 3：跑测试确认失败** — 长度/高程/道具位置/缩放用例失败（现 150km、簇在 5km）
- [ ] **Step 4：`src/sim/params.ts`**——`RACE` 增 `stageKm: 15`；删 `boxFirst/boxLast/boxClusterEvery`；`ITEM_BOXES` IIFE 替换为：

```ts
export const ITEM_BOXES: ItemBox[] = (() => {
  const L = RACE.stageKm * 1000;
  const boxes: ItemBox[] = [];
  for (let k = 1; k <= 8; k++) {
    for (const lat of RACE.boxOffsets) boxes.push({ d: L * 0.1 * k, lat });
  }
  return boxes;
})();
```

- [ ] **Step 5：重写 `src/sim/trackData.ts`**：

```ts
import { Track } from './track';
import { RACE } from './params';

const COUNT = 200;

const PROFILE_U: [number, number][] = [
  [0, 0], [0.05, 0.10], [0.14, 0.05], [0.28, 0.30], [0.48, 0.97], [0.56, 0.90],
  [0.64, 1.0], [0.70, 0.92], [0.88, 0.25], [0.94, 0.10], [1, 0],
];

function elevationAt(u: number, amp: number): number {
  if (u <= 0) return 0;
  if (u >= 1) return 0;
  for (let i = 1; i < PROFILE_U.length; i++) {
    if (u <= PROFILE_U[i][0]) {
      const [u0, f0] = PROFILE_U[i - 1];
      const [u1, f1] = PROFILE_U[i];
      const t = (u - u0) / (u1 - u0);
      const s = 0.5 - 0.5 * Math.cos(Math.PI * t);
      return (f0 + (f1 - f0) * s) * amp;
    }
  }
  return 0;
}

function turn(u: number, lo: number, hi: number): number {
  const t = Math.min(1, Math.max(0, (u - lo) / (hi - lo)));
  return t * t * (3 - 2 * t);
}

export function buildTrack(stageKm: number = RACE.stageKm): Track {
  const spacing = (stageKm * 1000) / COUNT;
  const amp = Math.min(1000, 22 * stageKm);
  const ctrl: [number, number, number][] = [];
  let x = 0;
  let z = 0;
  const headingAt = (u: number) =>
    0.6 * Math.sin(u * Math.PI * 6) + Math.PI * turn(u, 0.42, 0.46) + Math.PI * turn(u, 0.78, 0.82);
  for (let i = 0; i < COUNT; i++) {
    ctrl.push([x, elevationAt(i / COUNT, amp), z]);
    const mid = headingAt((i + 0.5) / COUNT);
    x += Math.cos(mid) * spacing;
    z += Math.sin(mid) * spacing;
  }
  return new Track(ctrl, 8000);
}
```

  注：TypeScript 控制流分析可能要求 elevationAt 尾部兜底 return——如报 TS2366 则补 `return 0;`。

- [ ] **Step 6：`src/sim/race.ts` `createRace`**——sprintDist 按赛段缩放：

```ts
export function createRace(track: Track): RaceState {
  const scale = track.length / 150000;
  const riders: RiderState[] = [makeRider(0, { ...PLAYER_TYPE, sprintDist: PLAYER_TYPE.sprintDist * scale }, true, 0, 0.8)];
  for (let i = 0; i < AI_FIELD.length; i++) {
    const t = AI_FIELD[i];
    riders.push(makeRider(i + 1, { ...t, sprintDist: t.sprintDist * scale }, false, (i + 1) * 1.6, (i % 2 === 0 ? -0.8 : 0.8)));
  }
  return { phase: 'countdown', time: 0, countdown: RACE.countdown, riders, trackLength: track.length, rngState: 20260926, results: [] };
}
```

- [ ] **Step 7：`src/game.ts`**——音乐/HUD 冲刺窗 4%：render 中 `remaining > 5000` → `remaining > s.trackLength * 0.04`；view 中 `remaining <= 5000` → `remaining <= s.trackLength * 0.04`
- [ ] **Step 8：`src/render/trackMesh.ts`**——`buildItemBoxes` 箱色 `(cluster % 29) / 29` → `cluster / 8`
- [ ] **Step 9：验证** — `npm test` 全绿（89 + 1 缩放用例 = 90）；typecheck/build 绿；记录超高速中间态全场完赛时间（~150-250s）
- [ ] **Step 10：提交** — `git add src/sim/params.ts src/sim/trackData.ts src/sim/trackData.test.ts src/sim/race.ts src/sim/race.test.ts src/game.ts src/render/trackMesh.ts` 然后 `git commit -m "✨ feat(sim): parameterized 15km stage with proportional sprint and item scaling"`

### Task 2：物理回调（真实职业档）

**Files:**
- Modify: `src/sim/params.ts`
- Test: `src/sim/physics.test.ts`（重标）、`src/sim/race.test.ts`（cruise 回归 + 时长契约）

- [ ] **Step 1：重写 `src/sim/physics.test.ts` 数值**：

```ts
import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';

describe('stepSpeed', () => {
  it('caps drive force at maxDriveForce at low speed', () => {
    const v = stepSpeed(1, 750, 0, false, 1 / 60);
    expect(v).toBeCloseTo(1 + (450 - 3.826 - 0.0797) / 78 / 60, 4);
  });
  it('converges to ~13.7 m/s at 300W on flat', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 300, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(13.5);
    expect(v).toBeLessThan(15.5);
  });
  it('converges to ~17.6 m/s at 750W on flat', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 750, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(17);
    expect(v).toBeLessThan(19);
  });
  it('reaches ~33 m/s on -12% descent with sprint power', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 750, -0.12, false, 1 / 60);
    expect(v).toBeGreaterThan(32);
    expect(v).toBeLessThan(40);
  });
  it('never returns negative', () => {
    expect(stepSpeed(0.5, 0, 0.08, false, 1 / 60)).toBeGreaterThanOrEqual(0);
  });
});
```

- [ ] **Step 2：`src/sim/race.test.ts`**：
  1. `cruise` 常量回归 `{ gear: 1, steer: 0, cadDelta: 0, cogDelta: 0 }`（真实速度下 cog 16 + 110rpm soft-pedal 交替 ~45km/h，无需 cogDelta）
  2. reckless 用例 cmd 改 `{ gear: 3, steer: 0, cadDelta: 0, cogDelta: 0 }`；`run(..., 1700)` → `run(..., 2000)`，断言 `< 1700` → 实测后收紧为 `实测值 + 15%`（Step 4 记录）
  3. soft-pedal 用例不动（cog 16/cadTarget 80/speed 12 → cad 105.7 ≥ 80 仍滑行）
  4. full-race 守卫 `60 * 2500` 保持（预估 1300-1700s < 2500s）
- [ ] **Step 3：跑测试确认失败** — physics 新带（CdA 0.002 下 300W→104m/s）、race 时长/cruise 相关失败
- [ ] **Step 4：`src/sim/params.ts`**——`gearRatios: [3.6, 6, 9, 15]` → `[0.6, 1, 1.5, 2.5]`；`PHYS.CdA: 0.002` → `0.13`。随后 `npm test` 记录实测：全场完赛各车手时间、reckless 完赛时间，按实测收紧 reckless 断言
- [ ] **Step 5：验证** — `npm test` 全绿、typecheck/build 绿
- [ ] **Step 6：提交** — `git add src/sim/params.ts src/sim/physics.test.ts src/sim/race.test.ts` 然后 `git commit -m "✨ feat(sim): realistic pro-tier physics with aero posture drag"`（正文附实测）

### Task 3：风噪声效

**Files:**
- Modify: `src/audio.ts`、`src/game.ts`

> 音频层无单测（既定偏差），门禁：typecheck + build + test 全绿。

- [ ] **Step 1：`src/audio.ts` Music 类**——`start()` 中 master 连接后追加风噪链路（先读现有结构对齐字段名）：

```ts
    const len = 2 * this.ctx.sampleRate;
    const buf = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.loop = true;
    this.windFilter = this.ctx.createBiquadFilter();
    this.windFilter.type = 'bandpass';
    this.windFilter.frequency.value = 300;
    this.windFilter.Q.value = 0.8;
    this.windGain = this.ctx.createGain();
    this.windGain.gain.value = 0;
    src.connect(this.windFilter);
    this.windFilter.connect(this.windGain);
    this.windGain.connect(this.master);
    src.start();
```

  私有字段 `private windGain: GainNode | null = null;`、`private windFilter: BiquadFilterNode | null = null;`；新增公共方法：

```ts
  setWind(speed: number): void {
    if (!this.ctx || !this.windGain || !this.windFilter) return;
    this.windGain.gain.setTargetAtTime(Math.min(0.5, (speed / 40) ** 2), this.ctx.currentTime, 0.1);
    this.windFilter.frequency.setTargetAtTime(300 + speed * 30, this.ctx.currentTime, 0.1);
  }
```

  （噪声 Math.random 属渲染/音频层，sim 确定性不受影响；M 键经 master 全静。）
- [ ] **Step 2：`src/game.ts` render()**——`this.music.setTier(...)` 之后加 `this.music.setWind(s.riders[0].speed);`
- [ ] **Step 3：验证** — typecheck/build/test 全绿
- [ ] **Step 4：提交** — `git add src/audio.ts src/game.ts` 然后 `git commit -m "✨ feat(audio): speed-driven wind noise layer"`

### Task 4：相机微抖 + FOV 重标 + 速度线重标

**Files:**
- Modify: `src/render/camera.ts`、`src/render/speedLines.ts`

- [ ] **Step 1：`src/render/camera.ts`**——整体替换：

```ts
import * as THREE from 'three';

export class ChaseCamera {
  private t = 0;

  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number, speed: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.camera.position.x += (target.x - fx * 6 - this.camera.position.x) * k;
    this.camera.position.y += (target.y + 2.4 - this.camera.position.y) * k;
    this.camera.position.z += (target.z - fz * 6 - this.camera.position.z) * k;
    this.camera.lookAt(target.x + fx * 18, target.y + 1.2 + gradient * 6, target.z + fz * 18);
    this.t += frameDt;
    const amp = Math.min(0.22, Math.max(0, (speed - 8) / 70));
    this.camera.position.x += amp * (Math.sin(this.t * 13.7) * 0.6 + Math.sin(this.t * 7.3 + 1.7) * 0.4);
    this.camera.position.y += amp * (Math.sin(this.t * 11.3 + 0.5) * 0.6 + Math.sin(this.t * 17.1) * 0.4);
    this.camera.fov = 65 + Math.min(20, speed * 1.5);
    this.camera.updateProjectionMatrix();
  }
}
```

- [ ] **Step 2：`src/render/speedLines.ts` `update()`**——两行替换：

```ts
    mat.opacity = Math.max(0, Math.min(0.55, (speed - 8) / 35));
```

```ts
    const len = Math.min(28, speed * 0.55);
```

- [ ] **Step 3：验证 + 提交** — typecheck/build/test 全绿后 `git add src/render/camera.ts src/render/speedLines.ts` → `git commit -m "✨ feat(render): speed-scaled fov, camera shake and low-threshold speed lines"`

### Task 5：环境加密（路旁树阵 + 里程桩）

**Files:**
- Modify: `src/render/trackMesh.ts`

- [ ] **Step 1：`trees()` 替换为等距双侧树阵**（确定性伪随机：正弦哈希代替 mulberry32 随机游走）：

```ts
function trees(track: Track): THREE.Group {
  const step = 22;
  const perSide = Math.floor(track.length / step);
  const count = perSide * 2;
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
  for (let i = 0; i < perSide; i++) {
    const d = (i + 0.5) * step;
    const s = track.sampleAt(d);
    for (const side of [-1, 1]) {
      const h = Math.sin(d * 0.7 + side * 13.7) * 0.5 + 0.5;
      const off = 8 + h * 27;
      const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
      const x = s.x + nx * side * off;
      const z = s.z + nz * side * off;
      const sc = 0.7 + h * 0.9;
      const ground = s.y + baseHills(x, z) * smoothstep(off, 12, 80) - 0.15;
      m.makeScale(sc, sc, sc);
      m.setPosition(x, ground + 1.2 * sc, z);
      trunks.setMatrixAt(placed, m);
      m.setPosition(x, ground + 5.15 * sc, z);
      crowns.setMatrixAt(placed, m);
      placed++;
    }
  }
  const g = new THREE.Group();
  g.add(trunks, crowns);
  return g;
}
```

- [ ] **Step 2：新增 `kmPosts()` 并加入 buildTrackMesh**：

```ts
function kmPosts(track: Track): THREE.InstancedMesh {
  const n = Math.floor(track.length / 500);
  const mesh = new THREE.InstancedMesh(
    new THREE.BoxGeometry(0.18, 1.1, 0.18),
    new THREE.MeshLambertMaterial({ color: 0xf2f2f2 }),
    n,
  );
  const m = new THREE.Matrix4();
  for (let i = 1; i <= n; i++) {
    const s = track.sampleAt(i * 500);
    const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
    const x = s.x + nx * (RACE.trackWidth / 2 + 0.6);
    const z = s.z + nz * (RACE.trackWidth / 2 + 0.6);
    m.identity();
    m.setPosition(x, s.y - 0.15 + 0.55, z);
    mesh.setMatrixAt(i - 1, m);
  }
  return mesh;
}
```

  `buildTrackMesh` 中 `group.add(trees(track));` 后加 `group.add(kmPosts(track));`。删除不再使用的 `mulberry32` import（如无其他引用）。
- [ ] **Step 3：验证 + 提交** — typecheck/build/test 全绿后 `git add src/render/trackMesh.ts` → `git commit -m "✨ feat(render): dense roadside tree lines and km posts"`

### Task 6：HUD 速度弹跳 + 过弯倾斜 + 轮组模糊

**Files:**
- Modify: `src/ui/hud.ts`、`src/style.css`、`src/render/riders.ts`、`src/game.ts`

- [ ] **Step 1：`src/ui/hud.ts`**——Hud 类增私有字段 `private prevKmh = 0;`、`private punchT: ReturnType<typeof setTimeout> | null = null;`；`update(v)` 速度行后追加：

```ts
    const acc = v.speedKmh - this.prevKmh;
    this.prevKmh = v.speedKmh;
    if (acc > 0.8) {
      g('speed').classList.add('punch');
      if (this.punchT) clearTimeout(this.punchT);
      this.punchT = setTimeout(() => g('speed').classList.remove('punch'), 160);
    }
```

- [ ] **Step 2：`src/style.css`**——追加：

```css
#speed {
  transition: transform 0.15s ease-out;
  transform-origin: left center;
}
#speed.punch {
  transform: scale(1.15);
}
```

- [ ] **Step 3：`src/render/riders.ts`**：
  1. `placeRider` 签名改 `(mesh: THREE.Object3D, s: TrackSample, lateral: number, lean = 0)`，末尾加 `mesh.rotation.z = lean;`
  2. `buildRiderMesh` 内：轮辐循环中把每根 spoke 收集进局部 `const spokes: THREE.Object3D[] = []`；两轮位置各建一个模糊盘并默认隐藏：

```ts
  const blur: THREE.Mesh[] = [];
  for (const z of [0.52, -0.52]) {
    const disc = new THREE.Mesh(
      new THREE.CylinderGeometry(0.34, 0.34, 0.06, 16),
      new THREE.MeshBasicMaterial({ color: 0x1a1d22, transparent: true, opacity: 0.35 }),
    );
    disc.rotation.z = Math.PI / 2;
    disc.position.set(0, 0.34, z);
    disc.visible = false;
    group.add(disc);
    blur.push(disc);
  }
  group.userData.spokes = spokes;
  group.userData.blur = blur;
```

- [ ] **Step 4：`src/game.ts` render() 骑手循环**——替换为（含倾斜与模糊切换）：

```ts
    for (let i = 0; i < s.riders.length; i++) {
      const p = this.prev.riders[i];
      const c = s.riders[i];
      const lean = Math.max(-0.35, Math.min(0.35, (c.lateral - p.lateral) * 6));
      placeRider(this.meshes[i], this.track.sampleAt(p.dist + (c.dist - p.dist) * alpha), p.lateral + (c.lateral - p.lateral) * alpha, lean);
      const blurOn = cadence(c.speed, c.cog) > 120;
      (this.meshes[i].userData.spokes as THREE.Object3D[]).forEach((sp) => (sp.visible = !blurOn));
      (this.meshes[i].userData.blur as THREE.Mesh[]).forEach((d) => (d.visible = blurOn));
    }
```

- [ ] **Step 5：验证 + 提交** — typecheck/build/test 全绿后 `git add src/ui/hud.ts src/style.css src/render/riders.ts src/game.ts` → `git commit -m "✨ feat(render): hud speed punch, cornering lean and wheel blur"`

### Task 7：全量回归与手动验收

- [ ] **Step 1：全量门禁** — `npm test` 全绿（90 用例）、typecheck、build；确定性用例绿
- [ ] **Step 2：回归数值记录**——真实档全场完赛时间（预估 1300-1700s）、reckless 实测与契约、爬坡/下坡峰值速度（遥测或脚本：全程 max(speed) 应 ∈ [30, 45] m/s 区间量级）、AI 名次分布
- [ ] **Step 3：手动验收清单**（`npm run dev`，需人工）
  1. 巡航 ~50km/h、冲刺 ~63-68、下坡 110-150km/h；风噪随速增强（M 全静）
  2. >30km/h 速度线显现；高速相机微抖；路旁树阵高速掠过；里程桩
  3. 过弯车身倾斜；高踏频轮组模糊；加速时速度数字弹跳
  4. 爬坡明显掉速（300W 爬坡 ~17-30km/h 视档位）、降档/发力恢复
  5. 15km 单局 ~20-28 分钟；道具 8 簇位置正确；小地图/仪表盘/终点提示正常
- [ ] **Step 4：最终整体审查**（子代理 code-reviewer，范围 = 本迭代全部提交，对照 spec §7/§8）

---

## Part C 覆盖与风险

### C1 规格覆盖映射

| 规格条目（realistic-speed-feel spec） | 任务 |
|---|---|
| §2 物理回调（gearRatios/CdA/牵引保留） | Task 2 |
| §3 赛道参数化 15km + 150km 保留 | Task 1 |
| §4 等比缩放（sprintDist/冲刺窗 4%/道具 8 簇） | Task 1 |
| §5.1 风噪 | Task 3 |
| §5.2 相机微抖 / §5.8 FOV / §5.4 速度线 | Task 4 |
| §5.3 环境加密 | Task 5 |
| §5.5 HUD 弹跳 / §5.6 倾斜 / §5.7 轮模糊 | Task 6 |
| §6 不变项守卫（解锁/道具/小地图/仪表盘等） | 各任务门禁 + Task 7 |
| §8 验收 | Task 7 |

### C2 风险与对策

| 风险 | 对策 |
|---|---|
| 剖面 cosine 峰值梯度超 ±16%（Catmull 采样叠加抖动） | Task 1 Step 3 红测把关；超限则加宽对应关键点间距（0.28-0.48 / 0.70-0.88 已留裕度） |
| 真实档全程仿真时长上升（~1500s × 60Hz × 8 骑手） | 实测 Task 1 中间态与 Task 2 后单测套件耗时（预估 <5s 总量）；超预算则 full-race 断言复用单次 run |
| reckless 真实档契约（体力耗尽爬坡） | Task 2 Step 2 实测后收紧断言为实测+15%，不预拍数值 |
| 相机微抖经 lerp 反馈放大 | 幅度 ≤0.22m、频率成分有界；lerp 每帧回收 ~9.5%；手动验收异常则改为独立 basePos（记录备选） |
| 树阵与回头弯近距走廊（8.2m@150km、15km 按比例 ~0.8m?） | 15km 缩放后双掉头弯平行腿间距按比例缩小——树 offset 下限 8m 可能落邻腿路面；Task 5 实现时如可视化冲突，将回头弯窗口拉宽（0.42-0.50/0.78-0.86）以扩大间隙，属容差内微调 |
| 风噪/噪声 Math.random 影响确定性 | 均在渲染/音频层，sim 无涉；确定性用例守卫 |



