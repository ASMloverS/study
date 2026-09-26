import { describe, expect, it } from 'vitest';
import { createRace, standings, stepRace } from './race';
import { buildTrack } from './trackData';
import { RACE } from './params';
import { aiShift } from './drivetrain';
import type { RiderCommand } from './types';

const track = buildTrack();
const cruise: RiderCommand = { gear: 1, steer: 0, cog: 6 };

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
  it('reckless sprinting loses to steady pacing', () => {
    const paced: RiderCommand = { gear: 1, steer: 0, cog: 6 };
    let s = createRace(track);
    let guard = 0;
    while (s.phase === 'countdown' && guard++ < 60 * 10) s = stepRace(s, track, paced, RACE.dt);
    let lastShift = 0;
    while (s.phase !== 'finished') {
      if (Math.floor(s.time / 0.5) !== lastShift) {
        lastShift = Math.floor(s.time / 0.5);
        paced.cog = aiShift(s.riders[0].speed, paced.cog);
      }
      s = stepRace(s, track, paced, RACE.dt);
    }
    const steady = s.riders[0].finishTime!;
    const reckless = run({ gear: 3, steer: 0, cog: 6 }, 900).riders[0].finishTime!;
    expect(reckless).toBeGreaterThan(steady);
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
    s = { ...s, riders: s.riders.map((r) => ({ ...r, dist: 565, speed: 11, energy: 31900 })) };
    for (let i = 0; i < 120; i++) s = stepRace(s, track, cruise, RACE.dt);
    expect(s.riders[0].energy).toBe(32000);
  });
  it('heavy cog over the full lap loses to default cog', () => {
    const heavy = run({ gear: 1, steer: 0, cog: 8 }, 900).riders[0].finishTime!;
    const base = run({ gear: 1, steer: 0, cog: 6 }, 900).riders[0].finishTime!;
    expect(heavy).toBeGreaterThan(base);
  }, 30000);
});
