import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';

describe('stepSpeed', () => {
  it('caps drive force at maxDriveForce at low speed', () => {
    const v = stepSpeed(1, 750, 0, false, 1 / 60);
    expect(v).toBeCloseTo(1 + (450 - 3.826 - 0.0858) / 78 / 60, 4);
  });
  it('converges to ~13.6 m/s at 300W on flat', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 300, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(13);
    expect(v).toBeLessThan(15);
  });
  it('converges to ~18.6 m/s at 750W on flat', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 750, 0, false, 1 / 60);
    expect(v).toBeGreaterThan(18);
    expect(v).toBeLessThan(20);
  });
  it('reaches ~35.8 m/s on -12% descent with sprint power', () => {
    let v = 8;
    for (let i = 0; i < 40000; i++) v = stepSpeed(v, 750, -0.12, false, 1 / 60);
    expect(v).toBeGreaterThan(33);
    expect(v).toBeLessThan(39);
  });
  it('never returns negative', () => {
    expect(stepSpeed(0.5, 0, 0.08, false, 1 / 60)).toBeGreaterThanOrEqual(0);
  });
});
