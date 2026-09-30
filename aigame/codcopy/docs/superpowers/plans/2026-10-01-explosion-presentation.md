# M17 爆炸演出层升级实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 空袭/集束引爆后有显眼大范围多层特效（冲击波/闪光/烟柱/焦痕）与持续声效（滚雷/燃烧余烬），incoming 阶段可见喷气机；手雷/油桶共用升级（mag=1）。

**Architecture:** 全部客户端演出层：`effects.ts` 重制 `explosion(pos, mag)` + 新增环/灯/焦痕池与余烬发射器、喷气机；`audio.ts` 爆炸分层 + 专属距离模型 + `burningLoop`；`main.ts` 按 cause 接线 mag/震动/节流；HUD/CSS 加近距闪光 overlay。服务端与协议零改动。设计 spec：`docs/superpowers/specs/2026-10-01-explosion-presentation-design.md`。

**Tech Stack:** TypeScript + three.js（InstancedMesh 粒子池）+ Web Audio 合成（零资源）+ vitest（node 环境，沿用 viewmodel/audio-params 纯逻辑测试模式）。

**可测性关键设计**：`Effects.update(dt, now = performance.now())` 注入时钟；effects 内部统一 `this.nowMs`（update 时刷新，explosion/ring/light/scorch/aftermath/jet 全部以 nowMs 记 born），测试可注入固定时间驱动淡出/清理逻辑。

**验证门槛（每任务后 + 最终）：** `npm run typecheck` + `npx vitest run` + `npm run build -w client` 全绿。

---

### Task 1: 音效层——爆炸分层 / 远距模型 / 燃烧声（TDD）

**Files:**
- Modify: `client/src/audio.ts`（`distanceCutoff` 后加两个纯函数；`playAt`/`emit` 加 mag；`'explode'` case 重制；新增 `burningLoop`）
- Test: `client/test/audio-params.test.ts`（追加 describe）

- [ ] **Step 1: 写失败测试** — `client/test/audio-params.test.ts` 末尾追加：

```ts
import { SHOT_PARAMS, distanceCutoff, explodeCutoff, burnWindowOpen } from '../src/audio';

describe('[M17] explosion audio', () => {
  it('explodeCutoff falls with distance and clamps to [240, 8000]', () => {
    expect(explodeCutoff(0)).toBeLessThanOrEqual(8000);
    expect(explodeCutoff(0)).toBeGreaterThan(explodeCutoff(60));
    expect(explodeCutoff(60)).toBeGreaterThan(explodeCutoff(120));
    expect(explodeCutoff(1000)).toBe(240);
  });

  it('explodeCutoff lets bass through farther than gunshot cutoff', () => {
    expect(explodeCutoff(80)).toBeLessThan(distanceCutoff(80));
    expect(explodeCutoff(80)).toBeGreaterThan(240);
  });

  it('burnWindowOpen gates one burn loop per cooldown window', () => {
    expect(burnWindowOpen(1000, 4000)).toBe(false);
    expect(burnWindowOpen(1000, 6000)).toBe(true);
    expect(burnWindowOpen(1000, 5999, 4999)).toBe(true);
  });
});
```

（import 合并进文件顶部现有 import 行，勿重复。）

- [ ] **Step 2: 确认失败**

Run: `npx vitest run client/test/audio-params.test.ts`
Expected: FAIL——`explodeCutoff`/`burnWindowOpen` 不存在。

- [ ] **Step 3: 实现**

3a. `client/src/audio.ts` 在 `distanceCutoff` 函数后追加：

```ts
/** [M17] 爆炸声专属远距低通：低频传得远，下限 240Hz */
export function explodeCutoff(dist: number): number {
  return Math.max(240, Math.min(8000, 8000 - dist * 55));
}

/** [M17] 连杀余烬声节流：冷却窗口内只放行一轮燃烧声（防 5-8 枚落弹各触发一份） */
export function burnWindowOpen(lastBurnAt: number, now: number, cooldownMs = 5000): boolean {
  return now - lastBurnAt >= cooldownMs;
}
```

