import { describe, expect, it } from 'vitest';
import { isDrafting, nearestAheadIndex } from './draft';
import type { RiderState, RiderType } from './types';

const T: RiderType = { label: 'test', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 };

function rider(id: number, dist: number, lateral = 0, speed = 8): RiderState {
  return { id, name: `r${id}`, isPlayer: false, type: T, dist, lateral, speed, energy: 24000, gear: 1, power: 300, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0 };
}

describe('drafting', () => {
  it('true when 1-15m ahead and laterally aligned', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 110)])).toBe(true);
  });
  it('false when ahead is beyond 15m', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 120)])).toBe(false);
  });
  it('false when lateral offset too large', () => {
    expect(isDrafting(0, [rider(0, 100, 0), rider(1, 110, 2)])).toBe(false);
  });
  it('false for rider behind', () => {
    expect(isDrafting(0, [rider(0, 100), rider(1, 90)])).toBe(false);
  });
  it('nearestAheadIndex picks closest ahead within 40m', () => {
    const rs = [rider(0, 100), rider(1, 130), rider(2, 108)];
    expect(nearestAheadIndex(rs[0], rs)).toBe(2);
  });
  it('nearestAheadIndex returns -1 when none ahead', () => {
    const rs = [rider(0, 100), rider(1, 90)];
    expect(nearestAheadIndex(rs[0], rs)).toBe(-1);
  });
});
