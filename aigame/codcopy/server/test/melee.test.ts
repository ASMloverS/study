import { describe, expect, it } from 'vitest';
import { BTN, WEAPONS } from 'shared';
import { Room } from '../src';

describe('weapon switch', () => {
  it('switches to slot 3 (sr) after switch time', () => {
    const room = new Room(undefined, { seed: 5, bots: 0 });
    const p = room.addPlayer('H', false);
    expect(p.weapon).toBe('ar');
    room.enqueueInput(p.id, { seq: 1, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons: 0, slot: 3 });
    room.step();
    expect(p.weapon).toBe('ar');
    for (let i = 2; i <= 16; i++) {
      room.enqueueInput(p.id, { seq: i, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons: 0, slot: 3 });
      room.step();
    }
    expect(p.weapon).toBe('sr');
    expect(p.mags.sr).toBe(WEAPONS.sr.magSize);
    expect(p.mags.ar).toBe(WEAPONS.ar.magSize);
  });

  it('cannot fire while switching', () => {
    const room = new Room(undefined, { seed: 5, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const before = b.health;
    const fireAt = (slot: number, seq: number) =>
      room.enqueueInput(a.id, { seq, moveX: 0, moveZ: 0, yaw: 0, pitch: -1.4, buttons: BTN.FIRE | BTN.ADS, slot });
    fireAt(2, 1);
    room.step();
    fireAt(0, 2);
    room.step();
    expect(b.health).toBe(before);
    expect(a.shotsFired).toBe(0);
    for (let i = 3; i <= 16; i++) {
      fireAt(0, i);
      room.step();
    }
    expect(a.weapon).toBe('sg');
  });
});

describe('regen', () => {
  it('heals after 5s without damage', () => {
    const room = new Room(undefined, { seed: 9, bots: 0 });
    const p = room.addPlayer('H', false);
    p.health = 40;
    p.lastDamagedTick = room.tick;
    for (let i = 0; i < 100; i++) room.step();
    expect(p.health).toBe(40);
    for (let i = 0; i < 160; i++) room.step();
    expect(p.health).toBeGreaterThan(40);
  });
});

describe('8-player melee long run', () => {
  it('bots fight, kill, and produce valid standings within a full match', () => {
    const room = new Room(undefined, { seed: 2026, bots: 8, botDifficulty: 'mixed' });
    const events: import('shared').GameEvent[] = [];
    const totalTicks = Math.ceil(600 * 30);
    for (let i = 0; i < totalTicks && !room.over; i++) {
      room.step();
      events.push(...room.drainEvents());
    }
    events.push(...room.drainEvents());
    expect(room.over).toBe(true);
    const kills = events.filter((e) => e.type === 'kill');
    expect(kills.length).toBeGreaterThan(10);
    for (const p of room.players) {
      expect(Number.isFinite(p.st.x)).toBe(true);
      expect(Math.abs(p.st.x)).toBeLessThanOrEqual(24);
      expect(Math.abs(p.st.z)).toBeLessThanOrEqual(24);
    }
    const over = events.find((e) => e.type === 'gameOver') as Extract<import('shared').GameEvent, { type: 'gameOver' }>;
    expect(over).toBeDefined();
    expect(over.standings.length).toBe(8);
    expect(over.standings[0].k).toBeGreaterThanOrEqual(over.standings[7].k);
    const totalKills = over.standings.reduce((s, x) => s + x.k, 0);
    expect(totalKills).toBe(kills.length);
    const shooter = room.players.find((p) => p.shotsFired > 0);
    expect(shooter).toBeDefined();
  });
});
