import { describe, expect, it } from 'vitest';
import { aiCommand } from './ai';
import type { RiderState, RiderType } from './types';

function rider(over: Partial<RiderState>, type: RiderType = { label: 't', ftp: 300, maxEnergy: 24000, sprintDist: 250, aggression: 0.5 }): RiderState {
  return { id: 1, name: 'ai', isPlayer: false, type, dist: 1000, lateral: 0, speed: 9, energy: 20000, gear: 1, power: 300, powerSum: 0, timeSum: 0, finishTime: null, wanderTarget: 0, ...over };
}

describe('aiCommand', () => {
  it('sprints inside sprint distance with energy', () => {
    expect(aiCommand(rider({ energy: 12000 }), null, 200, 0).gear).toBe(3);
  });
  it('no sprint and conserves when nearly empty', () => {
    expect(aiCommand(rider({ energy: 2000 }), null, 200, 0).gear).toBe(0);
  });
  it('responds with hard gear when wheel ahead surges', () => {
    const self = rider({ speed: 9 });
    const ahead = rider({ id: 0, dist: 1010, speed: 10.5 });
    expect(aiCommand(self, ahead, 3000, 0).gear).toBe(2);
  });
  it('climber attacks on steep gradient with fresh legs', () => {
    const climber: RiderType = { label: 'climber', ftp: 315, maxEnergy: 22000, sprintDist: 120, aggression: 0.85 };
    expect(aiCommand(rider({ energy: 20000 }, climber), null, 3000, 0.05).gear).toBe(2);
  });
  it('conserves below 25% energy', () => {
    expect(aiCommand(rider({ energy: 5000 }), null, 3000, 0).gear).toBe(0);
  });
  it('steers toward wheel ahead', () => {
    const self = rider({ lateral: 0 });
    const ahead = rider({ id: 0, dist: 1010, lateral: 1.5 });
    expect(aiCommand(self, ahead, 3000, 0).steer).toBe(1);
  });
});
