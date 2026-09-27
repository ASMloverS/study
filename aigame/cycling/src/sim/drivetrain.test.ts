import { describe, expect, it } from 'vitest';
import { aiShift, cadence, gearRatio, terrainCadence } from './drivetrain';

describe('gearRatio', () => {
  it('maps cog teeth to 52T ratios', () => {
    expect(gearRatio(36)).toBeCloseTo(52 / 36, 6);
    expect(gearRatio(16)).toBeCloseTo(3.25, 6);
    expect(gearRatio(10)).toBeCloseTo(5.2, 6);
  });
});

describe('cadence', () => {
  it('cruise 10.95 m/s on 52x16 is ~96 rpm', () => {
    expect(cadence(10.95, 16)).toBeCloseTo(96.4, 1);
  });
  it('climb 5.4 m/s on 52x28 is ~83 rpm', () => {
    expect(cadence(5.4, 28)).toBeCloseTo(83.2, 1);
  });
  it('top gear at 20 m/s is ~110 rpm', () => {
    expect(cadence(20, 10)).toBeCloseTo(110.1, 1);
  });
  it('zero speed is zero cadence', () => {
    expect(cadence(0, 16)).toBe(0);
  });
});

describe('terrainCadence', () => {
  it('climb above +2%, descent below -2%, flat otherwise', () => {
    expect(terrainCadence(0.025)).toBe(90);
    expect(terrainCadence(0.02)).toBe(110);
    expect(terrainCadence(0)).toBe(110);
    expect(terrainCadence(-0.02)).toBe(110);
    expect(terrainCadence(-0.025)).toBe(120);
  });
});

describe('aiShift', () => {
  it('keeps cog near 95 rpm at cruise via hysteresis', () => {
    expect(aiShift(10.95, 16)).toBe(16);
  });
  it('shifts analytically to ~32T on climbs', () => {
    expect(aiShift(5.4, 16)).toBeCloseTo(31.96, 1);
  });
  it('clamps to 10T at speed', () => {
    expect(aiShift(20, 16)).toBe(10);
  });
  it('keeps current cog at standstill', () => {
    expect(aiShift(0, 16)).toBe(16);
  });
  it('keeps current cog below min speed threshold', () => {
    expect(aiShift(0.3, 16)).toBe(16);
  });
  it('honors custom target 110 on flat', () => {
    expect(aiShift(10.95, 16, 110)).toBeCloseTo(18.25, 1);
  });
  it('honors custom target 90 on climb', () => {
    expect(aiShift(5.4, 16, 90)).toBeCloseTo(30.28, 1);
  });
});
