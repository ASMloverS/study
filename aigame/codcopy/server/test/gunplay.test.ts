import { describe, expect, it } from 'vitest';
import { BTN, WEAPONS, type GameEvent } from 'shared';
import { Room } from '../src';

function hold(room: Room, id: number, seqStart: number, buttons: number, ticks: number, slot = 0, moveZ = 0): number {
  for (let i = 0; i < ticks; i++) {
    room.enqueueInput(id, { seq: seqStart + i, moveX: 0, moveZ, yaw: 0, pitch: 0, buttons, slot });
    room.step();
  }
  return seqStart + ticks;
}

describe('reserve ammo and reload tiers', () => {
  it('tactical reload takes part of reserve; empty reload is 0.6s longer', () => {
    const room = new Room(undefined, { seed: 3, bots: 0 });
    const p = room.addPlayer('H', false);
    p.mags.ar = 10;
    let seq = hold(room, p.id, 1, BTN.RELOAD, 1);
    expect(p.reloadEndsTick - room.tick).toBe(Math.round(WEAPONS.ar.reloadTime * 30));
    for (let i = 0; i < 61; i++) room.step();
    expect(p.mags.ar).toBe(WEAPONS.ar.magSize);
    expect(p.reserve.ar).toBe(WEAPONS.ar.reserve - 20);

    p.mags.ar = 0;
    seq = hold(room, p.id, seq, BTN.RELOAD, 1);
    expect(p.reloadEndsTick - room.tick).toBe(Math.round((WEAPONS.ar.reloadTime + 0.6) * 30));
    void seq;
  });

  it('auto-reloads on empty trigger and dry-fires nothing when reserve is out', () => {
    const room = new Room(undefined, { seed: 4, bots: 0 });
    const p = room.addPlayer('H', false);
    p.mags.ar = 0;
    p.reserve.ar = 10;
    hold(room, p.id, 1, BTN.FIRE | BTN.ADS, 1);
    expect(p.reloadEndsTick).toBeGreaterThan(0);
    hold(room, p.id, 2, 0, 80);
    expect(p.mags.ar).toBe(10);
    expect(p.shotsFired).toBe(0);

    p.mags.ar = 0;
    p.reserve.ar = 0;
    hold(room, p.id, 100, BTN.FIRE | BTN.ADS, 10);
    expect(p.reloadEndsTick).toBe(-1);
    expect(p.shotsFired).toBe(0);
  });
});

describe('semi-auto SR', () => {
  it('fires once per trigger press even when held', () => {
    const room = new Room(undefined, { seed: 5, bots: 0 });
    const p = room.addPlayer('H', false);
    p.weapon = 'sr';
    let seq = hold(room, p.id, 1, BTN.FIRE | BTN.ADS, 60);
    expect(p.shotsFired).toBe(1);
    seq = hold(room, p.id, seq, 0, 3);
    seq = hold(room, p.id, seq, BTN.FIRE | BTN.ADS, 3);
    expect(p.shotsFired).toBe(2);
    void seq;
  });
});

describe('sprint-out fire gate', () => {
  it('cannot fire during sprint-out window, fires shortly after', () => {
    const room = new Room(undefined, { seed: 6, bots: 0 });
    const p = room.addPlayer('H', false);
    hold(room, p.id, 1, BTN.SPRINT | BTN.FIRE, 1, 1, 1);
    expect(p.shotsFired).toBe(0);
    expect(p.st.sprintLockT).toBeGreaterThan(0);
    hold(room, p.id, 3, BTN.FIRE | BTN.ADS, 4, 0, 1);
    expect(p.shotsFired).toBe(0);
    hold(room, p.id, 7, BTN.FIRE | BTN.ADS, 8, 0, 1);
    expect(p.shotsFired).toBeGreaterThan(0);
  });
});

describe('slide fire', () => {
  it('can fire while sliding', () => {
    const room = new Room(undefined, { seed: 8, bots: 0 });
    const p = room.addPlayer('H', false);
    hold(room, p.id, 1, BTN.SPRINT, 10, 0, 1);
    hold(room, p.id, 11, BTN.SPRINT | BTN.CROUCH, 1, 0, 1);
    expect(p.st.sliding).toBe(true);
    hold(room, p.id, 12, BTN.SPRINT | BTN.CROUCH | BTN.FIRE, 5, 0, 1);
    expect(p.shotsFired).toBeGreaterThan(0);
  });
});

describe('damage falloff and recoil in world', () => {
  it('AR damage shrinks with distance', () => {
    const room = new Room(undefined, { seed: 7, bots: 0 });
    const a = room.addPlayer('A', false);
    const b = room.addPlayer('B', false);
    const setPos = (id: number, x: number, z: number) => {
      const q = room.players.find((r) => r.id === id)!;
      q.st.x = x;
      q.st.z = z;
    };
    setPos(a.id, 20, 8);
    setPos(b.id, 20, -8);
    const dx = 0;
    const dz = -16;
    const dy = b.st.y + b.st.height * 0.5 - (a.st.y + a.st.height * 0.9);
    const yaw = Math.atan2(-dx, -dz);
    const pitch = Math.atan2(dy, Math.hypot(dx, dz));
    const events: GameEvent[] = [];
    for (let i = 1; i <= 10; i++) {
      room.enqueueInput(a.id, { seq: i, moveX: 0, moveZ: 0, yaw, pitch, buttons: BTN.FIRE | BTN.ADS, slot: 0 });
      room.step();
      events.push(...room.drainEvents());
    }
    const hits = events.filter((e) => e.type === 'hit') as Extract<GameEvent, { type: 'hit' }>[];
    expect(hits.length).toBeGreaterThan(0);
    expect(hits[0].damage).toBeLessThan(WEAPONS.ar.damage);
    expect(hits[0].damage).toBeGreaterThan(WEAPONS.ar.damage * 0.85);
  });
});
