import type { WeaponDef } from '../weapons';
import {
  RECOIL_MAX_PITCH,
  RECOIL_MAX_YAW,
  RECOIL_RECOVERY,
  RECOIL_RECOVERY_DELAY,
} from '../constants';

export interface RecoilState {
  pitch: number;
  yaw: number;
  lastShotAt: number;
}

export function createRecoilState(): RecoilState {
  return { pitch: 0, yaw: 0, lastShotAt: -1e9 };
}

export function addRecoilShot(s: RecoilState, w: WeaponDef, now: number, rng: () => number): void {
  s.pitch = Math.min(RECOIL_MAX_PITCH, s.pitch + w.recoilPitch);
  s.yaw = Math.max(-RECOIL_MAX_YAW, Math.min(RECOIL_MAX_YAW, s.yaw + (rng() * 2 - 1) * w.recoilYaw));
  s.lastShotAt = now;
}

export function updateRecoil(s: RecoilState, now: number, dt: number): void {
  if (now - s.lastShotAt < RECOIL_RECOVERY_DELAY) return;
  const drop = RECOIL_RECOVERY * dt;
  s.pitch = Math.max(0, s.pitch - drop);
  s.yaw = Math.abs(s.yaw) <= drop ? 0 : s.yaw - Math.sign(s.yaw) * drop;
}
