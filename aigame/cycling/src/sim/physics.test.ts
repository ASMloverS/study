import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';

describe('stepSpeed', () => {
  it('caps drive force at maxDriveForce at low speed', () => {
    const v = stepSpeed(1, 4500, 0, false, 1 / 60);
    expect(v).toBeCloseTo(1 + (450 - 3.826 - 0.001226) / 78 / 60, 4);
  });
  it('converges to ~104 m/s at 1800W on flat', () => {
    let v = 50;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 1800, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(100);
    expect(v).toBeLessThan(107);
  });
  it('converges to ~147 m/s at 4500W on flat', () => {
    let v = 50;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 4500, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(143);
    expect(v).toBeLessThan(150);
  });
  it('reaches ~200 m/s on -5% descent with sprint power', () => {
    let v = 50;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 4500, -0.05, false, 1 / 60);
    expect(v).toBeGreaterThan(195);
    expect(v).toBeLessThan(235);
  });
  it('never returns negative', () => {
    expect(stepSpeed(0.5, 0, 0.08, false, 1 / 60)).toBeGreaterThanOrEqual(0);
  });
});