3b. `playAt` 改签名与爆炸专属距离模型：

```ts
playAt(kind: SoundKind, x: number, y: number, z: number, mag = 1): void {
    if (!this.ctx || !this.master) return;
    const panner = this.ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    const boom = kind === 'explode';
    panner.refDistance = boom ? 10 : 4;
    panner.maxDistance = boom ? 200 : 80;
    panner.rolloffFactor = boom ? 1.0 : 1.4;
    panner.positionX.value = x;
    panner.positionY.value = y;
    panner.positionZ.value = z;
    const dist = Math.hypot(x - this.listenerPos.x, y - this.listenerPos.y, z - this.listenerPos.z);
    const lp = this.ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = boom ? explodeCutoff(dist) : distanceCutoff(dist);
    panner.connect(lp).connect(this.master);
    this.emit(kind, panner, mag);
}
```

3c. `emit` 签名 `private emit(kind: SoundKind, dest: AudioNode, mag = 1): void`；`'explode'` case 整体替换为：

```ts
case 'explode': {
    this.snap(dest, t, 2200, 0.05, 0.3 * mag);
    this.burst(dest, t, 850, 0.75, 0.6 * mag);
    this.thump(dest, t, 52, 0.9, 0.55 * mag);
    this.thump(dest, t + 0.05, 90, 0.3, 0.3 * mag);
    if (this.tailBus) {
      // [M17] 轰鸣层送混响总线 → 1.8s 滚雷回响尾
      const send = this.ctx!.createGain();
      send.gain.value = 0.5 * mag;
      this.burst(send, t, 700, 0.6, 0.5);
      send.connect(this.tailBus);
    }
    break;
}
```

3d. 类内新增 `burningLoop`（放在 `jetFlyby` 方法后）：

```ts
/** [M17] 落弹区持续燃烧轰鸣 + 随机噼啪（3D 定位，durSec 线性渐弱） */
burningLoop(x: number, z: number, durSec = 7): void {
    if (!this.ctx || !this.master || !this.noise) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const panner = ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    panner.refDistance = 6;
    panner.maxDistance = 60;
    panner.rolloffFactor = 1.2;
    panner.positionX.value = x;
    panner.positionY.value = 0.8;
    panner.positionZ.value = z;
    const lp = ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = 180;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.5, t);
    g.gain.linearRampToValueAtTime(0.0001, t + durSec);
    src.connect(lp).connect(g).connect(panner).connect(this.master);
    src.start(t);
    src.stop(t + durSec + 0.05);
    let at = t + 0.4;
    while (at < t + durSec - 0.2) {
      this.snap(panner, at, 1800 + Math.random() * 900, 0.03, 0.15);
      at += 0.3 + Math.random() * 0.4;
    }
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `npx vitest run client/test/audio-params.test.ts`
Expected: 全 PASS（原 3 项 + 新 3 项）。

- [ ] **Step 5: Commit**

```bash
git add client/src/audio.ts client/test/audio-params.test.ts
git commit -m "🔊 feat(codcopy): layered explosion audio with far reach and burning loop"
```

---

### Task 2: effects 爆炸多层重制 + 环/灯/焦痕池（TDD）

**Files:**
- Modify: `client/src/render/effects.ts`（构造器加 3 个池 + 粒子池扩容；`explosion`/`debris` 重制；`update` 加淡出驱动与 nowMs 时钟）
- Test: `client/test/effects.test.ts`（新建）

- [ ] **Step 1: 写失败测试** — 新建 `client/test/effects.test.ts`：

```ts
import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { Effects } from '../src/render/effects';

const v = new THREE.Vector3(5, 0.6, -5);

function visibleOf(list: { visible: boolean }[]): number {
  return list.filter((x) => x.visible).length;
}

