import { RACE } from './params';
import type { GearId } from './types';

export function drainRate(gear: GearId): number {
  const r = RACE.drainRatios[gear];
  return r * r * RACE.drainBase;
}

export function targetPower(gear: GearId, ftp: number, energy: number): number {
  const raw = ftp * RACE.gearRatios[gear];
  return energy <= 0 ? Math.min(raw, ftp * RACE.emptyCapRatio) : raw;
}

export function stepEnergy(energy: number, gear: GearId, maxEnergy: number, dt: number): number {
  let e = energy - drainRate(gear) * dt;
  if (gear <= 1) e += RACE.regenRate * maxEnergy * dt;
  return Math.min(maxEnergy, Math.max(0, e));
}
