import { describe, expect, it } from 'vitest';
import { mulberry32, rngNext } from './rng';

describe('rng', () => {
  it('same seed → same sequence', () => {
    const a = mulberry32(42);
    const b = mulberry32(42);
    for (let i = 0; i < 100; i++) expect(a()).toBe(b());
  });
  it('values in [0,1)', () => {
    const r = mulberry32(7);
    for (let i = 0; i < 1000; i++) {
      const v = r();
      expect(v).toBeGreaterThanOrEqual(0);
      expect(v).toBeLessThan(1);
    }
  });
  it('rngNext returns value and advanced state', () => {
    const [v1, s1] = rngNext(100);
    const [v2] = rngNext(s1);
    expect(v1).not.toBe(v2);
  });
  it('golden values and wrapper chaining pin algorithm fidelity', () => {
    const g42 = mulberry32(42);
    expect(g42()).toBeCloseTo(0.6011037519201636, 15);
    expect(g42()).toBeCloseTo(0.44829055899754167, 15);
    const g0 = mulberry32(0);
    expect(g0()).toBeCloseTo(0.26642920868471265, 15);
    const w = mulberry32(7);
    const [direct] = rngNext(7);
    expect(w()).toBe(direct);
  });
});
