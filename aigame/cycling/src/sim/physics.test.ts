import { describe, expect, it } from 'vitest';
import { stepSpeed } from './physics';
import { RACE } from './params';

const dt = RACE.dt;

function steadyState(power: number, gradient: number, drafting: boolean): number {
  let v = 0;
  for (let i = 0; i < 60 * 180; i++) v = stepSpeed(v, power, gradient, drafting, dt);
  return v;
}

describe('stepSpeed', () => {
  it('300W flat settles near 39 km/h', () => {
    const v = steadyState(300, 0, false);
    expect(v).toBeGreaterThan(10.3);
    expect(v).toBeLessThan(11.5);
  });
  it('drafting raises steady speed by >1 m/s at 300W', () => {
    expect(steadyState(300, 0, true) - steadyState(300, 0, false)).toBeGreaterThan(1);
  });
  it('6% climb at 300W settles between 4.8 and 6.0 m/s', () => {
    const v = steadyState(300, 0.06, false);
    expect(v).toBeGreaterThan(4.8);
    expect(v).toBeLessThan(6.0);
  });
  it('coasting decays to zero and never negative', () => {
    let v = 10;
    for (let i = 0; i < 60 * 180; i++) {
      v = stepSpeed(v, 0, 0, false, dt);
      expect(v).toBeGreaterThanOrEqual(0);
    }
    expect(v).toBe(0);
  });
  it('downhill rolls faster than flat at same power', () => {
    expect(steadyState(300, -0.04, false)).toBeGreaterThan(steadyState(300, 0, false));
  });
  it('single-step behavior: accelerates from rest, decelerates without power', () => {
    expect(stepSpeed(0, 300, 0, false, dt)).toBeGreaterThan(0);
    expect(stepSpeed(10, 0, 0, false, dt)).toBeLessThan(10);
  });
});
