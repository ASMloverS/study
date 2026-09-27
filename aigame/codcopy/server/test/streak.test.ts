import { describe, expect, it } from 'vitest';
import { AIRSTRIKE_COUNT, MAPS } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';
import { createStreakState, streakAvailableMask, streakCanUse, streakConsume, streakOnKill } from '../src/game/killstreak';

type RoomInternals = {
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function input(seq: number, buttons = 0, yaw = 0, pitch = -1.0, streak = 0) {
  return { seq, moveX: 0, moveZ: 0, yaw, pitch, buttons, slot: 0, streak };
}

describe('killstreak state machine', () => {
  it('earns tiers at 3/5/6 kills once per earn-consume cycle', () => {
    const s = createStreakState();
    expect(streakOnKill(s, 1)).toEqual([]);
    expect(streakOnKill(s, 3)).toEqual([0]);
    expect(streakOnKill(s, 4)).toEqual([]);
    expect(streakOnKill(s, 5)).toEqual([1]);
    expect(streakOnKill(s, 6)).toEqual([2]);
    expect(streakAvailableMask(s)).toBe(0b111);
  });

  it('[M14] consume resets the tier; next kill re-earns if counter still >= threshold', () => {
    const s = createStreakState();
    streakOnKill(s, 3);
    expect(streakCanUse(s, 1)).toBe(true);
    streakConsume(s, 1);
    expect(streakAvailableMask(s)).toBe(0);
    expect(streakCanUse(s, 1)).toBe(false);
    expect(streakOnKill(s, 4)).toEqual([0]);
    streakOnKill(s, 5);
    streakConsume(s, 2);
    // 计数 5：下一杀（6）同时重赚空袭并首达集束
    expect(streakOnKill(s, 6)).toEqual([1, 2]);
  });
});

describe('killstreak in room', () => {
  it('kills earn streaks, events fire, snapshot exposes sv', () => {
    const room = new Room(MAPS.warehouse, { seed: 31, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const c = room.addPlayer('C', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 3; i++) internals.killPlayer(a, b, 'ar', false);
    expect(a.streak).toBe(3);
    expect(streakAvailableMask(a.streaks)).toBe(1);
    expect(room.drainEvents().some((e) => e.type === 'streakEarned' && e.tier === 1 && e.playerId === a.id)).toBe(true);
    const snap = room.snapshot();
    expect(snap.players.find((p) => p.id === a.id)!.sv).toBe(1);
    internals.killPlayer(c, a, 'ar', false);
    // [M14] 死亡保留奖励：计数清零但未用 UAV 档位仍在
    expect(streakAvailableMask(a.streaks)).toBe(1);
    expect(a.streak).toBe(0);
  });

  it('[M14] earned rewards persist through death and can be used after respawn', () => {
    const room = new Room(MAPS.warehouse, { seed: 35, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 3; i++) internals.killPlayer(a, b, 'ar', false);
    internals.killPlayer(b, a, 'ar', false);
    expect(a.streak).toBe(0);
    expect(streakAvailableMask(a.streaks)).toBe(1);
    for (let i = 0; i < 95; i++) room.step(); // 等待重生
    expect(a.alive).toBe(true);
    expect(streakAvailableMask(a.streaks)).toBe(1);
    room.enqueueInput(a.id, input(1, 0, 0, -1.0, 1));
    room.step();
    expect(room.drainEvents().some((e) => e.type === 'streakUse' && e.tier === 1)).toBe(true);
    expect(streakAvailableMask(a.streaks)).toBe(0);
  });

  it('UAV activates via streak input and is consumed', () => {
    const room = new Room(MAPS.warehouse, { seed: 32, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    (room as unknown as RoomInternals).killPlayer(a, b, 'ar', false);
    (room as unknown as RoomInternals).killPlayer(a, b, 'ar', false);
    (room as unknown as RoomInternals).killPlayer(a, b, 'ar', false);
    room.enqueueInput(a.id, input(1, 0, 0, -1.0, 1));
    room.step();
    expect(room.drainEvents().some((e) => e.type === 'streakUse' && e.tier === 1)).toBe(true);
    expect(streakAvailableMask(a.streaks)).toBe(0);
  });

  it('airstrike schedules 5 blasts along facing and can kill with cause', () => {
    const room = new Room(MAPS.warehouse, { seed: 33, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const c = room.addPlayer('C', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    c.spawnProtUntil = 0;
    // a 在 (12,5) 面向 -z 俯角，c 在落点前方
    Object.assign(a.st, { x: 12, z: 5, vx: 0, vy: 0, vz: 0 });
    Object.assign(c.st, { x: 12, z: 2, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 0, 0, -0.9, 2));
    for (let i = 0; i < 130; i++) room.step();
    const events = room.drainEvents();
    expect(events.filter((e) => e.type === 'blast' && e.cause === 'airstrike').length).toBe(AIRSTRIKE_COUNT);
    expect(c.alive).toBe(false);
    expect(a.kills).toBe(6);
    expect(events.some((e) => e.type === 'kill' && e.cause === 'airstrike')).toBe(true);
    // [M14] 空袭消耗后计数 6 ≥ 5 立即重赚（bit1）；集束首达（bit2）；UAV 未用保留（bit0）
    expect(streakAvailableMask(a.streaks)).toBe(0b111);
  });

  it('streak input without earned tier does nothing', () => {
    const room = new Room(MAPS.warehouse, { seed: 34, bots: 0 });
    const a = room.addPlayer('A', false);
    room.enqueueInput(a.id, input(1, 0, 0, -1.0, 3));
    room.step();
    expect(room.drainEvents().some((e) => e.type === 'streakUse')).toBe(false);
  });
});
