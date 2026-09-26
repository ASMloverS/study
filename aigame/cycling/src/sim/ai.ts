import type { GearId, RiderState } from './types';

export function aiCommand(
  self: RiderState,
  ahead: RiderState | null,
  remaining: number,
  gradient: number,
): { gear: GearId; steer: number } {
  const e = self.energy / self.type.maxEnergy;
  let target = self.wanderTarget;
  if (ahead && ahead.dist - self.dist < 25) target = ahead.lateral * 0.8;
  let steer = 0;
  if (Math.abs(target - self.lateral) > 0.15) steer = Math.sign(target - self.lateral);
  if (e <= 0.02) return { gear: 0, steer: 0 };
  if (remaining <= self.type.sprintDist && e > 0.12) return { gear: 3, steer: 0 };
  if (ahead && ahead.dist - self.dist < 25 && ahead.speed > self.speed + 0.8) return { gear: 2, steer };
  if (gradient > 0.045 && e > 0.5 && self.type.aggression > 0.6) return { gear: 2, steer: 0 };
  return { gear: e < 0.25 ? 0 : 1, steer };
}
