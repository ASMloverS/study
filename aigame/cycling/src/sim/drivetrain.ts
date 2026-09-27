import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / cog;
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function cadenceEfficiency(cad: number, sprint = false): number {
  if (sprint) return 1;
  if (cad >= D.cadFullLo && cad <= D.cadFullHi) return 1;
  if (cad <= D.cadFloor || cad >= D.cadCeil) return D.effMin;
  if (cad < D.cadFullLo) {
    return D.effMin + ((cad - D.cadFloor) / (D.cadFullLo - D.cadFloor)) * (1 - D.effMin);
  }
  return D.effMin + ((D.cadCeil - cad) / (D.cadCeil - D.cadFullHi)) * (1 - D.effMin);
}

export function terrainCadence(gradient: number): number {
  if (gradient > D.climbGradient) return D.climbCadence;
  if (gradient < D.descentGradient) return D.descentCadence;
  return D.flatCadence;
}

export function aiShift(speed: number, currentCog: number, target: number = D.aiTargetCadence): number {
  if (speed < D.aiShiftMinSpeed) return currentCog;
  const ideal = Math.min(D.cogMax, Math.max(D.cogMin, (target * D.chainring * D.wheelCirc) / (speed * 60)));
  const err = (cog: number) => Math.abs(cadence(speed, cog) - target);
  return err(ideal) < err(currentCog) - D.aiShiftHysteresis ? ideal : currentCog;
}
