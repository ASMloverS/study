import { describe, expect, it } from 'vitest';
import { WEAPONS, falloffMul } from '../src';

describe('falloffMul', () => {
  it('AR keeps full damage inside 15m and floors at 70% beyond 45m', () => {
    const ar = WEAPONS.ar;
    expect(falloffMul(ar, 5)).toBe(1);
    expect(falloffMul(ar, 15)).toBe(1);
    expect(falloffMul(ar, 30)).toBeCloseTo(0.85, 5);
    expect(falloffMul(ar, 45)).toBeCloseTo(0.7, 5);
    expect(falloffMul(ar, 100)).toBe(0.7);
  });

  it('SG floors at 50% beyond 20m; SR never falls off', () => {
    expect(falloffMul(WEAPONS.sg, 8)).toBe(1);
    expect(falloffMul(WEAPONS.sg, 20)).toBeCloseTo(0.5, 5);
    expect(falloffMul(WEAPONS.sg, 100)).toBe(0.5);
    expect(falloffMul(WEAPONS.sr, 100)).toBe(1);
  });
});

describe('weapon defs', () => {
  it('SR is semi-auto bolt-action', () => {
    expect(WEAPONS.sr.auto).toBe(false);
  });

  it('switch times differ per weapon', () => {
    expect(WEAPONS.ar.switchTime).toBeLessThan(WEAPONS.sg.switchTime);
    expect(WEAPONS.sg.switchTime).toBeLessThan(WEAPONS.sr.switchTime);
  });
});
