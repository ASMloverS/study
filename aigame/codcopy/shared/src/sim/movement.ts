import type { InputMsg } from '../protocol';
import { BTN } from '../protocol';
import {
  ADS_SPEED,
  AIR_ACCEL,
  CROUCH_SPEED,
  GRAVITY,
  GROUND_ACCEL_K,
  JUMP_VELOCITY,
  PLAYER_CROUCH_HEIGHT,
  PLAYER_EYE_RATIO,
  PLAYER_RADIUS,
  PLAYER_STAND_HEIGHT,
  SLIDE_END_SPEED,
  SLIDE_INITIAL_SPEED,
  SLIDE_MAX_DURATION,
  SPRINT_SPEED,
  TERMINAL_VELOCITY,
  TICK_DT,
  WALK_SPEED,
} from '../constants';
import { moveBodyAxis, type AABB } from '../physics/aabb';

export interface MoveState {
  x: number;
  y: number;
  z: number;
  vx: number;
  vy: number;
  vz: number;
  yaw: number;
  pitch: number;
  height: number;
  radius: number;
  crouching: boolean;
  sliding: boolean;
  slideT: number;
  onGround: boolean;
  prevButtons: number;
}

export function createMoveState(x: number, y: number, z: number, yaw = 0): MoveState {
  return {
    x,
    y,
    z,
    vx: 0,
    vy: 0,
    vz: 0,
    yaw,
    pitch: 0,
    height: PLAYER_STAND_HEIGHT,
    radius: PLAYER_RADIUS,
    crouching: false,
    sliding: false,
    slideT: 0,
    onGround: true,
    prevButtons: 0,
  };
}

export function viewDir(yaw: number, pitch: number): { x: number; y: number; z: number } {
  const cp = Math.cos(pitch);
  return { x: -Math.sin(yaw) * cp, y: Math.sin(pitch), z: -Math.cos(yaw) * cp };
}

export function wishDir(yaw: number, moveX: number, moveZ: number): { x: number; z: number } {
  const fx = -Math.sin(yaw);
  const fz = -Math.cos(yaw);
  const rx = Math.cos(yaw);
  const rz = -Math.sin(yaw);
  let x = fx * moveZ + rx * moveX;
  let z = fz * moveZ + rz * moveX;
  const len = Math.hypot(x, z);
  if (len > 1e-6) {
    x /= len;
    z /= len;
  }
  return { x, z };
}

export function eyeY(s: MoveState): number {
  return s.y + s.height * PLAYER_EYE_RATIO;
}

export function stepMovement(s: MoveState, input: InputMsg, obstacles: readonly AABB[], dt: number = TICK_DT): void {
  const btn = input.buttons;
  const wantCrouch = (btn & BTN.CROUCH) !== 0;
  const wantAds = (btn & BTN.ADS) !== 0;
  const moving = input.moveX !== 0 || input.moveZ !== 0;
  const sprintHeld = (btn & BTN.SPRINT) !== 0 && !wantAds && moving;
  const sprinting = sprintHeld && !wantCrouch;
  const crouchEdge = wantCrouch && (s.prevButtons & BTN.CROUCH) === 0;

  if (crouchEdge && sprintHeld && s.onGround && !s.sliding) {
    const w = wishDir(input.yaw, input.moveX, input.moveZ);
    s.sliding = true;
    s.slideT = 0;
    s.vx = w.x * SLIDE_INITIAL_SPEED;
    s.vz = w.z * SLIDE_INITIAL_SPEED;
  }

  s.crouching = wantCrouch && !s.sliding;

  if (s.sliding) {
    s.slideT += dt;
    const k = Math.min(1, s.slideT / SLIDE_MAX_DURATION);
    const speed = SLIDE_INITIAL_SPEED + (SLIDE_END_SPEED - SLIDE_INITIAL_SPEED) * k;
    const len = Math.hypot(s.vx, s.vz);
    if (len > 1e-6) {
      s.vx = (s.vx / len) * speed;
      s.vz = (s.vz / len) * speed;
    }
    if (s.slideT >= SLIDE_MAX_DURATION) s.sliding = false;
  } else {
    const speed = s.crouching ? CROUCH_SPEED : wantAds ? ADS_SPEED : sprinting ? SPRINT_SPEED : WALK_SPEED;
    const w = wishDir(input.yaw, input.moveX, input.moveZ);
    if (s.onGround) {
      const blend = 1 - Math.exp(-GROUND_ACCEL_K * dt);
      s.vx += (w.x * speed - s.vx) * blend;
      s.vz += (w.z * speed - s.vz) * blend;
    } else if (moving) {
      s.vx += w.x * AIR_ACCEL * dt;
      s.vz += w.z * AIR_ACCEL * dt;
    }
  }

  if ((btn & BTN.JUMP) !== 0 && s.onGround) {
    s.vy = JUMP_VELOCITY;
    s.onGround = false;
    s.sliding = false;
    s.crouching = wantCrouch;
  }

  s.vy = Math.max(s.vy + GRAVITY * dt, TERMINAL_VELOCITY);

  const blockedX = moveBodyAxis(s, 'x', s.vx * dt, obstacles);
  const blockedZ = moveBodyAxis(s, 'z', s.vz * dt, obstacles);
  const vyBefore = s.vy;
  const blockedY = moveBodyAxis(s, 'y', s.vy * dt, obstacles);

  let grounded = false;
  if (blockedY && vyBefore < 0) grounded = true;
  if (s.y <= 0) {
    s.y = 0;
    if (vyBefore < 0) grounded = true;
  }
  s.onGround = grounded;
  if (blockedX) s.vx = 0;
  if (blockedZ) s.vz = 0;
  if (blockedY) s.vy = 0;
  else if (grounded) s.vy = 0;

  s.height = s.crouching || s.sliding ? PLAYER_CROUCH_HEIGHT : PLAYER_STAND_HEIGHT;
  s.yaw = input.yaw;
  s.pitch = input.pitch;
  s.prevButtons = btn;
}
