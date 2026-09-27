import { describe, expect, it } from 'vitest';
import { AI_FIELD, createRace, standings, stepRace } from './race';
import { buildTrack } from './trackData';
import { RACE } from './params';
import type { RiderCommand } from './types';

const track = buildTrack();
const cruise: RiderCommand = { gear: 0, steer: 0, cadDelta: 0, cogDelta: -15 };

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
    while (s.phase !== 'finished' && steps < 60 * 2500) {
      s = stepRace(s, track, cruise, RACE.dt);
      steps++;
    }
    expect(s.phase).toBe('finished');
    expect(s.riders.every((r) => r.finishTime !== null)).toBe(true);
    for (const row of s.results) {
      expect(row.avgPower).toBeGreaterThan(100);
      expect(row.avgPower).toBeLessThan(2500);
    }
  }, 30000);
  it('reckless sprinting finishes the stage within contract', () => {
    const s = run({ gear: 3, steer: 0, cadDelta: 0, cogDelta: -15 }, 1700);
    const p = s.riders[0];
    expect(p.finishTime).not.toBeNull();
    expect(p.finishTime!).toBeLessThan(1700);
  }, 30000);
  it('item box picked up when crossing at matching lateral', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 1499.5, speed: 100, lateral: 0, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].collected[1]).toBe(true);
    expect(s.riders[0].energy).toBeCloseTo(before + 4000, -1);
  });
  it('no pickup when lateral misses all boxes', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 1499.5, speed: 100, lateral: 0.85, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].collected.every((c) => !c)).toBe(true);
    expect(s.riders[0].energy).toBe(before);
  });
  it('each box is evaluated exactly once per rider', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 1499.9, speed: 2, lateral: 0, cadTarget: 0, energy: 20000 })) };
    const before = s.riders[0].energy;
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBeCloseTo(before + 4000, -1);
  });
  it('ai riders also pick up boxes', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r, i) => (i === 4 ? { ...r, dist: 1499.5, speed: 100, lateral: 0, energy: 10000 } : r)) };
    s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[4].collected.some((c) => c)).toBe(true);
    expect(s.riders[4].energy).toBeCloseTo(10000 + 0.1 * s.riders[4].type.maxEnergy, -1);
  });
  it('scales sprint distance to stage length', () => {
    const s = createRace(track);
    const scale = track.length / 150000;
    expect(s.riders[1].type.sprintDist).toBeCloseTo(AI_FIELD[0].sprintDist * scale, 0);
  });
  it('cog shifts freely with floor 1 and no upper clamp', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const half: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0.5 };
    s = stepRace(s, track, half, RACE.dt);
    expect(s.riders[0].cog).toBeCloseTo(16.5, 6);
    const heavy: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: -100 };
    s = stepRace(s, track, heavy, RACE.dt);
    expect(s.riders[0].cog).toBe(1);
    const light: RiderCommand = { gear: 1, steer: 0, cadDelta: 0, cogDelta: 500 };
    s = stepRace(s, track, light, RACE.dt);
    expect(s.riders[0].cog).toBe(501);
  });
  it('soft-pedal above cadence target: no power, no drain, decaying speed', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 200, speed: 12, cadTarget: 80, cog: 16 })) };
    const before = s.riders[0].energy;
    s = stepRace(s, track, { gear: 1, steer: 0, cadDelta: 0, cogDelta: 0 }, RACE.dt);
    expect(s.riders[0].power).toBe(0);
    expect(s.riders[0].energy).toBe(before);
    expect(s.riders[0].speed).toBeLessThan(12);
  });
  it('cadence target adjusts freely with floor 0 and no upper clamp', () => {
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, cruise, RACE.dt);
    const up: RiderCommand = { gear: 1, steer: 0, cadDelta: 5, cogDelta: 0 };
    for (let i = 0; i < 20; i++) s = stepRace(s, track, up, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(210);
    const down: RiderCommand = { gear: 1, steer: 0, cadDelta: -5, cogDelta: 0 };
    for (let i = 0; i < 100; i++) s = stepRace(s, track, down, RACE.dt);
    expect(s.riders[0].cadTarget).toBe(0);
  });
});
