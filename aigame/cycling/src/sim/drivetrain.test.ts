import { describe, expect, it } from 'vitest';
import { aiShift, cadence, cadenceEfficiency, gearRatio, terrainCadence } from './drivetrain';

describe('gearRatio', () => {
  it('maps cog index to 52T ratios', () => {
    expect(gearRatio(0)).toBeCloseTo(52 / 36, 6);
    expect(gearRatio(6)).toBe(3.25);
    expect(gearRatio(11)).toBeCloseTo(52 / 10, 6);
  });
});

describe('cadence', () => {
  it('cruise 10.95 m/s on 52x16 is ~96 rpm', () => {
    expect(cadence(10.95, 6)).toBeCloseTo(96.4, 1);
  });
  it('climb 5.4 m/s on 52x28 is ~83 rpm', () => {
    expect(cadence(5.4, 2)).toBeCloseTo(83.2, 1);
  });
  it('top gear at 20 m/s is ~110 rpm', () => {
    expect(cadence(20, 11)).toBeCloseTo(110.1, 1);
  });
  it('zero speed is zero cadence', () => {
    expect(cadence(0, 6)).toBe(0);
  });
});

describe('cadenceEfficiency', () => {
  it('full power inside [80,125] rpm', () => {
    expect(cadenceEfficiency(80)).toBe(1);
    expect(cadenceEfficiency(100)).toBe(1);
    expect(cadenceEfficiency(120)).toBe(1);
    expect(cadenceEfficiency(125)).toBe(1);
  });
  it('linear falloff to 0.55 between 50-80 and 125-150', () => {
    expect(cadenceEfficiency(65)).toBeCloseTo(0.775, 6);
    expect(cadenceEfficiency(70)).toBeCloseTo(0.85, 6);
    expect(cadenceEfficiency(140)).toBeCloseTo(0.73, 6);
  });
  it('clamps at 0.55 beyond the range', () => {
    expect(cadenceEfficiency(50)).toBe(0.55);
    expect(cadenceEfficiency(30)).toBe(0.55);
    expect(cadenceEfficiency(150)).toBe(0.55);
    expect(cadenceEfficiency(170)).toBe(0.55);
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
  it('keeps cog near 95 rpm at cruise', () => {
    expect(aiShift(10.95, 6)).toBe(6);
  });
  it('shifts to 32T on climbs', () => {
    expect(aiShift(5.4, 6)).toBe(1);
  });
  it('shifts to 10T at speed', () => {
    expect(aiShift(20, 6)).toBe(11);
  });
  it('keeps current cog at standstill tie', () => {
    expect(aiShift(0, 6)).toBe(6);
  });
  it('honors custom target 110 on flat', () => {
    expect(aiShift(10.95, 6, 110)).toBe(5);
  });
  it('honors custom target 90 on climb', () => {
    expect(aiShift(5.4, 6, 90)).toBe(1);
  });
});
