import type { AABB } from '../physics/aabb';
import { boxCenter } from '../physics/aabb';

export type CoverType = 'wall' | 'lowwall' | 'container' | 'crate' | 'barrel';

export type DynamicKind = 'door' | 'lift';

export interface CoverDef {
  type: CoverType;
  pos: [number, number, number];
  size: [number, number, number];
  destructible?: boolean;
  hp?: number;
  dynamic?: DynamicKind;
}

export interface MapDef {
  name: string;
  size: number;
  covers: CoverDef[];
  spawns: [number, number, number][];
  interestPoints: [number, number, number][];
}

export function coverToAABB(c: CoverDef): AABB {
  return boxCenter(c.pos[0], c.pos[1], c.pos[2], c.size[0], c.size[1], c.size[2]);
}

export function mapToObstacles(m: MapDef): AABB[] {
  return m.covers.map(coverToAABB);
}

export function mapToNavObstacles(m: MapDef): AABB[] {
  return m.covers.filter((c) => !c.dynamic).map(coverToAABB);
}
