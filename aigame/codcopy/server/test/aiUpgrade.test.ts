import { describe, expect, it } from 'vitest';
import { BTN, MAPS } from 'shared';
import { Room } from '../src';
import { updateBot } from '../src/ai/controller';

describe('AI behavior upgrade (M6.4)', () => {
  it('bots ADS when engaging beyond 8m', () => {
    const room = new Room(MAPS.warehouse, { seed: 41, bots: 0, botDifficulty: 'hard' });
    const a = room.addPlayer('A', true, 'hard');
    const b = room.addPlayer('B', true, 'hard');
    Object.assign(a.st, { x: 12, z: 5, yaw: 0 });
    Object.assign(b.st, { x: 12, z: -10, yaw: Math.PI });
    for (let i = 0; i < 150; i++) {
      a.health = 10000;
      b.health = 10000;
      room.step();
    }
    const inp = updateBot(room, a);
    const dist = Math.hypot(b.st.x - a.st.x, b.st.z - a.st.z);
    if (dist > 8) {
      expect((inp.buttons & BTN.ADS) !== 0).toBe(true);
    }
  });

  it('bot with cover assignment crouches when hiding', () => {
    const room = new Room(MAPS.warehouse, { seed: 42, bots: 0, botDifficulty: 'hard' });
    const a = room.addPlayer('A', true, 'hard');
    room.step();
    const brain = a.bot!;
    brain.coverSpot = { x: a.st.x, z: a.st.z - 2 };
    brain.peekSpot = null;
    brain.moveGoal = brain.coverSpot;
    brain.peeking = false;
    brain.lastDecisionTick = room.tick;
    brain.targetId = null;
    a.st.onGround = true;
    const inp = updateBot(room, a);
    expect((inp.buttons & BTN.CROUCH) !== 0).toBe(true);
  });
});
