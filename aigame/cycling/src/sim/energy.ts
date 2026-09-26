import { RACE } from './params';
import type { GearId } from './types';

export function drainRate(power: number, ftp: number): number {
  const r = power / ftp;
  return r * r * RACE.drainBase;
}

export function targetPower(gear: GearId, ftp: number, energy: number): number {
  const raw = ftp * RACE.gearRatios[gear];
  return energy <= 0 ? Math.min(raw, ftp * RACE.emptyCapRatio) : raw;
}

export function stepEnergy(energy: number, power: number, ftp: number, dt: number): number {
  return Math.max(0, energy - drainRate(power, ftp) * dt);
}
