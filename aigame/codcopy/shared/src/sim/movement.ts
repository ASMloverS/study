import type { InputMsg, WeaponId } from '../protocol';
import { BTN } from '../protocol';
import { WEAPONS } from '../weapons';
import {
  ADS_SPEED,
  AIR_ACCEL,
  CROUCH_SPEED,
  GRAVITY,
  GROUND_ACCEL_K,
  JUMP_VELOCITY,
  MANTLE_COOLDOWN,
  MANTLE_LAND_INSET,
  MANTLE_MAX_RISE,
  MANTLE_MIN_RISE,
  MANTLE_REACH,
  PLAYER_CROUCH_HEIGHT,
  PLAYER_EYE_RATIO,
  PLAYER_RADIUS,
  PLAYER_STAND_HEIGHT,
  SLIDE_COOLDOWN,
  SLIDE_END_SPEED,
  SLIDE_FRICTION,
  SLIDE_MIN_START_SPEED,
  SLIDE_START_MUL,
  SPRINT_SPEED,
  TERMINAL_VELOCITY,
  TICK_DT,
  WALK_SPEED,
} from '../constants';
import { bodyOverlaps, moveBodyAxis, type AABB } from '../physics/aabb';
import { raycastBoxes } from '../physics/raycast';

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
  slideV0: number;
  slideCooldownT: number;
  sprintLockT: number;
  mantleCdT: number;
  sprinting: boolean;
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
    slideV0: 0,
    slideCooldownT: 0,
    sprintLockT: 0,
    mantleCdT: 0,
    sprinting: false,
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

export function stepMovement(s: MoveState, input: InputMsg, obstacles: readonly AABB[], dt: number = TICK_DT, weapon: WeaponId = 'ar'): void {
  const btn = input.buttons;
  const wdef = WEAPONS[weapon];
  const wantCrouch = (btn & BTN.CROUCH) !== 0;
  const wantAds = (btn & BTN.ADS) !== 0;
  const fireHeld = (btn & BTN.FIRE) !== 0;
  const fireEdge = fireHeld && (s.prevButtons & BTN.FIRE) === 0;
  const moving = input.moveX !== 0 || input.moveZ !== 0;
  const sprintIntent = (btn & BTN.SPRINT) !== 0 && !wantAds && moving;
  if (s.sprintLockT > 0) s.sprintLockT = Math.max(0, s.sprintLockT - dt);
  if (fireEdge && sprintIntent && !s.sliding && s.sprintLockT <= 0) {
    s.sprintLockT = wdef.sprintOutTime;
  }
  const sprinting = sprintIntent && !wantCrouch && !fireHeld && s.sprintLockT <= 0;
  s.sprinting = sprinting;
  const crouchEdge = wantCrouch && (s.prevButtons & BTN.CROUCH) === 0;

  if (s.slideCooldownT > 0) s.slideCooldownT = Math.max(0, s.slideCooldownT - dt);
  if (crouchEdge && sprintIntent && s.onGround && !s.sliding && s.slideCooldownT <= 0) {
    const w = wishDir(input.yaw, input.moveX, input.moveZ);
    const v0 = Math.max(Math.hypot(s.vx, s.vz) * SLIDE_START_MUL, SLIDE_MIN_START_SPEED);
    s.sliding = true;
    s.slideT = 0;
    s.slideV0 = v0;
    s.vx = w.x * v0;
    s.vz = w.z * v0;
  }

  s.crouching = wantCrouch && !s.sliding;

  if (s.sliding) {
    s.slideT += dt;
    const speed = s.slideV0 * Math.exp(-SLIDE_FRICTION * s.slideT);
    const len = Math.hypot(s.vx, s.vz);
    if (len > 1e-6) {
      s.vx = (s.vx / len) * speed;
      s.vz = (s.vz / len) * speed;
    }
    if (speed <= SLIDE_END_SPEED) {
      s.sliding = false;
      s.slideCooldownT = SLIDE_COOLDOWN;
    }
  } else {
    const speed = s.crouching
      ? CROUCH_SPEED * wdef.moveMul
      : wantAds
        ? ADS_SPEED * wdef.adsMul
        : (sprinting ? SPRINT_SPEED : WALK_SPEED) * wdef.moveMul;
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
    if (s.sliding) {
      s.sliding = false;
      s.slideCooldownT = SLIDE_COOLDOWN;
    }
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

  if (s.mantleCdT > 0) s.mantleCdT = Math.max(0, s.mantleCdT - dt);
  if (!s.onGround && (btn & BTN.JUMP) !== 0 && moving && s.mantleCdT <= 0) {
    const f = wishDir(input.yaw, input.moveX, input.moveZ);
    const wall = raycastBoxes(s.x, s.y + 0.25, s.z, f.x, 0, f.z, MANTLE_REACH, obstacles);
    if (wall) {
      const rise = wall.box.maxY - s.y;
      if (rise >= MANTLE_MIN_RISE && rise <= MANTLE_MAX_RISE) {
        const landX = s.x + f.x * (wall.t + MANTLE_LAND_INSET);
        const landZ = s.z + f.z * (wall.t + MANTLE_LAND_INSET);
        const body = { x: landX, y: wall.box.maxY, z: landZ, height: PLAYER_STAND_HEIGHT, radius: s.radius };
        if (!obstacles.some((o) => bodyOverlaps(body, o))) {
          s.x = landX;
          s.y = wall.box.maxY;
          s.z = landZ;
          s.vx = 0;
          s.vz = 0;
          s.vy = 0;
          s.onGround = true;
          s.sliding = false;
          s.crouching = wantCrouch;
          s.mantleCdT = MANTLE_COOLDOWN;
        }
      }
    }
  }

  s.height = s.crouching || s.sliding ? PLAYER_CROUCH_HEIGHT : PLAYER_STAND_HEIGHT;
  s.yaw = input.yaw;
  s.pitch = input.pitch;
  s.prevButtons = btn;
}
