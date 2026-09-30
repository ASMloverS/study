import { describe, expect, it } from 'vitest';
import { AIRSTRIKE_COUNT, AIRSTRIKE_DMG, AIRSTRIKE_RADIUS, CLUSTER_DMG, CLUSTER_RADIUS, MAPS, MAX_HEALTH } from 'shared';
import { Room } from '../src';
import type { ServerPlayer } from '../src/game/world';

type RoomInternals = {
  killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: string, hs: boolean): void;
};

function input(seq: number, streak: number, streakTarget?: { x: number; z: number }, streakYaw?: number) {
  return { seq, moveX: 0, moveZ: 0, yaw: 0, pitch: -1.0, buttons: 0, slot: 0, streak, streakTarget, streakYaw };
}

function setup(seed: number) {
  const room = new Room(MAPS.warehouse, { seed, bots: 0, killLimit: 100 });
  const a = room.addPlayer('A', false);
  const b = room.addPlayer('B', false);
  const c = room.addPlayer('C', false);
  const internals = room as unknown as RoomInternals;
  for (let i = 0; i < 5; i++) internals.killPlayer(a, b, 'ar', false);
  a.spawnProtUntil = 0;
  c.spawnProtUntil = 0;
  return { room, a, c };
}

/** [M16] 集装箱 pos [5,1.3,-2] size [2.4,2.6,6]：footprint x∈[3.8,6.2] z∈[-5,1]，顶面 y=2.6。
 * 落点 (5,-5)、heading 0 时 5 弹 z = -0.6/-2.8/-5.0/-7.2/-9.4，前 3 发落在箱顶。 */
describe('[M16] strike surface detonation', () => {
  it('bomblets on container footprint detonate at container top, killing players standing on it', () => {
    const { room, a, c } = setup(51);
    Object.assign(c.st, { x: 5, y: 2.6, z: -5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 5, z: -5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const blasts = room.drainEvents().filter((e) => e.type === 'blast' && e.cause === 'airstrike');
    expect(blasts.length).toBe(AIRSTRIKE_COUNT);
    let onTop = 0;
    let offTop = 0;
    for (const e of blasts) {
      if (e.type !== 'blast') continue;
      if (e.pos.z >= -5.001 && e.pos.z <= 1) {
        onTop++;
        expect(e.pos.y).toBeCloseTo(3.2, 1); // 集装箱顶 2.6 + 0.6
      } else {
        offTop++;
        expect(e.pos.y).toBeCloseTo(0.6, 1); // 开阔地面
      }
    }
    expect(onTop).toBe(3);
    expect(offTop).toBe(2);
    expect(c.alive).toBe(false);
    expect(a.kills).toBe(6);
  });

  it('player sheltered behind the container is shielded from top-surface blasts (LOS)', () => {
    const { room, a, c } = setup(52);
    Object.assign(c.st, { x: 7.5, y: 0, z: -5, vx: 0, vy: 0, vz: 0 });
    room.enqueueInput(a.id, input(1, 2, { x: 5, z: -5 }, 0));
    for (let i = 0; i < 130; i++) room.step();
    const evs = room.drainEvents();
    expect(c.alive).toBe(true);
    expect(evs.some((e) => e.type === 'kill' && e.victimId === c.id)).toBe(false);
  });

  it('direct-hit damage invariant: single blast kills a full-health standing player', () => {
    const chestGap = 1.8 * 0.6 - 0.6; // 站立胸口到爆心（surface+0.6）的垂直距离
    expect(AIRSTRIKE_DMG * (1 - chestGap / AIRSTRIKE_RADIUS)).toBeGreaterThanOrEqual(MAX_HEALTH);
    expect(CLUSTER_DMG * (1 - chestGap / CLUSTER_RADIUS)).toBeGreaterThanOrEqual(MAX_HEALTH);
  });
});
