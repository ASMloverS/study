import { KILLSTREAK_AIRSTRIKE_KILLS, KILLSTREAK_CLUSTER_KILLS, KILLSTREAK_UAV_KILLS } from 'shared';

export const STREAK_THRESHOLDS = [KILLSTREAK_UAV_KILLS, KILLSTREAK_AIRSTRIKE_KILLS, KILLSTREAK_CLUSTER_KILLS] as const;

export interface StreakState {
  /** 各档是否已达成且未消耗（[M14] 死亡保留，仅激活消耗后重置） */
  earned: [boolean, boolean, boolean];
}

export function createStreakState(): StreakState {
  return { earned: [false, false, false] };
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

/** [M14] 死亡不再作废奖励：计数器由调用方清零，已获未用奖励保留（MW2019 式） */

/** 可用档位掩码（bit0 = UAV, bit1 = 空袭, bit2 = 集束） */
export function streakAvailableMask(s: StreakState): number {
  let m = 0;
  for (let i = 0; i < 3; i++) if (s.earned[i]) m |= 1 << i;
  return m;
}

/** 档位是否可激活（1 基 tier：1/2/3） */
export function streakCanUse(s: StreakState, tier: number): boolean {
  const i = tier - 1;
  return i >= 0 && i < 3 && s.earned[i];
}

/** [M14] 激活消耗：重置该档（可重新赚取；若当前计数仍 ≥ 阈值，下一次击杀即重新获得） */
export function streakConsume(s: StreakState, tier: number): void {
  s.earned[tier - 1] = false;
}
