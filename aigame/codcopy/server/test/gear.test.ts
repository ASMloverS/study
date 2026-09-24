import { describe, expect, it } from 'vitest';
import { BTN, createMoveState, FRAG_FUSE_TICKS, FRAG_MAX_DMG, FRAG_RADIUS, MAPS } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';

type RoomInternals = {
  throwNade(p: ServerPlayer, input: Record<string, unknown>, kind: 'frag' | 'flash', sinceTick: number): void;
  explodeAt(attackerId: number | null, cx: number, cy: number, cz: number, r: number, dmg: number, cause: string): void;
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function place(p: ServerPlayer, x: number, z: number): void {
  Object.assign(p.st, createMoveState(x, 0, z));
}

function input(seq: number, buttons: number, yaw = 0, pitch = 0) {
  return { seq, moveX: 0, moveZ: 0, yaw, pitch, buttons, slot: 0 };
}

describe('melee (M6.2)', () => {
  it('one-hit kills in range with melee cause', () => {
    const room = new Room(MAPS.warehouse, { seed: 21, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(a, 12, 5);
    place(b, 12, 4);
    room.enqueueInput(a.id, input(1, BTN.MELEE, 0, 0));
    for (let i = 0; i < 10; i++) room.step();
    expect(b.alive).toBe(false);
    expect(a.kills).toBe(1);
    const events = room.drainEvents();
    expect(events.some((e) => e.type === 'melee' && e.victimId === b.id)).toBe(true);
    expect(events.some((e) => e.type === 'kill' && e.cause === 'melee')).toBe(true);
  });

  it('misses beyond range', () => {
    const room = new Room(MAPS.warehouse, { seed: 22, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(a, 12, 5);
    place(b, 12, 1);
    room.enqueueInput(a.id, input(1, BTN.MELEE, 0, 0));
    for (let i = 0; i < 10; i++) room.step();
    expect(b.alive).toBe(true);
    expect(room.drainEvents().some((e) => e.type === 'melee' && e.victimId === null)).toBe(true);
  });

  it('cooldown blocks an immediate second swing', () => {
    const room = new Room(MAPS.warehouse, { seed: 23, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(a, 12, 5);
    place(b, 12, 4);
    room.enqueueInput(a.id, input(1, BTN.MELEE, 0, 0));
    for (let i = 0; i < 10; i++) room.step();
    room.enqueueInput(a.id, input(2, 0, 0, 0));
    room.step();
    room.enqueueInput(a.id, input(3, BTN.MELEE, 0, 0));
    for (let i = 0; i < 8; i++) room.step();
    expect(room.drainEvents().filter((e) => e.type === 'melee').length).toBe(1);
  });
});

describe('grenade (M6.2)', () => {
  it('cook + release throws a live grenade and consumes equipment', () => {
    const room = new Room(MAPS.warehouse, { seed: 24, bots: 0 });
    const a = room.addPlayer('A', false);
    expect(a.lethal).toBe(1);
    room.enqueueInput(a.id, input(1, BTN.LETHAL, 0, 0));
    room.step();
    room.step();
    expect(a.cookingKind).toBe('frag');
    expect(a.lethal).toBe(0);
    room.enqueueInput(a.id, input(2, 0, 0, 0));
    room.step();
    expect(a.cookingKind).toBeNull();
    expect(room.drainEvents().some((e) => e.type === 'grenadeThrow')).toBe(true);
    const snap = room.snapshot();
    expect((snap.nades ?? []).length).toBe(1);
  });

  it('explosion damage kills with grenade cause', () => {
    const room = new Room(MAPS.warehouse, { seed: 25, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(a, 12, 6);
    place(b, 12, 4);
    (room as unknown as RoomInternals).explodeAt(a.id, b.st.x, b.st.y + b.st.height * 0.6, b.st.z, FRAG_RADIUS, FRAG_MAX_DMG, 'grenade');
    expect(b.alive).toBe(false);
    expect(a.kills).toBe(1);
    expect(room.drainEvents().some((e) => e.type === 'blast' && e.cause === 'grenade')).toBe(true);
  });

  it('holding too long detonates in hand (suicide)', () => {
    const room = new Room(MAPS.warehouse, { seed: 26, bots: 0 });
    const a = room.addPlayer('A', false);
    a.spawnProtUntil = 0;
    for (let i = 1; i <= FRAG_FUSE_TICKS + 5; i++) {
      room.enqueueInput(a.id, input(i, BTN.LETHAL, 0, 0));
      room.step();
    }
    expect(a.alive).toBe(false);
    expect(a.kills).toBe(0);
    expect(a.deaths).toBe(1);
    expect(room.drainEvents().some((e) => e.type === 'kill' && e.cause === 'suicide')).toBe(true);
  });

  it('equipment replenishes on respawn', () => {
    const room = new Room(MAPS.warehouse, { seed: 27, bots: 0 });
    const a = room.addPlayer('A', false);
    a.lethal = 0;
    a.tactical = 0;
    (room as unknown as RoomInternals).killPlayer(a, a, 'suicide', false);
    room.enqueueInput(a.id, input(1, 0, 0, 0));
    room.step();
    expect(a.alive).toBe(false);
    for (let i = 0; i < 91; i++) room.step();
    expect(a.alive).toBe(true);
    expect(a.lethal).toBe(1);
    expect(a.tactical).toBe(1);
  });
});

describe('flashbang (M6.2)', () => {
  it('blinds a facing player and wears off', () => {
    const room = new Room(MAPS.warehouse, { seed: 28, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    place(a, 12, 5);
    place(b, 12, 2);
    b.st.yaw = Math.PI;
    expect(a.tactical).toBe(1);
    (room as unknown as RoomInternals).throwNade(a, { yaw: 0, pitch: -1.2 }, 'flash', room.tick);
    for (let i = 0; i < 60 && !room.drainEvents().some((e) => e.type === 'flashPop'); i++) room.step();
    expect(room.isBlinded(b.id)).toBe(true);
    for (let i = 0; i < 100; i++) room.step();
    expect(room.isBlinded(b.id)).toBe(false);
  });
});
