import { describe, expect, it } from 'vitest';
import { AIRSTRIKE_COUNT, CLUSTER_COUNT, CLUSTER_SCATTER, MAPS } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';

type RoomInternals = {
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function input(seq: number, streak: number, streakTarget?: { x: number; z: number }, streakYaw?: number) {
  return { seq, moveX: 0, moveZ: 0, yaw: 0, pitch: -1.0, buttons: 0, slot: 0, streak, streakTarget, streakYaw };
}

describe('[M15] killstreak placement input', () => {
  it('non-finite streakTarget is dropped (falls back to player position at activation)', () => {
    const room = new Room(MAPS.warehouse, { seed: 41, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    Object.assign(a.st, { x: 5, z: 5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: Number.NaN, z: 5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(AIRSTRIKE_COUNT);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.abs(e.pos.x - 5)).toBeLessThanOrEqual(4.6);
      expect(Math.abs(e.pos.z - 5)).toBeLessThanOrEqual(4.6);
    }
  });
});

describe('[M15] placement targeting and self-exemption', () => {
  function setup(seed: number) {
    const room = new Room(MAPS.warehouse, { seed, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const c = room.addPlayer('C', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    return { room, a, b, c };
  }

  it('airstrike uses client placement target and heading', () => {
    const { room, a, c } = setup(42);
    a.spawnProtUntil = 0;
    c.spawnProtUntil = 0;
    Object.assign(c.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 10, z: -8 }, Math.PI / 2));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(AIRSTRIKE_COUNT);
    expect(c.alive).toBe(false);
    expect(a.kills).toBe(6);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(e.pos.z).toBeCloseTo(-8, 0);
      expect(e.pos.x).toBeGreaterThanOrEqual(5.5);
      expect(e.pos.x).toBeLessThanOrEqual(14.5);
    }
  });

  it('caller standing at ground zero is exempt from own airstrike', () => {
    const { room, a, c } = setup(43);
    a.spawnProtUntil = 0;
    c.spawnProtUntil = 0;
    Object.assign(a.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    Object.assign(c.st, { x: 10, z: -8, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 10, z: -8 }, Math.PI / 2));
    for (let i = 0; i < 130; i++) room.step();
    const evs = room.drainEvents();
    expect(evs.some((e) => e.type === 'hit' && e.victimId === a.id)).toBe(false);
    expect(a.alive).toBe(true);
    expect(a.health).toBe(100);
    expect(c.alive).toBe(false);
  });

  it('out-of-bounds placement target is clamped to map edge', () => {
    const { room, a } = setup(44);
    room.enqueueInput(a.id, input(1, 2, { x: 999, z: -999 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast');
    expect(blasts.length).toBe(AIRSTRIKE_COUNT);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.abs(e.pos.x)).toBeLessThanOrEqual(23.1);
      expect(Math.abs(e.pos.z)).toBeLessThanOrEqual(23.1);
    }
  });

  it('cluster strike scatters within bounds and exempts the caller', () => {
    // [M15] 种子 46：最近弹距中心 0.58 ≤ 3（CLUSTER_RADIUS），删除豁免必失败
    const { room, a, b } = setup(46);
    (room as unknown as RoomInternals).killPlayer(a, b, 'ar', false);
    a.spawnProtUntil = 0;
    Object.assign(a.st, { x: 3, z: -3, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 3, { x: 3, z: -3 }, undefined));
    for (let i = 0; i < 130; i++) room.step();
    const evs = room.drainEvents();
    const blasts = evs.filter((e) => e.type === 'blast' && e.cause === 'cluster');
    expect(blasts.length).toBe(CLUSTER_COUNT);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.hypot(e.pos.x - 3, e.pos.z + 3)).toBeLessThanOrEqual(CLUSTER_SCATTER + 0.1);
      expect(Math.abs(e.pos.x)).toBeLessThanOrEqual(23.1);
      expect(Math.abs(e.pos.z)).toBeLessThanOrEqual(23.1);
    }
    expect(evs.some((e) => e.type === 'hit' && e.victimId === a.id)).toBe(false);
    expect(a.alive).toBe(true);
    expect(a.health).toBe(100);
  });
});

describe('[M15] strike delay', () => {
  it('airstrike detonates after 90-tick (3s) delay, not before', () => {
    const room = new Room(MAPS.warehouse, { seed: 45, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    room.enqueueInput(a.id, input(1, 2, { x: 0, z: 0 }, 0));
    for (let i = 0; i < 82; i++) room.step();
    expect(room.drainEvents().filter((e) => e.type === 'blast').length).toBe(0);
    for (let i = 0; i < 25; i++) room.step();
    expect(room.drainEvents().filter((e) => e.type === 'blast').length).toBe(AIRSTRIKE_COUNT);
  });
});
