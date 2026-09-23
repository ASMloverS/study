import type { CoverDef, Vec3 } from 'shared';

export interface DestructibleState {
  coverIndex: number;
  hp: number;
  destroyed: boolean;
  kind: 'crate' | 'barrel';
  center: Vec3;
}

export const BARREL_BLAST_RADIUS = 3;
export const BARREL_MAX_DAMAGE = 60;
export const CHAIN_DELAY_TICKS = 9;

export function barrelDamage(dist: number): number {
  return dist >= BARREL_BLAST_RADIUS ? 0 : BARREL_MAX_DAMAGE * (1 - dist / BARREL_BLAST_RADIUS);
}

export function createDestructibles(covers: CoverDef[]): DestructibleState[] {
  const list: DestructibleState[] = [];
  for (let i = 0; i < covers.length; i++) {
    const c = covers[i];
    if (!c.destructible) continue;
    list.push({
      coverIndex: i,
      hp: c.hp ?? 100,
      destroyed: false,
      kind: c.type === 'barrel' ? 'barrel' : 'crate',
      center: { x: c.pos[0], y: c.pos[1], z: c.pos[2] },
    });
  }
  return list;
}

export function findBarrelNear(
  destructibles: DestructibleState[],
  coverBoxes: (object | null)[],
  x: number,
  z: number,
  radius: number,
): number | null {
  let best: number | null = null;
  let bestD = radius;
  for (const d of destructibles) {
    if (d.destroyed || d.kind !== 'barrel') continue;
    if (!coverBoxes[d.coverIndex]) continue;
    const dist = Math.hypot(d.center.x - x, d.center.z - z);
    if (dist < bestD) {
      bestD = dist;
      best = d.coverIndex;
    }
  }
  return best;
}
