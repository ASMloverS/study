import { describe, expect, it } from 'vitest';
import { BTN, createMoveState, eyeY } from 'shared';
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

function place(room: Room, id: number, x: number, z: number, y = 0) {
  const p = room.players.find((q) => q.id === id)!;
  Object.assign(p.st, createMoveState(x, y, z));
}

describe('destructible covers', () => {
  it('crate breaks after enough shots and becomes passable', () => {
    const room = new Room(undefined, { seed: 11, bots: 0 });
    const a = room.addPlayer('A', false);
    place(room, a.id, 17, -9);
    const eye = () => ({ x: a.st.x, y: eyeY(a.st), z: a.st.z });
    let aim = aimAt(eye(), { x: 14, y: 1.8, z: -9 });
    let destroyedCount = () => room.destructibles.filter((d) => d.kind === 'crate' && d.destroyed).length;
    let seq = 0;
    while (destroyedCount() < 1) {
      room.enqueueInput(a.id, aimInput(++seq, aim.yaw, aim.pitch));
      room.step();
    }
    aim = aimAt(eye(), { x: 14, y: 0.6, z: -9 });
    while (destroyedCount() < 2) {
      room.enqueueInput(a.id, aimInput(++seq, aim.yaw, aim.pitch));
      room.step();
    }
    expect(room.drainEvents().filter((e) => e.type === 'coverBreak').length).toBe(2);
    for (let i = 0; i < 60; i++) room.step();
    for (let i = 0; i < 60; i++) {
      room.enqueueInput(a.id, { seq: ++seq, moveX: 0, moveZ: 1, yaw: Math.PI / 2, pitch: 0, buttons: BTN.SPRINT, slot: 0 });
      room.step();
    }
    expect(a.st.x).toBeLessThan(12.5);
  });

  it('barrel explodes with AOE damage and chains to nearby barrel', () => {
    const room = new Room(undefined, { seed: 12, bots: 0 });
    const a = room.addPlayer('A', false);
    const victim = room.addPlayer('V', false);
    place(room, a.id, 13.4, 3);
    place(room, victim.id, 14.5, -0.5);
    const aim = aimAt({ x: a.st.x, y: eyeY(a.st), z: a.st.z }, { x: 13, y: 0.6, z: -3 });
    let exploded = false;
    for (let i = 0; i < 90 && !exploded; i++) {
      room.enqueueInput(a.id, aimInput(i + 1, aim.yaw, aim.pitch));
      room.step();
      exploded = room.destructibles.filter((d) => d.kind === 'barrel' && d.destroyed && Math.abs(d.center.x - 13) < 0.5).length > 0;
    }
    for (let i = 0; i < 30; i++) room.step();
    const events = room.drainEvents();
    const explodes = events.filter((e) => e.type === 'explode');
    expect(explodes.length).toBeGreaterThanOrEqual(2);
    const barrelStates = room.destructibles.filter((d) => d.kind === 'barrel');
    expect(barrelStates.filter((d) => d.center.x > 12 && d.center.x < 15 && d.center.z > -5 && d.center.z < -1).every((d) => d.destroyed)).toBe(true);
    expect(victim.health).toBeLessThan(100);
    expect(victim.alive).toBe(true);
  });
});

describe('dynamic covers', () => {
  it('door opens when a player is near and stops blocking shots', () => {
    const room = new Room(undefined, { seed: 13, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    place(room, a.id, 1.6, 11.5);
    place(room, b.id, 1.6, -11.5);
    expect(room.blocked(1.6, 1.5, 11.5, 0, -1, 20)).toBe(true);
    for (let i = 0; i < 45; i++) room.step();
    const doors = room.dynamic.states.filter((s) => s.kind === 'door');
    expect(doors.every((d) => (d as any).open > 0.9)).toBe(true);
    expect(room.blocked(1.6, 1.5, 11.5, 0, -1, 20)).toBe(false);
    const eye = { x: a.st.x, y: eyeY(a.st), z: a.st.z };
    const chest = { x: b.st.x, y: b.st.y + 1.1, z: b.st.z };
    const aim = aimAt(eye, chest);
    let killed = false;
    for (let i = 0; i < 60 && !killed; i++) {
      room.enqueueInput(a.id, aimInput(i + 1, aim.yaw, aim.pitch));
      room.step();
      killed = !b.alive;
    }
    expect(killed).toBe(true);
  });

  it('lift cycles and carries a standing player upward', () => {
    const room = new Room(undefined, { seed: 14, bots: 0 });
    const p = room.addPlayer('P', false);
    place(room, p.id, 0, 0, 1.1);
    for (let i = 0; i < 55; i++) room.step();
    expect(p.st.y).toBeGreaterThan(1.8);
    const lift = room.dynamic.states.find((s) => s.kind === 'lift') as { phase: number };
    expect(lift.phase).toBeGreaterThan(0.4);
  });

  it('snapshot includes destroyed covers, dyn values and stays small', () => {
    const room = new Room(undefined, { seed: 15, bots: 8 });
    const snap = room.snapshot();
    expect(snap.destroyed).toBeDefined();
    expect(snap.dyn.length).toBe(3);
    const size = JSON.stringify(snap).length;
    expect(size).toBeLessThan(6200);
  });
});
