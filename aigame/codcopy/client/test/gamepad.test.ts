import { describe, expect, it } from 'vitest';
import { deadzone, EMPTY_PAD, mapGamepad } from '../src/gamepad';

function pad(buttons: number[] = [], axes: number[] = []) {
  const all = Array.from({ length: 16 }, () => ({ pressed: false, value: 0 }));
  for (const i of buttons) all[i] = { pressed: true, value: 1 };
  return { buttons: all, axes };
}

describe('gamepad mapping (M12)', () => {
  it('idle pad maps to empty actions', () => {
    const a = mapGamepad(pad(), 0.12, 2.4, 1 / 60);
    expect(a).toEqual(EMPTY_PAD);
  });

  it('xbox standard layout: triggers/sticks/buttons', () => {
    const a = mapGamepad(pad([7, 6, 0, 10], [0, -1, 0.5, 0]), 0.12, 2.4, 1 / 60);
    expect(a.fire).toBe(true); // RT
    expect(a.ads).toBe(true); // LT
    expect(a.jump).toBe(true); // A
    expect(a.sprint).toBe(true); // L3
    expect(a.moveZ).toBeCloseTo(1, 6); // 左摇杆前推（-y）
    // 右摇杆 0.5 经死区重标定 (0.5-0.12)/0.88
    expect(a.lookDX).toBeCloseTo(-((0.5 - 0.12) / 0.88) * (2.4 / 60), 6);
  });

  it('dpad maps to streak tiers', () => {
    expect(mapGamepad(pad([12]), 0.12, 2.4, 1 / 60).streak).toBe(1);
    expect(mapGamepad(pad([15]), 0.12, 2.4, 1 / 60).streak).toBe(2);
    expect(mapGamepad(pad([13]), 0.12, 2.4, 1 / 60).streak).toBe(3);
  });

  it('trigger threshold 0.5 via value', () => {
    const all = Array.from({ length: 16 }, () => ({ pressed: false, value: 0 }));
    all[7] = { pressed: false, value: 0.7 };
    expect(mapGamepad({ buttons: all, axes: [] }, 0.12, 2.4, 1 / 60).fire).toBe(true);
    all[7] = { pressed: false, value: 0.3 };
    expect(mapGamepad({ buttons: all, axes: [] }, 0.12, 2.4, 1 / 60).fire).toBe(false);
  });
});

describe('deadzone (M12)', () => {
  it('inside deadzone returns zero', () => {
    expect(deadzone(0.1, 0.05, 0.12)).toEqual({ x: 0, y: 0 });
  });
  it('outside deadzone rescales but keeps direction', () => {
    const r = deadzone(1, 0, 0.12);
    expect(r.x).toBeCloseTo(1, 6);
    expect(r.y).toBeCloseTo(0, 6);
    const half = deadzone(0.56, 0, 0.12);
    expect(half.x).toBeCloseTo(0.5, 2);
    expect(half.x).toBeGreaterThan(0);
  });
});
