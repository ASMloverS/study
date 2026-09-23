import type { AABB, Vec3 } from 'shared';
import { boxCenter } from 'shared';
import type { CoverDef, DynamicKind } from 'shared';

export interface DoorState {
  coverIndex: number;
  kind: 'door';
  center: Vec3;
  size: [number, number, number];
  open: number;
  targetOpen: number;
  triggerUntilTick: number;
  lastBlockedTick: number;
}

export interface LiftState {
  coverIndex: number;
  kind: 'lift';
  baseY: number;
  travel: number;
  phase: number;
  dir: 1 | -1;
}

export type DynamicState = DoorState | LiftState;

export interface DynamicCoverManager {
  states: DynamicState[];
  values(): number[];
  update(dt: number, tick: number, players: { x: number; z: number }[]): void;
  boxOf(s: DynamicState): AABB;
}

export const DOOR_HOLD_TICKS = 30;

export function createDynamicCovers(covers: CoverDef[]): DynamicCoverManager {
  const states: DynamicState[] = [];
  for (let i = 0; i < covers.length; i++) {
    const c = covers[i];
    if (!c.dynamic) continue;
    if (c.dynamic === 'door') {
      states.push({
        coverIndex: i,
        kind: 'door',
        center: { x: c.pos[0], y: c.pos[1], z: c.pos[2] },
        size: c.size,
        open: 0,
        targetOpen: 0,
        triggerUntilTick: 0,
        lastBlockedTick: 0,
      });
    } else {
      states.push({ coverIndex: i, kind: 'lift', baseY: c.pos[1], travel: 2, phase: 0, dir: 1 });
    }
  }
  const manager: DynamicCoverManager = {
    states,
    values() {
      return states.map((s) => (s.kind === 'door' ? round2(s.open) : round2(s.phase)));
    },
    update(dt, tick, players) {
      for (const s of states) {
        if (s.kind === 'door') {
          const near = players.some((p) => Math.hypot(p.x - s.center.x, p.z - s.center.z) < 2.2);
          if (near) s.triggerUntilTick = tick + DOOR_HOLD_TICKS;
          s.targetOpen = tick < s.triggerUntilTick ? 1 : 0;
          const speed = dt / 1.0;
          if (s.open < s.targetOpen) s.open = Math.min(s.targetOpen, s.open + speed);
          else if (s.open > s.targetOpen) s.open = Math.max(s.targetOpen, s.open - speed);
          if (s.open < 0.7) s.lastBlockedTick = tick;
        } else {
          s.phase += s.dir * (dt / 4);
          if (s.phase >= 1) {
            s.phase = 1;
            s.dir = -1;
          } else if (s.phase <= 0) {
            s.phase = 0;
            s.dir = 1;
          }
        }
      }
    },
    boxOf(s) {
      const c = covers[s.coverIndex];
      if (s.kind === 'door') {
        return boxCenter(s.center.x, s.center.y, s.center.z, s.size[0], s.size[1], s.size[2]);
      }
      return boxCenter(c.pos[0], s.baseY + s.phase * s.travel, c.pos[2], c.size[0], c.size[1], c.size[2]);
    },
  };
  return manager;
}

function round2(v: number): number {
  return Math.round(v * 100) / 100;
}

export function isDynamicKind(v: unknown): v is DynamicKind {
  return v === 'door' || v === 'lift';
}
