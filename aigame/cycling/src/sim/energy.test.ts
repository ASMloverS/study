import { describe, expect, it } from 'vitest';
import { drainRate, stepEnergy, targetPower } from './energy';
import { RACE } from './params';

describe('energy', () => {
  it('drain scales with squared relative power', () => {
    expect(drainRate(300, 300)).toBe(30);
    expect(drainRate(750, 300)).toBe(187.5);
    expect(drainRate(180, 300)).toBeCloseTo(10.8, 6);
  });
  it('stepEnergy floors at zero', () => {
    expect(stepEnergy(1, 750, 300, 1)).toBe(0);
    expect(stepEnergy(24000, 300, 300, RACE.dt)).toBeCloseTo(24000 - 30 * RACE.dt, 6);
  });
  it('full tank targets gear power', () => {
    expect(targetPower(3, 300, 1000)).toBe(750);
  });
  it('empty tank caps power at 70% ftp', () => {
    expect(targetPower(3, 300, 0)).toBe(210);
    expect(targetPower(1, 300, 0)).toBe(210);
    expect(targetPower(0, 300, 0)).toBe(180);
  });
  it('cruise drains full tank in ~800s', () => {
    let e = 24000;
    let t = 0;
    while (e > 0) {
      e = stepEnergy(e, 300, 300, 1);
      t++;
    }
    expect(t).toBeGreaterThan(700);
    expect(t).toBeLessThan(900);
  });
});