describe('[M17] explosion layers', () => {
  it('spawns layered particles scaled by mag', () => {
    const fx = new Effects(new THREE.Scene());
    fx.update(0.016, 1000);
    fx.explosion(v, 1.6);
    const live = (fx as unknown as { live: { size: number; pool: unknown }[] }).live;
    expect(live.length).toBeGreaterThanOrEqual(70);
    expect(live.length).toBeLessThanOrEqual(85);
    const additivePool = (fx as unknown as { additivePool: unknown }).additivePool;
    const maxAdd = Math.max(...live.filter((p) => p.pool === additivePool).map((p) => p.size));
    expect(maxAdd).toBeCloseTo(2.4, 1); // 白热核心 1.5×1.6
  });

  it('mag=1 spawns exactly the base set (2+22+34+12)', () => {
    const fx = new Effects(new THREE.Scene());
    fx.update(0.016, 1000);
    fx.explosion(v, 1);
    expect((fx as unknown as { live: unknown[] }).live.length).toBe(70);
  });

  it('shockwave ring pool recycles at 8 and fades out via injected clock', () => {
    const fx = new Effects(new THREE.Scene());
    const rings = () => (fx as unknown as { rings: THREE.Mesh[] }).rings;
    fx.update(0.016, 1000);
    for (let i = 0; i < 10; i++) fx.explosion(v, 1.6);
    expect(visibleOf(rings())).toBe(8);
    fx.update(0.016, 2000); // age 1000ms > dur 450ms
    expect(visibleOf(rings())).toBe(0);
  });

  it('blast light and scorch gated by quality tier', () => {
    const fxLow = new Effects(new THREE.Scene());
    fxLow.quality = 'low';
    fxLow.update(0.016, 1000);
    fxLow.explosion(v, 1.6);
    const low = fxLow as unknown as { blastLights: THREE.PointLight[]; scorches: THREE.Mesh[] };
    expect(visibleOf(low.blastLights)).toBe(0);
    expect(visibleOf(low.scorches)).toBe(0);

    const fxMed = new Effects(new THREE.Scene());
    fxMed.quality = 'medium';
    fxMed.update(0.016, 1000);
    fxMed.explosion(v, 1.6);
    const med = fxMed as unknown as { blastLights: THREE.PointLight[]; scorches: THREE.Mesh[] };
    expect(visibleOf(med.blastLights)).toBe(1);
    expect(visibleOf(med.scorches)).toBe(1);
    fxMed.update(0.016, 14000); // scorch dur 12s
    expect(visibleOf(med.scorches)).toBe(0);
  });
});
```

- [ ] **Step 2: 确认失败**

Run: `npx vitest run client/test/effects.test.ts`
Expected: FAIL——`explosion` 当前签名无 mag（TS 编译错）或断言失败（粒子数 28≠70+）。

- [ ] **Step 3: 实现** — `client/src/render/effects.ts`：

3a. 类字段新增（muzzleLights 附近）：

```ts
private readonly rings: THREE.Mesh[] = [];
private ringIdx = 0;
private readonly blastLights: THREE.PointLight[] = [];
private lightBlastIdx = 0;
private readonly scorches: THREE.Mesh[] = [];
private scorchIdx = 0;
private nowMs = performance.now();
```

3b. 构造器：两处 `new InstancePool(scene, 320, ...)` 改为 `1024`；构造器末尾（muzzleLights 循环后）追加：

```ts
// [M17] 冲击波环池 ×8（全画质档）
const ringGeo = new THREE.RingGeometry(0.86, 1, 40);
for (let i = 0; i < 8; i++) {
  const m = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: 0xffc890, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
  m.rotation.x = -Math.PI / 2;
  m.visible = false;
  scene.add(m);
  this.rings.push(m);
}
// [M17] 爆炸闪光灯池 ×3（中/高档）
for (let i = 0; i < 3; i++) {
  const l = new THREE.PointLight(0xffa050, 0, 22);
  l.visible = false;
  scene.add(l);
  this.blastLights.push(l);
}
// [M17] 地面焦痕池 ×12（中/高档，12s 渐隐）
const scorchGeo = new THREE.CircleGeometry(1, 24);
for (let i = 0; i < 12; i++) {
  const m = new THREE.Mesh(scorchGeo, new THREE.MeshBasicMaterial({ color: 0x14161a, transparent: true, opacity: 0, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 }));
  m.rotation.x = -Math.PI / 2;
  m.visible = false;
  scene.add(m);
  this.scorches.push(m);
}
```

3c. `debris` 加 count 参数（`explosion` 是其唯一调用方）：

```ts
debris(pos: THREE.Vector3, color = 0xc98f42, count = 12): void {
    for (let i = 0; i < count; i++) {
      this.spawn(pos, {
        color,
        size: 0.16,
        vel: randVec(1).multiplyScalar(6).add(new THREE.Vector3(0, 2, 0)),
        life: 0.7,
        gravity: 14,
      });
    }
}
```

3d. `explosion` 整体替换：

```ts
explosion(pos: THREE.Vector3, mag = 1): void {
    const density = this.quality === 'low' ? 0.4 : this.quality === 'medium' ? 0.7 : 1;
    // 白热核心 + 橙红火球（additive 双层，峰值 ~5.2×mag）
    this.spawn(pos, { color: 0xfff6d0, size: 1.5 * mag, vel: new THREE.Vector3(0, 1.2, 0), life: 0.5, grow: 5, additive: true });
    this.spawn(pos, { color: 0xff9a2e, size: 0.8 * mag, vel: new THREE.Vector3(0, 2.2, 0), life: 0.4, grow: 9, additive: true });
    // 冲击波环：贴地扩散至 7×mag
    const ring = this.rings[this.ringIdx];
    this.ringIdx = (this.ringIdx + 1) % this.rings.length;
    ring.visible = true;
    ring.position.set(pos.x, 0.15, pos.z);
    ring.scale.setScalar(0.6 * mag);
    ring.userData.born = this.nowMs;
    ring.userData.dur = 450;
    ring.userData.mag = mag;
    (ring.material as THREE.MeshBasicMaterial).opacity = 0.85;
    // 上升烟柱（蘑菇状烟云）
    for (let i = 0; i < Math.round(34 * density); i++) {
      this.spawn(pos, {
        color: Math.random() < 0.5 ? 0x4a525c : 0x6a7480,
        size: 0.5 + Math.random() * 0.4,
        vel: new THREE.Vector3((Math.random() - 0.5) * 1.6, 2.2 + Math.random() * 1.4, (Math.random() - 0.5) * 1.6),
        life: 2.6 + Math.random() * 1.6,
        gravity: -0.6,
        grow: 2.5,
      });
    }
    // 余烬火星
    for (let i = 0; i < Math.round(22 * density); i++) {
      this.spawn(pos, {
        color: Math.random() < 0.5 ? 0xffc356 : 0xff8432,
        size: 0.06 + Math.random() * 0.04,
        vel: randVec(1).multiplyScalar(5 + Math.random() * 4).add(new THREE.Vector3(0, 3, 0)),
        life: 0.9 + Math.random() * 0.7,
        gravity: 12,
        additive: true,
      });
    }
    this.debris(pos, 0xc98f42, Math.round(12 * mag));
    // 爆炸闪光灯（中/高档）
    if (this.quality !== 'low') {
      const l = this.blastLights[this.lightBlastIdx];
      this.lightBlastIdx = (this.lightBlastIdx + 1) % this.blastLights.length;
      l.visible = true;
      l.intensity = 55 * mag;
      l.position.set(pos.x, pos.y + 1.5, pos.z);
      l.userData.born = this.nowMs;
      l.userData.dur = 400;
      l.userData.peak = 55 * mag;
    }
    // 地面焦痕（中/高档）
    if (this.quality !== 'low') {
      const s = this.scorches[this.scorchIdx];
      this.scorchIdx = (this.scorchIdx + 1) % this.scorches.length;
      s.visible = true;
      s.position.set(pos.x, 0.02, pos.z);
      s.scale.setScalar(2.4 * mag);
      s.rotation.z = Math.random() * Math.PI * 2;
      s.userData.born = this.nowMs;
      s.userData.dur = 12000;
      (s.material as THREE.MeshBasicMaterial).opacity = 0.82;
    }
}
```

3e. `update` 改签名并加池淡出驱动（方法开头替换）：

```ts
update(dt: number, now: number = performance.now()): void {
    this.nowMs = now;
```

原方法体内 `const now = performance.now();` 一行删除（其余逻辑用注入的 now）。在 muzzleLights 衰减循环之后、tracers 循环之前插入：

```ts
    // [M17] 冲击波环：扩散 + 淡出
    for (const r of this.rings) {
      if (!r.visible) continue;
      const k = (now - r.userData.born) / r.userData.dur;
      if (k >= 1) {
        r.visible = false;
        continue;
      }
      r.scale.setScalar((0.6 + 6.4 * k) * (r.userData.mag as number));
      (r.material as THREE.MeshBasicMaterial).opacity = 0.85 * (1 - k);
    }
    // [M17] 爆炸灯：二次方衰减
    for (const l of this.blastLights) {
      if (!l.visible) continue;
      const k = (now - l.userData.born) / l.userData.dur;
      if (k >= 1) {
        l.visible = false;
        l.intensity = 0;
        continue;
      }
      l.intensity = (l.userData.peak as number) * (1 - k) * (1 - k);
    }
    // [M17] 焦痕：12s 线性渐隐
    for (const s of this.scorches) {
      if (!s.visible) continue;
      const k = (now - s.userData.born) / s.userData.dur;
      if (k >= 1) {
        s.visible = false;
        continue;
      }
      (s.material as THREE.MeshBasicMaterial).opacity = 0.82 * (1 - k);
    }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `npx vitest run client/test/effects.test.ts`
Expected: 4 项全 PASS。

- [ ] **Step 5: Commit**

```bash
git add client/src/render/effects.ts client/test/effects.test.ts
git commit -m "✨ feat(codcopy): multi-layer explosion fx with shockwave, flash lights and scorch"
```

---

### Task 3: 余烬发射器 aftermath（TDD）

**Files:**
- Modify: `client/src/render/effects.ts`（emitters 数组 + `aftermath()` + update 驱动）
- Test: `client/test/effects.test.ts`（追加 describe）

- [ ] **Step 1: 写失败测试** — `client/test/effects.test.ts` 追加：

```ts
describe('[M17] burn aftermath emitters', () => {
  it('registers emitter, spawns fire+smoke over time, expires at until', () => {
    const fx = new Effects(new THREE.Scene());
    fx.update(0.016, 5000);
    fx.aftermath(3, -3, 3000);
    const ems = () => (fx as unknown as { emitters: { x: number; z: number; until: number; nextAt: number }[] }).emitters;
    expect(ems().length).toBe(1);
    fx.update(0.016, 5200); // ≥2 个 120ms 间隔
    expect((fx as unknown as { live: unknown[] }).live.length).toBeGreaterThanOrEqual(4);
    fx.update(0.016, 9000); // past until=8000
    expect(ems().length).toBe(0);
  });

  it('caps emitters at 4 with FIFO eviction', () => {
    const fx = new Effects(new THREE.Scene());
    fx.update(0.016, 1000);
    for (let i = 0; i < 5; i++) fx.aftermath(i, 0, 5000);
    const ems = (fx as unknown as { emitters: { x: number }[] }).emitters;
    expect(ems.length).toBe(4);
    expect(ems[0].x).toBe(1); // 第一个（x=0）被淘汰
  });
});
```

- [ ] **Step 2: 确认失败**

Run: `npx vitest run client/test/effects.test.ts`
Expected: 新 2 项 FAIL（`aftermath` 不存在）。

- [ ] **Step 3: 实现**

3a. 类字段新增：`private readonly emitters: { x: number; z: number; until: number; nextAt: number }[] = [];`

3b. 公有方法（`smokeMarker` 后）：

```ts
/** [M17] 连杀落弹区余烬：durMs 内持续火苗 + 浓烟（池 4，FIFO 淘汰） */
aftermath(x: number, z: number, durMs = 7000): void {
    if (this.emitters.length >= 4) this.emitters.shift();
    this.emitters.push({ x, z, until: this.nowMs + durMs, nextAt: this.nowMs });
}
```

3c. `update` 内（焦痕循环之后）插入：

```ts
    // [M17] 余烬发射器：每 120ms 火苗 + 浓烟
    for (let i = this.emitters.length - 1; i >= 0; i--) {
      const em = this.emitters[i];
      if (now >= em.until) {
        this.emitters.splice(i, 1);
        continue;
      }
      while (now >= em.nextAt) {
        const px = em.x + (Math.random() - 0.5) * 2.4;
        const pz = em.z + (Math.random() - 0.5) * 2.4;
        this.spawn(new THREE.Vector3(px, 0.3, pz), { color: 0xff9040, size: 0.25, vel: new THREE.Vector3(0, 1.4, 0), life: 0.5, additive: true });
        this.spawn(new THREE.Vector3(px, 0.5, pz), { color: 0x3c424a, size: 0.7, vel: new THREE.Vector3((Math.random() - 0.5) * 0.6, 1.8, (Math.random() - 0.5) * 0.6), life: 3, grow: 3 });
        em.nextAt += 120;
      }
    }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `npx vitest run client/test/effects.test.ts`
Expected: 6 项全 PASS。

- [ ] **Step 5: Commit**

```bash
git add client/src/render/effects.ts client/test/effects.test.ts
git commit -m "✨ feat(codcopy): sustained burn aftermath emitters at strike zones"
```

---

### Task 4: 可见喷气机 jet + jetPath（TDD）

**Files:**
- Modify: `client/src/render/effects.ts`（导出 `jetPath` 纯函数 + `jet()` + update 驱动 + disposeJet）
- Test: `client/test/effects.test.ts`（追加 describe）

- [ ] **Step 1: 写失败测试** — `client/test/effects.test.ts` 追加：

```ts
import { jetPath } from '../src/render/effects';

describe('[M17] jet flyby', () => {
  it('jetPath runs along heading through the target at y=14', () => {
    const start = jetPath(0, 0, 0, 0);
    expect(start.z).toBeCloseTo(70, 5); // 航线后方 70m
    expect(start.y).toBe(14);
    const mid = jetPath(0, 0, 0, 70 / 90);
    expect(mid.x).toBeCloseTo(0, 3);
    expect(mid.z).toBeCloseTo(0, 3);
    const end = jetPath(0, 0, 0, 140 / 90);
    expect(end.z).toBeCloseTo(-70, 3);
  });

  it('jet mesh is added on jet() and disposed after the run', () => {
    const scene = new THREE.Scene();
    const fx = new Effects(scene);
    const before = scene.children.length;
    fx.update(0.016, 1000);
    fx.jet(5, -5, 0);
    expect(scene.children.length).toBe(before + 1);
    fx.update(0.016, 1000 + 1700); // run = 140/90 ≈ 1.556s
    expect(scene.children.length).toBe(before);
  });
});
```

（import 合并进文件顶部现有 `Effects` import 行。）

- [ ] **Step 2: 确认失败**

Run: `npx vitest run client/test/effects.test.ts`
Expected: 新 2 项 FAIL（`jetPath`/`jet` 不存在）。

- [ ] **Step 3: 实现**

3a. 模块级常量与纯函数（文件尾部 `randVec` 前）：

```ts
export const JET_SPEED = 90;
const JET_Y = 14;
const JET_HALF_RUN = 70;

/** [M17] 喷气机航迹（纯函数）：tSec 秒时机身位置，从航线后方 70m 掠过目标至前方 70m */
export function jetPath(x: number, z: number, yaw: number, tSec: number): THREE.Vector3 {
  const dx = -Math.sin(yaw);
  const dz = -Math.cos(yaw);
  return new THREE.Vector3(x - dx * JET_HALF_RUN + dx * JET_SPEED * tSec, JET_Y, z - dz * JET_HALF_RUN + dz * JET_SPEED * tSec);
}
```

3b. 类字段：`private jetView: { x: number; z: number; yaw: number; born: number; group: THREE.Group; lastTrail: number } | null = null;`

3c. 公有方法（`aftermath` 后）：

```ts
/** [M17] 可见喷气机：沿航线 y=14 掠过（~1.56s，与 jetFlyby 音效对齐），双翼尖尾迹 */
jet(x: number, z: number, yaw: number): void {
    this.disposeJet();
    const g = new THREE.Group();
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.6, 5.6), toonMat(0x2a2e34));
    g.add(body);
    const wing = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.12, 1.3), toonMat(0x33383f));
    wing.position.z = 0.6;
    g.add(wing);
    const tail = new THREE.Mesh(new THREE.BoxGeometry(2.2, 0.1, 0.8), toonMat(0x33383f));
    tail.position.z = 2.4;
    g.add(tail);
    const fin = new THREE.Mesh(new THREE.BoxGeometry(0.1, 1.1, 1), toonMat(0x33383f));
    fin.position.set(0, 0.6, 2.5);
    g.add(fin);
    g.rotation.y = yaw;
    this.scene.add(g);
    this.jetView = { x, z, yaw, born: this.nowMs, group: g, lastTrail: this.nowMs };
}

