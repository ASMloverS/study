import { describe, expect, it } from 'vitest';
import { createRace, standings, stepRace } from './race';
import { buildTrack } from './trackData';
import { RACE } from './params';
import type { RiderCommand } from './types';

const track = buildTrack();
const cruise: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0 };

function run(cmd: RiderCommand, seconds: number) {
  let s = createRace(track);
  for (let i = 0; i < seconds / RACE.dt; i++) s = stepRace(s, track, cmd, RACE.dt);
  return s;
}

describe('race', () => {
  it('countdown counts down to racing', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.phase).toBe('racing');
    expect(s.time).toBe(0);
  });
  it('riders advance after start', () => {
    const s = run(cruise, 10);
    expect(s.riders.every((r) => r.dist > 10)).toBe(true);
  });
  it('records finish time when crossing line', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: track.length - 0.1, speed: 20 })) };
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders.every((r) => r.finishTime !== null)).toBe(true);
    expect(s.riders.every((r) => r.dist === track.length)).toBe(true);
  });
  it('deterministic for identical inputs', () => {
    expect(JSON.stringify(run(cruise, 120))).toBe(JSON.stringify(run(cruise, 120)));
  });
  it('standings orders by distance during race', () => {
    const st = standings(run(cruise, 60));
    for (let i = 1; i < st.length; i++) {
      expect(st[i - 1].dist).toBeGreaterThanOrEqual(st[i].dist);
    }
  });
  it('full race completes with all riders finishing and sane avg power', () => {
    let s = createRace(track);
    let steps = 0;
    while (s.phase !== 'finished' && steps < 60 * 1500) {
      s = stepRace(s, track, cruise, RACE.dt);
      steps++;
    }
    expect(s.phase).toBe('finished');
    expect(s.riders.every((r) => r.finishTime !== null)).toBe(true);
    for (const row of s.results) {
      expect(row.avgPower).toBeGreaterThan(150);
      expect(row.avgPower).toBeLessThan(400);
    }
  }, 30000);
  it('reckless sprinting still finishes comfortably via feeds', () => {
    const s = run({ gear: 3, steer: 0, cadDelta: 0, cogDelta: 0 }, 900);
    const p = s.riders[0];
    expect(p.finishTime).not.toBeNull();
    expect(p.finishTime!).toBeLessThan(500);
  }, 30000);
  it('feed zone restores energy while passing through', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 565, speed: 11, energy: 10000 })) };
    const before = s.riders[0].energy;
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBeGreaterThan(before + 2000);
  });
  it('energy caps at max inside feed zone', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 565, speed: 11, energy: 39900 })) };
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBe(40000);
  });
  it('cog shifts in continuous teeth via cogDelta and clamps to 10..36', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const half: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0.5 };
    s = stepRace(s, track, half, RACE.dt);
    expect(s.riders[0].cog).toBeCloseTo(16.5, 6);
    const heavy: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: -100 };
    s = stepRace(s, track, heavy, RACE.dt);
    expect(s.riders[0].cog).toBe(10);
    const light: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 100 };
    s = stepRace(s, track, light, RACE.dt);
    expect(s.riders[0].cog).toBe(36);
  });
  it('soft-pedal above cadence target: no power, no drain, decaying speed', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 200, speed: 12, cadTarget: 80 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].power).toBe(0);
    expect(s.riders[0].energy).toBe(before);
    expect(s.riders[0].speed).toBeLessThan(12);
  });
  it('cadence target adjusts via cadDelta and clamps to 80..150', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const up: RiderCommand = { gear: 1, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, up, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(150);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    for (let i = 0; i < 40; i++) s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(80);
  });
  it('sprint gear unlocks cadence target above 150 and keeps it after exit', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const sprintUp: RiderCommand = { gear: 3, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, sprintUp, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(205);
  });
  it('sprint gear keeps full efficiency at extreme cadence', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 200, speed: 20, cadTarget: 250 })) };
    s = stepRace(s, track, { gear: 3, steer: 0, cadDelta: 0, cogDelta: 0 }, RACE.dt);
    expect(s.riders[0].power).toBeCloseTo(750, 5);
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].power).toBeCloseTo(165, 5);
  });
});
