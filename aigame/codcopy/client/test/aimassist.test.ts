import { describe, expect, it } from 'vitest';
import { DEG2RAD } from 'shared';
import { ASSIST_LEVELS, ASSIST_PULL_ANGLE, ASSIST_PULL_RATE, ASSIST_SLOW_ANGLE, applyAimAssist } from '../src/aimassist';

describe('aim assist math (M12)', () => {
  it('off / zero strength returns user view unchanged', () => {
    const r = applyAimAssist({
      userYaw: 1,
      userPitch: 0.2,
      prevYaw: 0.9,
      prevPitch: 0.1,
      targetYaw: 2,
      targetPitch: 0.5,
      strength: 0,
      dt: 1 / 60,
    });
    expect(r.yaw).toBe(1);
    expect(r.pitch).toBe(0.2);
  });

  it('slowdown dampens user delta when inside slow angle', () => {
    // 目标在正前方 1°（< 4°），用户本帧向右拉了 0.02rad → 应被减速
    const strength = ASSIST_LEVELS.medium;
    const r = applyAimAssist({
      userYaw: 0.02,
      userPitch: 0,
      prevYaw: 0,
      prevPitch: 0,
      targetYaw: 1 * DEG2RAD,
      targetPitch: 0,
      strength,
      dt: 1 / 60,
    });
    expect(r.yaw).toBeGreaterThan(0);
    expect(r.yaw).toBeLessThan(0.02);
  });

  it('pull is capped by hard rate limit', () => {
    const dt = 1 / 60;
    // 目标 1.4° < 1.5°，吸附上限 = 2°/s × 0.5 × dt
    const dyaw = 1.4 * DEG2RAD;
    const maxD = ASSIST_PULL_RATE * ASSIST_LEVELS.medium * dt;
    const r = applyAimAssist({
      userYaw: 0,
      userPitch: 0,
      prevYaw: 0,
      prevPitch: 0,
      targetYaw: dyaw,
      targetPitch: 0,
      strength: ASSIST_LEVELS.medium,
      dt,
    });
    expect(r.yaw).toBeCloseTo(maxD, 8);
    expect(r.yaw).toBeLessThan(dyaw);
  });

  it('no pull beyond pull angle; no slowdown beyond slow angle', () => {
    const r = applyAimAssist({
      userYaw: 0,
      userPitch: 0,
      prevYaw: 0,
      prevPitch: 0,
      targetYaw: 3 * DEG2RAD,
      targetPitch: 0,
      strength: ASSIST_LEVELS.high,
      dt: 1 / 60,
    });
    expect(r.yaw).toBe(0);
    expect(3 * DEG2RAD).toBeGreaterThan(ASSIST_PULL_ANGLE);
    // 用户增量与目标角距均超出慢速区 → 原样返回
    const far = applyAimAssist({
      userYaw: 0.05,
      userPitch: 0,
      prevYaw: 0,
      prevPitch: 0,
      targetYaw: 12 * DEG2RAD,
      targetPitch: 0,
      strength: ASSIST_LEVELS.high,
      dt: 1 / 60,
    });
    expect(far.yaw).toBeCloseTo(0.05, 10);
    expect(12 * DEG2RAD - 0.05).toBeGreaterThan(ASSIST_SLOW_ANGLE);
  });

  it('pull rate hard cap is never exceeded even at high strength', () => {
    const dt = 1 / 60;
    const r = applyAimAssist({
      userYaw: 0,
      userPitch: 0,
      prevYaw: 0,
      prevPitch: 0,
      targetYaw: 1.49 * DEG2RAD,
      targetPitch: 0.2 * DEG2RAD,
      strength: ASSIST_LEVELS.high,
      dt,
    });
    const moved = Math.hypot(r.yaw, r.pitch);
    expect(moved).toBeLessThanOrEqual(ASSIST_PULL_RATE * ASSIST_LEVELS.high * dt + 1e-9);
  });
});
