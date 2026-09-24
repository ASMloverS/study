import { describe, expect, it } from 'vitest';
import { BTN, createMoveState, MAPS } from 'shared';
import { Room } from '../src';

type RoomInternals = {
  applyDamage(a: unknown, v: unknown, part: 'head' | 'body', dmg: number, headMul: number, cause?: string): void;
  killPlayer(killer: unknown, victim: unknown, cause: string, hs: boolean): void;
  explodeBarrel(attackerId: number, coverIndex: number): void;
  fire(p: unknown, input: Record<string, unknown>): void;
};

describe('match rules (M6.1)', () => {
  it('suicide via barrel explosion does not credit a kill', () => {
    const room = new Room(MAPS.warehouse, { seed: 5, bots: 0 });
    const a = room.addPlayer('A', false);
    const barrel = room.destructibles.find((d) => d.kind === 'barrel')!;
    Object.assign(a.st, createMoveState(barrel.center.x + 0.5, 0, barrel.center.z));
    a.spawnProtUntil = 0;
    a.health = 20;
    (room as unknown as RoomInternals).explodeBarrel(a.id, barrel.coverIndex);
    expect(a.alive).toBe(false);
    expect(a.kills).toBe(0);
    expect(a.deaths).toBe(1);
    expect(a.streak).toBe(0);
    const kill = room.drainEvents().find((e) => e.type === 'kill');
    expect(kill).toBeDefined();
    expect(kill!.cause).toBe('suicide');
  });

  it('spawn protection blocks damage, expires, and breaks on firing', () => {
    const room = new Room(MAPS.warehouse, { seed: 9, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    expect(b.spawnProtUntil).toBeGreaterThan(room.tick);
    (room as unknown as RoomInternals).applyDamage(a, b, 'body', 80, 1, 'ar');
    expect(b.health).toBe(100);
    b.spawnProtUntil = 0;
    (room as unknown as RoomInternals).applyDamage(a, b, 'body', 80, 1, 'ar');
    expect(b.health).toBe(20);

    a.spawnProtUntil = room.tick + 50;
    (room as unknown as RoomInternals).fire(a, { seq: 1, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons: BTN.FIRE, slot: 0 });
    expect(a.spawnProtUntil).toBe(0);
  });

  it('kill limit is configurable and ends the match', () => {
    const room = new Room(MAPS.warehouse, { seed: 2, bots: 0, killLimit: 2 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const c = room.addPlayer('C', false);
    expect(room.killLimit).toBe(2);
    const internals = room as unknown as RoomInternals;
    internals.killPlayer(a, b, 'ar', false);
    internals.killPlayer(a, c, 'sr', true);
    room.step();
    expect(room.over).toBe(true);
    expect(room.winner).toBe(a.id);
    const kill = room.drainEvents().filter((e) => e.type === 'kill');
    expect(kill.some((e) => e.type === 'kill' && e.hs === true && e.cause === 'sr')).toBe(true);
  });

  it('match duration is configurable', () => {
    const room = new Room(MAPS.warehouse, { seed: 4, bots: 0, durationSec: 30 });
    expect(room.durationSec).toBe(30);
    for (let i = 0; i < 910 && !room.over; i++) room.step();
    expect(room.over).toBe(true);
    expect(room.winner).toBeNull();
  });

  it('snapshot exposes spawn protection and streak', () => {
    const room = new Room(MAPS.warehouse, { seed: 6, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const snap = room.snapshot();
    const sa = snap.players.find((p) => p.id === a.id)!;
    expect(sa.sp).toBeGreaterThan(0);
    (room as unknown as RoomInternals).killPlayer(a, b, 'ar', false);
    const snap2 = room.snapshot();
    expect(snap2.players.find((p) => p.id === a.id)!.st).toBe(1);
  });
});
