import { describe, expect, it } from 'vitest';
import { BTN, boxCenter, createMoveState, stepMovement, type InputMsg } from '../src';

function input(seq: number, yaw: number, moveX: number, moveZ: number, buttons = 0): InputMsg {
  return { seq, moveX, moveZ, yaw, pitch: 0, buttons, slot: 0 };
}

function run(s: ReturnType<typeof createMoveState>, inp: InputMsg, ticks: number, obstacles: Parameters<typeof stepMovement>[2] = []) {
  for (let i = 0; i < ticks; i++) stepMovement(s, { ...inp, seq: inp.seq + i }, obstacles);
}

describe('stepMovement', () => {
  it('walks forward about WALK_SPEED over 1s', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1), 30);
    expect(s.z).toBeLessThan(-4.0);
    expect(s.z).toBeGreaterThan(-4.25);
    expect(s.onGround).toBe(true);
  });

  it('jumps and lands back on ground', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 0, BTN.JUMP), 15); // 0.5s: apex at ~0.25s
    expect(s.y).toBeGreaterThan(0.1);
    run(s, input(31, 0, 0, 0), 30);
    expect(s.y).toBe(0);
    expect(s.onGround).toBe(true);
  });

  it('crouching lowers height and speed', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.CROUCH), 30);
    expect(s.height).toBe(1.2);
    expect(s.z).toBeGreaterThan(-2.5);
  });

  it('sprint is faster than walk', () => {
    const walk = createMoveState(0, 0, 0);
    const sprint = createMoveState(0, 0, 0);
    run(walk, input(1, 0, 0, 1), 30);
    run(sprint, input(1, 0, 0, 1, BTN.SPRINT), 30);
    expect(Math.abs(sprint.z)).toBeGreaterThan(Math.abs(walk.z) + 1.5);
  });

  it('tactical sprint reaches about 7.4 m/s and sets the sprinting flag', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT), 30);
    expect(s.sprinting).toBe(true);
    expect(s.vz).toBeLessThan(-7.2);
    expect(s.vz).toBeGreaterThan(-7.5);
  });

  it('fire edge during sprint applies sprint-out lock and suppresses sprint while firing', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT), 10);
    const fast = Math.hypot(s.vx, s.vz);
    run(s, input(11, 0, 0, 1, BTN.SPRINT | BTN.FIRE), 1);
    expect(s.sprintLockT).toBeGreaterThan(0);
    run(s, input(12, 0, 0, 1, BTN.SPRINT | BTN.FIRE), 20);
    expect(s.sprintLockT).toBe(0);
    expect(Math.hypot(s.vx, s.vz)).toBeLessThan(fast);
  });

  it('sprint resumes after fire is released and lock expires', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT | BTN.FIRE), 1);
    run(s, input(2, 0, 0, 1, BTN.FIRE), 30);
    expect(s.sprintLockT).toBe(0);
    run(s, input(32, 0, 0, 1, BTN.SPRINT), 30);
    expect(Math.hypot(s.vx, s.vz)).toBeGreaterThan(6);
  });

  it('slide inherits momentum, boosts beyond sprint and ends when slow', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT), 10);
    const speedBefore = Math.hypot(s.vx, s.vz);
    run(s, input(11, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(true);
    expect(Math.hypot(s.vx, s.vz)).toBeGreaterThan(speedBefore);
    expect(s.slideV0).toBeCloseTo(Math.max(speedBefore * 1.15, 6.0), 5);
    expect(s.height).toBe(1.2);
    run(s, input(12, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 30);
    expect(s.sliding).toBe(false);
    expect(s.slideCooldownT).toBeGreaterThan(0);
  });

  it('slide has cooldown before it can trigger again', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(true);
    run(s, input(2, 0, 0, 1), 25);
    expect(s.sliding).toBe(false);
    expect(s.slideCooldownT).toBeGreaterThan(0);
    run(s, input(27, 0, 0, 1, BTN.SPRINT), 5);
    const crouchTick = 32;
    run(s, input(crouchTick, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(false);
    run(s, input(33, 0, 0, 1, BTN.SPRINT), 20);
    run(s, input(53, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(true);
  });

  it('jump cancels slide and keeps horizontal momentum', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(true);
    const slideSpeed = Math.hypot(s.vx, s.vz);
    run(s, input(2, 0, 0, 1, BTN.SPRINT | BTN.CROUCH | BTN.JUMP), 1);
    expect(s.sliding).toBe(false);
    expect(s.onGround).toBe(false);
    expect(Math.hypot(s.vx, s.vz)).toBeGreaterThan(slideSpeed - 0.5);
    expect(s.slideCooldownT).toBeGreaterThan(0);
  });

  it('stops at wall', () => {
    const wall = boxCenter(5, 1.5, 0, 0.5, 3, 8);
    const s = createMoveState(0, 0, 0);
    // face +x: forward = (-sin(yaw),0,-cos(yaw)) = (1,0,0) => yaw = -PI/2
    run(s, input(1, -Math.PI / 2, 0, 1), 90, [wall]);
    expect(s.x).toBeGreaterThan(4.3);
    expect(s.x).toBeLessThan(4.4);
  });

  it('stands on a low platform when falling onto it', () => {
    const platform = boxCenter(0, 0.25, -3, 1.2, 0.5, 1.2);
    const s = createMoveState(0, 2, -3);
    run(s, input(1, 0, 0, 0), 60, [platform]);
    expect(s.y).toBeCloseTo(0.5, 3);
    expect(s.onGround).toBe(true);
  });

  it('mantles up onto a ledge too high to jump', () => {
    const ledge = boxCenter(0, 0.55, -3, 1.2, 1.1, 1.2);
    const s = createMoveState(0, 0, -1.6);
    let mantled = false;
    for (let i = 0; i < 40 && !mantled; i++) {
      stepMovement(s, input(i + 1, 0, 0, 1, BTN.JUMP), [ledge]);
      mantled = s.onGround && s.y > 1.0;
    }
    expect(mantled).toBe(true);
    run(s, input(41, 0, 0, 1), 3, [ledge]);
    expect(s.y).toBeCloseTo(1.1, 2);
    expect(s.onGround).toBe(true);
  });

  it('cannot mantle onto a wall beyond reach', () => {
    const wall = boxCenter(0, 1.25, -3, 1.2, 2.5, 1.2);
    const s = createMoveState(0, 0, -1.6);
    run(s, input(1, 0, 0, 1, BTN.JUMP), 40, [wall]);
    expect(s.y).toBeLessThan(1.2);
    expect(s.onGround).toBe(true);
  });
});
