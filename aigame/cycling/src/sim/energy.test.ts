import { describe, expect, it } from 'vitest';
import { drainRate, stepEnergy } from './energy';

describe('drainRate', () => {
  it('drains by gear ratio squared times base', () => {
    expect(drainRate(0)).toBeCloseTo(0.36 * 12, 6);
    expect(drainRate(1)).toBeCloseTo(12, 6);
    expect(drainRate(2)).toBeCloseTo(27, 6);
    expect(drainRate(3)).toBeCloseTo(75, 6);
  });
});

describe('stepEnergy', () => {
  it('drains sprint at 75 J/s', () => {
    expect(stepEnergy(40000, 3, 40000, 1)).toBeCloseTo(40000 - 75, 6);
  });
  it('regens while cruising at low gear', () => {
    expect(stepEnergy(30000, 1, 40000, 1)).toBeCloseTo(30000 - 12 + 60, 6);
  });
  it('caps at max energy', () => {
    expect(stepEnergy(39995, 0, 40000, 1)).toBe(40000);
  });
  it('never goes below zero', () => {
    expect(stepEnergy(10, 3, 40000, 1)).toBe(0);
  });
});
