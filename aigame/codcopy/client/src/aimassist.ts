import { DEG2RAD } from 'shared';

/** [M12] 辅助瞄准（纯客户端视角层）：粘滞 + 轻吸附。零服务端变更。 */

export type AssistLevel = 'off' | 'low' | 'medium' | 'high';

export const ASSIST_LEVELS: Record<AssistLevel, number> = {
  off: 0,
  low: 0.25,
  medium: 0.5,
  high: 0.75,
};

export const ASSIST_SLOW_ANGLE = 4 * DEG2RAD;
export const ASSIST_PULL_ANGLE = 1.5 * DEG2RAD;
/** 牵引上限（°/s，硬编码防作弊级强吸附） */
export const ASSIST_PULL_RATE = 2 * DEG2RAD;

export interface AimAssistInput {
  /** 本帧用户视角（鼠标/摇杆累计后） */
  userYaw: number;
  userPitch: number;
  /** 上一帧最终视角（计算用户本帧增量） */
  prevYaw: number;
  prevPitch: number;
  /** 目标（敌人胸口）的期望视角 */
  targetYaw: number;
  targetPitch: number;
  strength: number;
  dt: number;
}

function normAngle(a: number): number {
  let d = a % (Math.PI * 2);
  if (d > Math.PI) d -= Math.PI * 2;
  if (d < -Math.PI) d += Math.PI * 2;
  return d;
}

function clamp(v: number, lo: number, hi: number): number {
  return Math.max(lo, Math.min(hi, v));
}

/** 计算辅助修正后的视角；strength<=0 时原样返回 */
export function applyAimAssist(i: AimAssistInput): { yaw: number; pitch: number } {
  if (i.strength <= 0 || i.dt <= 0) return { yaw: i.userYaw, pitch: i.userPitch };
  const dyaw = normAngle(i.targetYaw - i.userYaw);
  const dpitch = normAngle(i.targetPitch - i.userPitch);
  const angDist = Math.hypot(dyaw, dpitch);
  let yaw = i.userYaw;
  let pitch = i.userPitch;
  if (angDist < ASSIST_SLOW_ANGLE) {
    // 粘滞：越接近目标减速越强（中心 100% 强度 → 边缘 50%）
    const slow = 1 - i.strength * (1 - 0.5 * (angDist / ASSIST_SLOW_ANGLE));
    yaw = i.prevYaw + (i.userYaw - i.prevYaw) * slow;
    pitch = i.prevPitch + (i.userPitch - i.prevPitch) * slow;
  }
  if (angDist < ASSIST_PULL_ANGLE) {
    // 轻吸附：向目标牵引，速率受强度缩放且总量硬上限
    const maxD = ASSIST_PULL_RATE * i.strength * i.dt;
    yaw += clamp(dyaw, -maxD, maxD);
    pitch += clamp(dpitch, -maxD, maxD);
  }
  return { yaw, pitch };
}
