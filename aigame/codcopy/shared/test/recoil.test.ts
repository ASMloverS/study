import { describe, expect, it } from 'vitest';
import { WEAPONS, addRecoilShot, createRecoilState, updateRecoil } from '../src';

describe('recoil', () => {
  it('accumulates pitch per shot up to the cap', () => {
    const s = createRecoilState();
    for (let i = 0; i < 40; i++) addRecoilShot(s, WEAPONS.ar, i * 0.1, () => 0.5);
    expect(s.pitch).toBe(6);
    expect(s.yaw).toBe(0);
  });

  it('yaw drifts randomly within bounds', () => {
    const s = createRecoilState();
    for (let i = 0; i < 50; i++) addRecoilShot(s, WEAPONS.ar, i * 0.1, () => 0.99);
    expect(s.yaw).toBe(3);
    for (let i = 0; i < 50; i++) addRecoilShot(s, WEAPONS.ar, 10 + i * 0.1, () => 0.01);
    expect(s.yaw).toBe(-3);
  });

  it('holds during recovery delay then recovers to zero', () => {
    const s = createRecoilState();
    addRecoilShot(s, WEAPONS.ar, 0, () => 0.5);
    updateRecoil(s, 0.1, 0.01);
    expect(s.pitch).toBe(WEAPONS.ar.recoilPitch);
    updateRecoil(s, 0.5, 0.01);
    expect(s.pitch).toBeLessThan(WEAPONS.ar.recoilPitch);
    expect(s.pitch).toBeGreaterThan(0);
    updateRecoil(s, 5, 0.5);
    expect(s.pitch).toBe(0);
    expect(s.yaw).toBe(0);
  });
});
