import { RACE } from './params';
import type { RiderState } from './types';

export function nearestAheadIndex(self: RiderState, riders: readonly RiderState[]): number {
  let best = -1;
  let bestGap = 40;
  for (let i = 0; i < riders.length; i++) {
    const gap = riders[i].dist - self.dist;
    if (i !== self.id && gap > 0 && gap < bestGap) {
      bestGap = gap;
      best = i;
    }
  }
  return best;
}

export function isDrafting(selfIndex: number, riders: readonly RiderState[]): boolean {
  const self = riders[selfIndex];
  for (let i = 0; i < riders.length; i++) {
    if (i === selfIndex) continue;
    const gap = riders[i].dist - self.dist;
    if (gap >= RACE.draftGapMin && gap <= RACE.draftGapMax
      && Math.abs(riders[i].lateral - self.lateral) <= RACE.draftLateralWidth) {
      return true;
    }
  }
  return false;
}