private disposeJet(): void {
    if (!this.jetView) return;
    this.scene.remove(this.jetView.group);
    for (const m of this.jetView.group.children) {
      const mesh = m as THREE.Mesh;
      mesh.geometry.dispose();
      (mesh.material as THREE.Material).dispose();
    }
    this.jetView = null;
}
```

3d. `update` 内（余烬循环之后）插入：

```ts
    // [M17] 喷气机：航迹推进 + 翼尖尾迹
    if (this.jetView) {
      const j = this.jetView;
      const t = (now - j.born) / 1000;
      if (t >= (JET_HALF_RUN * 2) / JET_SPEED) {
        this.disposeJet();
      } else {
        j.group.position.copy(jetPath(j.x, j.z, j.yaw, t));
        if (now - j.lastTrail >= 30) {
          j.lastTrail = now;
          const px = -(-Math.cos(j.yaw));
          const pz = -(-Math.sin(j.yaw));
          for (const side of [-3.2, 3.2]) {
            this.spawn(new THREE.Vector3(j.group.position.x + px * side, JET_Y - 0.2, j.group.position.z + pz * side), { color: 0xdfe6ec, size: 0.5, vel: new THREE.Vector3(0, -0.2, 0), life: 1.8, grow: 2 });
          }
        }
      }
    }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `npx vitest run client/test/effects.test.ts`
