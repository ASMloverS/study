import { describe, expect, it } from 'vitest';
import { MAPS } from 'shared';
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
    expect(blasts.length).toBe(5);
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      expect(Math.abs(e.pos.x - 5)).toBeLessThanOrEqual(5.5);
      expect(Math.abs(e.pos.z - 5)).toBeLessThanOrEqual(5.5);
    }
  });
});
