import { describe, expect, it } from 'vitest';
import { aiShift, cadence, cadenceEfficiency, gearRatio } from './drivetrain';

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
  it('full power inside [60,115] rpm', () => {
    expect(cadenceEfficiency(60)).toBe(1);
    expect(cadenceEfficiency(70)).toBe(1);
    expect(cadenceEfficiency(100)).toBe(1);
    expect(cadenceEfficiency(115)).toBe(1);
  });
  it('linear falloff to 0.55 between 40-60 and 115-140', () => {
    expect(cadenceEfficiency(50)).toBeCloseTo(0.775, 6);
    expect(cadenceEfficiency(120)).toBeCloseTo(0.91, 6);
    expect(cadenceEfficiency(125)).toBeCloseTo(0.82, 6);
  });
  it('clamps at 0.55 beyond the range', () => {
    expect(cadenceEfficiency(40)).toBe(0.55);
    expect(cadenceEfficiency(20)).toBe(0.55);
    expect(cadenceEfficiency(140)).toBe(0.55);
    expect(cadenceEfficiency(160)).toBe(0.55);
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
});