Expected: 8 项全 PASS。

- [ ] **Step 5: Commit**

```bash
git add client/src/render/effects.ts client/test/effects.test.ts
git commit -m "✨ feat(codcopy): visible jet flyby with wingtip contrails"
```

---

### Task 5: HUD 近距闪光 overlay

**Files:**
- Modify: `client/index.html`（line 17 `#vignette` 后加一行）
- Modify: `client/src/style.css`（`#vignette` 规则后加一条）
- Modify: `client/src/hud.ts`（字段 + `blastFlash` 方法）

- [ ] **Step 1: index.html** — `<div id="vignette"></div>` 之后加：

```html
      <div id="blastflash"></div>
```

- [ ] **Step 2: style.css** — `#vignette` 规则行后加：

```css
#blastflash { position: absolute; inset: 0; opacity: 0; background: radial-gradient(ellipse at center, rgba(255,190,120,0.75) 0%, rgba(255,120,40,0.35) 45%, transparent 75%); mix-blend-mode: screen; pointer-events: none; transition: opacity 0.12s; }
```

- [ ] **Step 3: hud.ts** — `vignette` 字段行后加字段；`damageFlash()` 方法后加方法（同模式）：

```ts
private readonly blastflash = el<HTMLDivElement>('blastflash');
```

```ts
/** [M17] 近距爆炸橙白闪光（强度 0-1） */
blastFlash(a: number): void {
    this.blastflash.style.opacity = String(a);
    setTimeout(() => (this.blastflash.style.opacity = '0'), 120);
}
```

