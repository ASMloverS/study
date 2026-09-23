import { describe, expect, it } from 'vitest';
import { boxCenter, moveBodyAxis, type Body } from '../src';

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
