import { DRIVETRAIN, RACE } from './params';
import { rngNext } from './rng';
import { stepEnergy, targetPower } from './energy';
import { stepSpeed } from './physics';
import { isDrafting, nearestAheadIndex } from './draft';
import { aiCommand } from './ai';
import type { Track } from './track';
import type { Phase, ResultRow, RiderCommand, RiderState, RiderType } from './types';

export interface RaceState {
  phase: Phase;
  time: number;
  countdown: number;
  riders: RiderState[];
  trackLength: number;
  rngState: number;
  results: ResultRow[];
}

export const PLAYER_TYPE: RiderType = { label: 'all-rounder', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 };

export const AI_FIELD: RiderType[] = [
  { label: 'climber', ftp: 315, maxEnergy: 22000, sprintDist: 120, aggression: 0.85 },
  { label: 'climber', ftp: 306, maxEnergy: 22500, sprintDist: 130, aggression: 0.7 },
  { label: 'sprinter', ftp: 286, maxEnergy: 26000, sprintDist: 350, aggression: 0.4 },
  { label: 'sprinter', ftp: 293, maxEnergy: 25500, sprintDist: 300, aggression: 0.5 },
  { label: 'rouleur', ftp: 298, maxEnergy: 24000, sprintDist: 200, aggression: 0.55 },
  { label: 'rouleur', ftp: 295, maxEnergy: 24500, sprintDist: 220, aggression: 0.6 },
  { label: 'all-rounder', ftp: 300, maxEnergy: 23500, sprintDist: 240, aggression: 0.5 },
];

const NAMES = ['你', '山神', '穿山甲', '火箭', '冲刺王', '发动机', '老将', '全能手'];

function makeRider(id: number, type: RiderType, isPlayer: boolean, dist: number, lateral: number): RiderState {
  return {
    id, name: NAMES[id], isPlayer, type,
    dist, lateral, speed: 0, energy: type.maxEnergy, gear: 1, cog: DRIVETRAIN.defaultCog,
    power: 0, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0,
  };
}

export function createRace(track: Track): RaceState {
  const riders: RiderState[] = [makeRider(0, PLAYER_TYPE, true, 0, 0.8)];
  for (let i = 0; i < AI_FIELD.length; i++) {
    riders.push(makeRider(i + 1, AI_FIELD[i], false, (i + 1) * 1.6, (i % 2 === 0 ? -0.8 : 0.8)));
  }
  return { phase: 'countdown', time: 0, countdown: RACE.countdown, riders, trackLength: track.length, rngState: 20260926, results: [] };
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}

export function buildResults(riders: readonly RiderState[]): ResultRow[] {
  return [...riders]
    .sort((a, b) => (a.finishTime ?? 1e9) - (b.finishTime ?? 1e9) || b.dist - a.dist)
    .map((r) => ({
      id: r.id,
      name: r.name,
      time: r.finishTime ?? r.dist,
      avgPower: r.timeSum > 0 ? r.powerSum / r.timeSum : 0,
    }));
}

export function standings(s: RaceState): RiderState[] {
  return [...s.riders].sort((a, b) => (a.finishTime ?? 1e9) - (b.finishTime ?? 1e9) || b.dist - a.dist);
}

export function stepRace(s: RaceState, track: Track, playerCmd: RiderCommand, dt: number): RaceState {
  if (s.phase === 'finished') return s;
  if (s.phase === 'countdown') {
    const countdown = s.countdown - dt;
    return countdown <= 0 ? { ...s, phase: 'racing', countdown: 0 } : { ...s, countdown };
  }
  let rngState = s.rngState;
  const rng = () => {
    const [v, next] = rngNext(rngState);
    rngState = next;
    return v;
  };
  const riders = s.riders.map((r) => ({ ...r }));
  for (const r of riders) {
    if (!r.isPlayer && rng() < 0.02) r.wanderTarget = (rng() * 2 - 1) * 0.6;
  }
  const gradients = riders.map((r) => track.sampleAt(Math.min(r.dist, s.trackLength - 0.01)).gradient);
  const cmds = riders.map<RiderCommand>((r, i) => {
    if (r.finishTime !== null) return { gear: 0, steer: 0, cog: r.cog };
    if (r.isPlayer) return playerCmd;
    const a = nearestAheadIndex(r, riders);
    return { ...aiCommand(r, a >= 0 ? riders[a] : null, Math.max(0, s.trackLength - r.dist), gradients[i]), cog: r.cog };
  });
  const drafts = riders.map((_, i) => isDrafting(i, riders));
  for (let i = 0; i < riders.length; i++) {
    const r = riders[i];
    const cmd = cmds[i];
    r.gear = cmd.gear;
    r.cog = cmd.cog;
    r.power = r.finishTime !== null ? 0 : targetPower(cmd.gear, r.type.ftp, r.energy);
    r.speed = r.finishTime !== null ? Math.max(0, r.speed - 2 * dt) : stepSpeed(r.speed, r.power, gradients[i], drafts[i], dt);
    if (r.finishTime === null) {
      r.dist += r.speed * dt;
      r.lateral = clamp(r.lateral + cmd.steer * RACE.lateralSpeed * dt, -RACE.lateralMax, RACE.lateralMax);
      r.energy = stepEnergy(r.energy, r.power, r.type.ftp, dt);
      r.powerSum += r.power * dt;
      r.timeSum += dt;
      if (r.dist >= s.trackLength) {
        r.finishTime = s.time + dt - (r.dist - s.trackLength) / Math.max(r.speed, 0.1);
        r.dist = s.trackLength;
      }
    }
  }
  const next: RaceState = { ...s, time: s.time + dt, riders, rngState };
  if (riders.every((r) => r.finishTime !== null)
    || (riders[0].finishTime !== null && s.time > riders[0].finishTime + RACE.finishWait)) {
    next.results = buildResults(riders);
    next.phase = 'finished';
  }
  return next;
}
