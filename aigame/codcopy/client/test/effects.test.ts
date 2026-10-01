import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { Effects, jetPath } from '../src/render/effects';

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
    expect(live.length).toBe(77);
    const additivePool = (fx as unknown as { additivePool: unknown }).additivePool;
    const maxAdd = Math.max(...live.filter((p) => p.pool === additivePool).map((p) => p.size));
    expect(maxAdd).toBeCloseTo(2.4, 1); // 白热核心 1.5×1.6
    const fxAny = fx as unknown as { blastLights: THREE.PointLight[]; scorches: THREE.Mesh[] };
    expect(fxAny.blastLights[0].userData.peak).toBe(88); // 55×1.6
    expect(fxAny.scorches[0].scale.x).toBeCloseTo(3.84, 2); // 2.4×1.6
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

  it('wingtip contrails are perpendicular to the flight path at diagonal headings', () => {
    const fx = new Effects(new THREE.Scene());
    fx.update(0.016, 1000);
    fx.jet(0, 0, Math.PI / 4);
    fx.update(0.016, 1031); // 触发一次尾迹生成（30ms 间隔）
    const live = (fx as unknown as { live: { pos: THREE.Vector3 }[] }).live;
    expect(live.length).toBe(2);
    const dx = -Math.sin(Math.PI / 4);
    const dz = -Math.cos(Math.PI / 4);
    // 中点 ≈ 机体中心（在航线上）；断言翼尖相对中点的偏移垂直于航线
    const mid = live[0].pos.clone().add(live[1].pos).multiplyScalar(0.5);
    for (const p of live) {
      const off = p.pos.clone().sub(mid);
      const dot = off.x * dx + off.z * dz;
      expect(Math.abs(dot)).toBeLessThan(0.01);
    }
  });
});
