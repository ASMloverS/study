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
