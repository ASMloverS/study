import { describe, expect, it } from 'vitest';
import type { GameEvent } from 'shared';
import { Killcam } from '../src/render/killcam';

function snap(id: number, x: number, z: number): any {
  return { id, x, y: 0, z, vx: 0, vy: 0, vz: 0, yaw: 0, pitch: 0, h: 1.8, sl: false, hp: 100, a: true, w: 'ar', m: 30, rs: 90, rl: 0, k: 0, d: 0, sf: 0, sh: 0, bs: 0 };
}

function shotEvent(shooterId: number): GameEvent {
  return { type: 'shot', tick: 0, shooterId, origin: { x: 0, y: 1, z: 0 }, end: { x: 1, y: 1, z: 0 }, weapon: 'ar' };
}

describe('killcam buffer (M6.4)', () => {
  it('interpolates player positions at sampled time', () => {
    const kc = new Killcam();
    kc.pushSnapshot([snap(1, 0, 0), snap(2, 10, 0)], 100);
    kc.pushSnapshot([snap(1, 0, 2), snap(2, 10, 4)], 100.1);
    const mid = kc.sample(103, 100.05, 3);
    expect(mid).not.toBeNull();
    const p1 = mid!.find((p) => p.id === 1)!;
    expect(p1.z).toBeCloseTo(1);
    expect(p1.x).toBeCloseTo(0);
    const last = kc.sample(103, 103, 3);
    expect(last!.find((p) => p.id === 1)!.z).toBeCloseTo(2);
    const clamped = kc.sample(103, 96, 3);
    expect(clamped!.find((p) => p.id === 1)!.z).toBeCloseTo(0);
  });

  it('drains events once within a time window', () => {
    const kc = new Killcam();
    kc.pushEvent(shotEvent(7), 99.5);
    kc.pushEvent(shotEvent(7), 100.2);
    const first = kc.drainEvents(99, 100);
    expect(first.length).toBe(1);
    const second = kc.drainEvents(100, 101);
    expect(second.length).toBe(1);
    expect(kc.drainEvents(100.2, 101)).toHaveLength(0);
  });

  it('drops frames older than the window', () => {
    const kc = new Killcam();
    for (let i = 0; i < 300; i++) kc.pushSnapshot([snap(1, i, 0)], 100 + i * 0.1);
    expect(kc.size).toBeLessThan(130);
    const near = kc.sample(129.9, 127, 3);
    expect(near).not.toBeNull();
    expect(near!.find((p) => p.id === 1)!.x).toBeCloseTo(270, 0);
  });
});
