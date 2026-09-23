import { MEMORY_DECAY_TICKS, SOUND_SHOT_RADIUS, SOUND_STEP_RADIUS, VIEW_DISTANCE } from 'shared';
import type { Room, ServerPlayer } from '../game/world';

export interface SoundEvent {
  x: number;
  z: number;
  sourceId: number;
  kind: 'shot' | 'step';
}

export interface TargetMemory {
  id: number;
  x: number;
  y: number;
  z: number;
  tick: number;
}

export interface PerceptionState {
  memories: Map<number, TargetMemory>;
}

export function createPerception(): PerceptionState {
  return { memories: new Map() };
}

export function updatePerception(room: Room, p: ServerPlayer, st: PerceptionState, sounds: readonly SoundEvent[]): void {
  for (const [id, m] of st.memories) {
    if (room.tick - m.tick > MEMORY_DECAY_TICKS) st.memories.delete(id);
  }
  const fx = -Math.sin(p.st.yaw);
  const fz = -Math.cos(p.st.yaw);
  for (const q of room.players) {
    if (q === p || !q.alive) continue;
    const dx = q.st.x - p.st.x;
    const dz = q.st.z - p.st.z;
    const d = Math.hypot(dx, dz);
    if (d > VIEW_DISTANCE) continue;
    if (d > 3 && (dx / d) * fx + (dz / d) * fz < 0.574) continue;
    if (!room.hasLineOfSight(p, q)) continue;
    st.memories.set(q.id, { id: q.id, x: q.st.x, y: q.st.y, z: q.st.z, tick: room.tick });
  }
  for (const s of sounds) {
    if (s.sourceId === p.id) continue;
    const radius = s.kind === 'shot' ? SOUND_SHOT_RADIUS : SOUND_STEP_RADIUS;
    if (Math.hypot(s.x - p.st.x, s.z - p.st.z) > radius) continue;
    const existing = st.memories.get(s.sourceId);
    if (!existing || room.tick - existing.tick > 30) {
      st.memories.set(s.sourceId, { id: s.sourceId, x: s.x, y: 0, z: s.z, tick: room.tick });
    }
  }
}
