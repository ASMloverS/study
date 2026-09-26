import { describe, expect, it } from 'vitest';
import { planFrame } from './loop';

describe('planFrame', () => {
  it('one 60fps frame yields one step', () => {
    const p = planFrame(0, 1 / 60);
    expect(p.steps).toBe(1);
    expect(p.alpha).toBeCloseTo(0, 6);
  });
  it('30fps frame yields two steps', () => {
    expect(planFrame(0, 1 / 30).steps).toBe(2);
  });
  it('accumulates remainder', () => {
    const a = planFrame(0, 1 / 120);
    expect(a.steps).toBe(0);
    expect(planFrame(a.acc, 1 / 120).steps).toBe(1);
  });
  it('clamps huge frame delta to 0.25s', () => {
    const p = planFrame(0, 5);
    expect(p.steps).toBeGreaterThanOrEqual(14);
    expect(p.steps).toBeLessThanOrEqual(15);
  });
});
