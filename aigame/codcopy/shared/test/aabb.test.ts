import { describe, expect, it } from 'vitest';
import { boxCenter, moveBodyAxis, surfaceYAt, type Body, type AABB } from '../src';

function body(x: number, y: number, z: number, height = 1.8, radius = 0.4): Body {
  return { x, y, z, height, radius };
}

describe('moveBodyAxis', () => {
  const wall = boxCenter(5, 1.5, 0, 0.5, 3, 6);

  it('blocks +x movement and clamps to wall face minus radius', () => {
    const b = body(4, 0, 0);
    const blocked = moveBodyAxis(b, 'x', 0.5, [wall]);
    expect(blocked).toBe(true);
    expect(b.x).toBeCloseTo(4.75 - 0.4, 5);
  });

  it('allows free movement when no obstacle', () => {
    const b = body(0, 0, 0);
    expect(moveBodyAxis(b, 'z', 2, [wall])).toBe(false);
    expect(b.z).toBe(2);
  });

  it('lands on top of box when falling', () => {
    const crate = boxCenter(0, 0.6, 0, 1.2, 1.2, 1.2);
    const b = body(0, 2, 0);
    const blocked = moveBodyAxis(b, 'y', -0.9, [crate]);
    expect(blocked).toBe(true);
    expect(b.y).toBeCloseTo(1.2, 5);
  });

  it('hits ceiling when jumping under box', () => {
    const ceiling = boxCenter(0, 2.2, 0, 4, 0.4, 4);
    const b = body(0, 0.7, 0);
    const blocked = moveBodyAxis(b, 'y', 0.3, [ceiling]);
    expect(blocked).toBe(true);
    expect(b.y).toBeCloseTo(2.0 - 1.8, 5);
  });
});

describe('surfaceYAt', () => {
  const container: AABB = boxCenter(5, 1.3, -2, 2.4, 2.6, 6); // 顶面 2.6, footprint x∈[3.8,6.2] z∈[-5,1]
  const crate: AABB = boxCenter(0, 0.6, 0, 1.2, 1.2, 1.2); // 顶面 1.2

  it('returns 0 on open ground', () => {
    expect(surfaceYAt([container], 20, 20)).toBe(0);
  });

  it('returns cover top inside footprint (inclusive edges)', () => {
    expect(surfaceYAt([container], 5, -5)).toBeCloseTo(2.6, 5);
    expect(surfaceYAt([container], 3.8, -2)).toBeCloseTo(2.6, 5);
  });

  it('returns highest top among overlapping covers', () => {
    expect(surfaceYAt([crate, container], 5, -2)).toBeCloseTo(2.6, 5);
    expect(surfaceYAt([crate, boxCenter(5, 1.8, -2, 1.2, 1.2, 1.2)], 5, -2)).toBeCloseTo(2.4, 5);
  });

  it('returns 0 for empty obstacle list', () => {
    expect(surfaceYAt([], 0, 0)).toBe(0);
  });
});