- [ ] **Step 4: 验证 + Commit**

Run: `npm run typecheck`; `npm run build -w client`
Expected: 均无错误（DOM 逻辑无单测，typecheck 把关）。

```bash
git add client/index.html client/src/style.css client/src/hud.ts
git commit -m "✨ feat(codcopy): blast flash hud overlay"
```

---

### Task 6: main.ts 接线（按 cause 区分 mag / 余烬节流 / 喷气机 / 闪光）

**Files:**
- Modify: `client/src/main.ts`（blast 分支重写 + streakUse 分支加一行 + 模块变量 + import）

本任务为纯接线（main.ts 无单测，typecheck + build + 全量回归把关）。

- [ ] **Step 1: 模块变量与 import**

`'./audio'` 现有 import 追加 `burnWindowOpen`；模块变量区（`let seq = 0;` 附近）加：

```ts
let lastBurnAt = -1e9;
```

- [ ] **Step 2: blast 分支整体替换**（现 main.ts:546-555）：

```ts
} else if (e.type === 'blast') {
    const p = new THREE.Vector3(e.pos.x, e.pos.y, e.pos.z);
    const streakBlast = e.cause === 'airstrike' || e.cause === 'cluster';
    const mag = streakBlast ? 1.6 : 1;
    fx.explosion(p, mag);
    audio.playAt('explode', p.x, p.y, p.z, mag);
    const s = predictor.state;
    const dist = Math.hypot(p.x - s.x, p.y - s.y - 1, p.z - s.z);
    if (streakBlast) {
      // [M17] 连杀爆炸：更大震感 + 持续余烬 + 近距闪光
      if (dist < 45) {
        shake = Math.max(shake, 0.75 * (1 - dist / 45));
        if (settings.padRumble) gamepad.rumble(now, 120, 0.6, 1);
      }
      if (burnWindowOpen(lastBurnAt, now, 5)) {
        lastBurnAt = now;
        audio.burningLoop(p.x, p.z);
        fx.aftermath(p.x, p.z);
      }
      if (dist < 28) hud.blastFlash(Math.max(0, 1 - dist / 28) * 0.9);
    } else if (dist < 18) {
      shake = Math.max(shake, 0.45 * (1 - dist / 18));
      if (settings.padRumble) gamepad.rumble(now, 120, 0.6, 1);
    }
}
```

