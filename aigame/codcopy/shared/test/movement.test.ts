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

  it('slide boosts speed beyond sprint and ends after SLIDE_MAX_DURATION', () => {
    const s = createMoveState(0, 0, 0);
    run(s, input(1, 0, 0, 1, BTN.SPRINT), 10);
    const speedBefore = Math.hypot(s.vx, s.vz);
    run(s, input(11, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 1);
    expect(s.sliding).toBe(true);
    expect(Math.hypot(s.vx, s.vz)).toBeGreaterThan(speedBefore);
    expect(s.height).toBe(1.2);
    run(s, input(12, 0, 0, 1, BTN.SPRINT | BTN.CROUCH), 30);
    expect(s.sliding).toBe(false);
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
});
