import { describe, expect, it } from 'vitest';
import { BTN, type GameEvent } from 'shared';
import { Room } from '../src';

function aimAt(from: { x: number; y: number; z: number }, to: { x: number; y: number; z: number }) {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const dz = to.z - from.z;
  return { yaw: Math.atan2(-dx, -dz), pitch: Math.atan2(dy, Math.hypot(dx, dz)) };
}

function eyeOf(p: { st: { x: number; y: number; z: number; height: number } }) {
  return { x: p.st.x, y: p.st.y + p.st.height * 0.9, z: p.st.z };
}

describe('lag compensation', () => {
  it('rewinds victims by attacker RTT so delayed shots still connect', () => {
    const room = new Room(undefined, { seed: 11, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    a.st.x = 12;
    a.st.z = 5;
    b.st.x = 12;
    b.st.z = -5;
    room.setRtt(a.id, 100);
    for (let i = 0; i < 12; i++) room.step();
    const oldPos = { x: b.st.x, y: b.st.y + b.st.height * 0.5, z: b.st.z };
    b.st.x = 14;
    room.step();
    const aim = aimAt(eyeOf(a), oldPos);
    const events: GameEvent[] = [];
    room.enqueueInput(a.id, {
      seq: 1,
      moveX: 0,
      moveZ: 0,
      yaw: aim.yaw,
      pitch: aim.pitch,
      buttons: BTN.FIRE | BTN.ADS,
      slot: 0,
    });
    room.step();
    events.push(...room.drainEvents());
    const hits = events.filter((e) => e.type === 'hit');
    expect(hits.length).toBeGreaterThan(0);
    expect(b.health).toBeLessThan(100);
  });

  it('without RTT the same delayed shot misses the old position', () => {
    const room = new Room(undefined, { seed: 12, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    a.st.x = 12;
    a.st.z = 5;
    b.st.x = 12;
    b.st.z = -5;
    for (let i = 0; i < 12; i++) room.step();
    const oldPos = { x: b.st.x, y: b.st.y + b.st.height * 0.5, z: b.st.z };
    b.st.x = 14;
    room.step();
    const aim = aimAt(eyeOf(a), oldPos);
    const events: GameEvent[] = [];
    room.enqueueInput(a.id, {
      seq: 1,
      moveX: 0,
      moveZ: 0,
      yaw: aim.yaw,
      pitch: aim.pitch,
      buttons: BTN.FIRE | BTN.ADS,
      slot: 0,
    });
    room.step();
    events.push(...room.drainEvents());
    const hits = events.filter((e) => e.type === 'hit');
    expect(hits.length).toBe(0);
    expect(b.health).toBe(100);
  });

  it('rewind ticks are clamped by LAG_COMP_MAX_TICKS', () => {
    const room = new Room(undefined, { seed: 13, bots: 0 });
    const a = room.addPlayer('A', false);
    room.setRtt(a.id, 5000);
    expect((room as unknown as { rewindTicksFor(id: number): number }).rewindTicksFor(a.id)).toBe(8);
  });
});
