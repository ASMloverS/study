import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / cog;
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function terrainCadence(gradient: number): number {
  if (gradient > D.climbGradient) return D.climbCadence;
  if (gradient < D.descentGradient) return D.descentCadence;
  return D.flatCadence;
}

export function aiShift(speed: number, currentCog: number, target: number = D.aiTargetCadence): number {
  if (speed < D.aiShiftMinSpeed) return currentCog;
  const ideal = Math.min(D.aiCogMax, Math.max(D.aiCogMin, (target * D.chainring * D.wheelCirc) / (speed * 60)));
  const err = (cog: number) => Math.abs(cadence(speed, cog) - target);
  return err(ideal) < err(currentCog) - D.aiShiftHysteresis ? ideal : currentCog;
}
