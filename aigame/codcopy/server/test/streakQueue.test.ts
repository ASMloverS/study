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

/** [M15] 单机/高延迟时序:客户端心跳持续入队,服务器 tick 迟到后 takeInput 一次性裁剪,
 * 一次性连杀意图帧(streak/streakTarget/streakYaw)若落在丢弃区会永久丢失,激活静默失效。 */
describe('[M15] streak intent survives input-queue trimming', () => {
  it('airstrike intent frame dropped by trim is merged into surviving head', () => {
    const room = new Room(MAPS.warehouse, { seed: 47, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    // 堆积 6 帧,一次性意图夹在 seq2(将被裁剪丢弃)
    room.enqueueInput(a.id, input(1, 0));
    room.enqueueInput(a.id, input(2, 2, { x: 5, z: -5 }, 0));
    room.enqueueInput(a.id, input(3, 0));
    room.enqueueInput(a.id, input(4, 0));
    room.enqueueInput(a.id, input(5, 0));
    room.enqueueInput(a.id, input(6, 0));
    room.step();
    const evs = room.drainEvents();
    expect(evs.some((e) => e.type === 'streakUse' && e.tier === 2)).toBe(true);
  });

  it('latest intent wins when multiple intent frames are trimmed', () => {
    const room = new Room(MAPS.warehouse, { seed: 48, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const internals = room as unknown as RoomInternals;
    for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
    room.enqueueInput(a.id, input(1, 0));
    room.enqueueInput(a.id, input(2, 2, { x: 5, z: -5 }, 0));
    room.enqueueInput(a.id, input(3, 1));
    room.enqueueInput(a.id, input(4, 0));
    room.enqueueInput(a.id, input(5, 0));
    room.enqueueInput(a.id, input(6, 0));
    room.step();
    const evs = room.drainEvents();
    expect(evs.some((e) => e.type === 'streakUse' && e.tier === 1)).toBe(true);
    expect(evs.some((e) => e.type === 'streakUse' && e.tier === 2)).toBe(false);
  });

  it('trim without intent frames keeps newest inputs (regression)', () => {
    const room = new Room(MAPS.warehouse, { seed: 49, bots: 0, killLimit: 100 });
    const a = room.addPlayer('A', false);
    for (let i = 1; i <= 6; i++) room.enqueueInput(a.id, input(i, 0));
    room.step();
    expect((a as unknown as { lastInput: { seq: number } }).lastInput.seq).toBe(4);
  });
});
