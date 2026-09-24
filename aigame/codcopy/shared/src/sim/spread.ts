import { SPREAD_MUL_AIR, SPREAD_MUL_JUMP, SPREAD_MUL_MOVE, SPREAD_MUL_SLIDE } from '../constants';
import type { InputMsg } from '../protocol';
import type { MoveState } from './movement';

export function spreadMulFor(st: MoveState, input: InputMsg): number {
  if (st.sliding) return SPREAD_MUL_SLIDE;
  if (!st.onGround) return st.vy > 0 ? SPREAD_MUL_JUMP : SPREAD_MUL_AIR;
  if (input.moveX !== 0 || input.moveZ !== 0) return SPREAD_MUL_MOVE;
  return 1;
}

export function jitterDir(
  base: { x: number; y: number; z: number },
  spread: number,
  rng: () => number,
): { x: number; y: number; z: number } {
  if (spread <= 0) return base;
  const upX = Math.abs(base.y) < 0.99 ? 0 : 1;
  const upY = Math.abs(base.y) < 0.99 ? 1 : 0;
  let rx = -base.z * upY;
  let ry = base.z * upX;
  let rz = base.x * upY - base.y * upX;
  const rLen = Math.hypot(rx, ry, rz) || 1;
  rx /= rLen;
  ry /= rLen;
  rz /= rLen;
  const ux = ry * base.z - rz * base.y;
  const uy = rz * base.x - rx * base.z;
  const uz = rx * base.y - ry * base.x;
  const a = rng() * Math.PI * 2;
  const r = Math.sqrt(rng()) * spread;
  const ca = Math.cos(a) * r;
  const sa = Math.sin(a) * r;
  const dx = base.x + rx * ca + ux * sa;
  const dy = base.y + ry * ca + uy * sa;
  const dz = base.z + rz * ca + uz * sa;
  const len = Math.hypot(dx, dy, dz);
  return { x: dx / len, y: dy / len, z: dz / len };
}