（`now` 为 main.ts 模块级秒时钟，冷却参数 5 = 5 秒。）

- [ ] **Step 3: streakUse 分支**——现有 `audio.jetFlyby(e.target.x, e.target.z, e.yaw ?? 0);` 之后加：

```ts
      fx.jet(e.target.x, e.target.z, e.yaw ?? 0);
```

- [ ] **Step 4: 验证 + Commit**

Run: `npm run typecheck`; `npx vitest run`; `npm run build -w client`
Expected: 全绿（全量 = 159 + audio 3 + effects 8 = 170 项）。

```bash
git add client/src/main.ts
git commit -m "🔌 feat(codcopy): wire strike presentation into blast and streakuse handlers"
```

---

### Task 7: 最终验证与交付

- [ ] **Step 1: 三门槛**

Run: `npm run typecheck`; `npx vitest run`; `npm run build -w client`
Expected: 全部通过（全量 = 159 + 3 + 8 = 170 项）。

- [ ] **Step 2: 手动冒烟清单（交付说明中列出，可选执行）**

`npm run dev` 本地局，分别在低/中/高档画质下：
1. 攒 5 杀呼叫空袭 → 高空可见喷气机沿航线掠过（带双翼尾迹），与呼啸音同步
2. 3s 后沿航线 5 连爆：白热火球 ~8m、冲击波环扩散、橙色闪光打亮周围、上升烟柱
3. 落弹区随后 6-8s 持续冒烟起火 + 低频燃烧轰鸣渐弱（远距仍可闻）
4. 近距（<28m）时屏幕橙白闪光 + 明显震感（45m 内）
5. 集束：8 点散布连爆 + 大片余烬区
6. 手雷/油桶：同款多层爆炸（mag=1，无余烬），音效更厚
7. 地面焦痕 ~12s 渐隐（低档无灯/焦痕）

- [ ] **Step 3: 最终整体 code review（subagent）+ 汇总交付**
