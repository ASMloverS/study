import { describe, expect, it } from 'vitest';
import { GRAVITY, stepProjectile, type AABB } from '../src';

function box(minX: number, minY: number, minZ: number, maxX: number, maxY: number, maxZ: number): AABB {
  return { minX, minY, minZ, maxX, maxY, maxZ };
}

describe('projectile sim', () => {
  it('applies gravity each step', () => {
    const p = { x: 0, y: 5, z: 0, vx: 0, vy: 0, vz: 0 };
    stepProjectile(p, 1 / 30, [], 0.35, 0.09);
    expect(p.vy).toBeCloseTo(GRAVITY / 30);
    expect(p.y).toBeLessThan(5);
  });

  it('bounces off the ground with restitution', () => {
    const p = { x: 0, y: 5, z: 0, vx: 2, vy: -10, vz: 1 };
    for (let i = 0; i < 60; i++) stepProjectile(p, 1 / 30, [], 0.35, 0.09);
    expect(p.y).toBeGreaterThanOrEqual(0.09);
    expect(Math.abs(p.vy)).toBeLessThanOrEqual(10);
  });

  it('reflects off AABB walls without tunneling', () => {
    const wall = box(2, 0, -1, 2.4, 3, 1);
    const p = { x: 0, y: 1, z: 0, vx: 14, vy: 0, vz: 0 };
    for (let i = 0; i < 30; i++) stepProjectile(p, 1 / 30, [wall], 0.35, 0.09);
    expect(p.x).toBeLessThan(2);
    expect(p.vx).toBeLessThan(0);
  });
});
