import { DRIVETRAIN as D } from './params';

export function gearRatio(cog: number): number {
  return D.chainring / D.cassette[cog];
}

export function cadence(speed: number, cog: number): number {
  return (speed / (gearRatio(cog) * D.wheelCirc)) * 60;
}

export function cadenceEfficiency(cad: number): number {
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
  let best = currentCog;
  let bestErr = Math.abs(cadence(speed, currentCog) - target);
  for (let c = 0; c < D.cassette.length; c++) {
    const err = Math.abs(cadence(speed, c) - target);
    if (err < bestErr - D.aiShiftHysteresis) {
      best = c;
      bestErr = err;
    }
  }
  return best;
}
