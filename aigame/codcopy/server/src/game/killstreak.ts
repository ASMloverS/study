import { KILLSTREAK_AIRSTRIKE_KILLS, KILLSTREAK_CLUSTER_KILLS, KILLSTREAK_UAV_KILLS } from 'shared';

export const STREAK_THRESHOLDS = [KILLSTREAK_UAV_KILLS, KILLSTREAK_AIRSTRIKE_KILLS, KILLSTREAK_CLUSTER_KILLS] as const;

export interface StreakState {
  /** 本条连杀内各档是否已达成（死亡清零） */
  earned: [boolean, boolean, boolean];
  /** 已激活消耗 */
  used: [boolean, boolean, boolean];
}

export function createStreakState(): StreakState {
  return { earned: [false, false, false], used: [false, false, false] };
}

/** 击杀后推进：streak 达到阈值且未达成过的档位标记为达成，返回新达成的档位（0 基）列表 */
export function streakOnKill(s: StreakState, streak: number): number[] {
  const fresh: number[] = [];
  for (let i = 0; i < 3; i++) {
    if (!s.earned[i] && streak >= STREAK_THRESHOLDS[i]) {
      s.earned[i] = true;
      fresh.push(i);
    }
  }
  return fresh;
}

/** 死亡清空：未使用的奖励作废（经典 MW 规则） */
export function streakOnDeath(s: StreakState): void {
  s.earned = [false, false, false];
  s.used = [false, false, false];
}

/** 可用档位掩码（bit0 = UAV, bit1 = 空袭, bit2 = 集束） */
export function streakAvailableMask(s: StreakState): number {
  let m = 0;
  for (let i = 0; i < 3; i++) if (s.earned[i] && !s.used[i]) m |= 1 << i;
  return m;
}

/** 档位是否可激活（1 基 tier：1/2/3） */
export function streakCanUse(s: StreakState, tier: number): boolean {
  const i = tier - 1;
  return i >= 0 && i < 3 && s.earned[i] && !s.used[i];
}

export function streakConsume(s: StreakState, tier: number): void {
  s.used[tier - 1] = true;
}
