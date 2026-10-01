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
