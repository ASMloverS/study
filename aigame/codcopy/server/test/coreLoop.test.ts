import { describe, expect, it } from 'vitest';
import { BTN, MAPS, createMoveState, eyeY } from 'shared';
import { Room } from '../src';

function aimInput(seq: number, yaw: number, pitch: number, buttons = BTN.FIRE | BTN.ADS) {
  return { seq, moveX: 0, moveZ: 0, yaw, pitch, buttons, slot: 0 };
}

function aimAt(from: { x: number; y: number; z: number }, to: { x: number; y: number; z: number }) {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const dz = to.z - from.z;
  return { yaw: Math.atan2(-dx, -dz), pitch: Math.atan2(dy, Math.hypot(dx, dz)) };
}

function place(room: Room, id: number, x: number, z: number) {
  const p = room.players.find((q) => q.id === id)!;
  Object.assign(p.st, createMoveState(x, 0, z));
}

describe('Room core loop', () => {
  it('body shots kill and victim respawns after 3s', () => {
    const room = new Room(MAPS.warehouse, { seed: 42, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(room, a.id, 12, 5);
    place(room, b.id, 12, -5);
    const eye = { x: a.st.x, y: eyeY(a.st), z: a.st.z };
    const chest = { x: b.st.x, y: b.st.y + b.st.height * 0.5, z: b.st.z };
    const aim = aimAt(eye, chest);

    let killed = false;
    for (let i = 0; i < 60 && !killed; i++) {
      room.enqueueInput(a.id, aimInput(i + 1, aim.yaw, aim.pitch));
      room.step();
      killed = !b.alive;
    }
    expect(killed).toBe(true);
    expect(a.kills).toBe(1);
    expect(b.deaths).toBe(1);
    const kills = room.drainEvents().filter((e) => e.type === 'kill');
    expect(kills.length).toBe(1);

    for (let i = 0; i < 91; i++) room.step();
    expect(b.alive).toBe(true);
    expect(b.health).toBe(100);
  });

  it('headshots multiply damage', () => {
    const room = new Room(MAPS.warehouse, { seed: 7, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    b.spawnProtUntil = 0;
    place(room, a.id, 2.5, 5);
    place(room, b.id, 2.5, -5);
    const eye = { x: a.st.x, y: eyeY(a.st), z: a.st.z };
    const head = { x: b.st.x, y: b.st.y + b.st.height - 0.175, z: b.st.z };
    const aim = aimAt(eye, head);

    let killed = false;
    for (let i = 0; i < 60 && !killed; i++) {
      room.enqueueInput(a.id, aimInput(i + 1, aim.yaw, aim.pitch));
      room.step();
      killed = !b.alive;
    }
    expect(killed).toBe(true);
    const hits = room.drainEvents().filter((e) => e.type === 'hit') as Extract<
      import('shared').GameEvent,
      { type: 'hit' }
    >[];
    expect(hits.length).toBeLessThanOrEqual(3);
    expect(hits.some((e) => e.part === 'head')).toBe(true);
  });

  it('bots move and stay in bounds', () => {
    const room = new Room(MAPS.warehouse, { seed: 1, bots: 2 });
    const start = room.players.map((p) => ({ x: p.st.x, z: p.st.z }));
    for (let i = 0; i < 600; i++) room.step();
    const moved = room.players.some((p, i) => Math.hypot(p.st.x - start[i].x, p.st.z - start[i].z) > 2);
    expect(moved).toBe(true);
    for (const p of room.players) {
      expect(Number.isFinite(p.st.x)).toBe(true);
      expect(Math.abs(p.st.x)).toBeLessThanOrEqual(24);
      expect(Math.abs(p.st.z)).toBeLessThanOrEqual(24);
    }
  });

  it('snapshot contains acks and players', () => {
    const room = new Room(MAPS.warehouse, { seed: 3, bots: 1 });
    const human = room.addPlayer('H', false);
    const snap = room.snapshot();
    expect(snap.kind).toBe('snapshot');
    expect(snap.players.length).toBe(2);
    expect(snap.acks[human.id]).toBe(0);
  });
});
