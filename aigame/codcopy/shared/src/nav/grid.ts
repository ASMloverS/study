import type { AABB } from '../physics/aabb';

export interface NavGrid {
  cell: number;
  width: number;
  height: number;
  originX: number;
  originZ: number;
  walkable: Uint8Array;
}

export function makeGrid(width: number, height: number, blocked: Iterable<string>, cell = 0.5): NavGrid {
  const g: NavGrid = {
    cell,
    width,
    height,
    originX: 0,
    originZ: 0,
    walkable: new Uint8Array(width * height).fill(1),
  };
  for (const key of blocked) {
    const [cx, cz] = key.split(',').map(Number);
    g.walkable[cz * width + cx] = 0;
  }
  return g;
}

export function bakeNavGrid(size: number, obstacles: readonly AABB[], inflation = 0.45, cell = 0.5): NavGrid {
  const width = Math.round(size / cell);
  const height = width;
  const half = size / 2;
  const g: NavGrid = {
    cell,
    width,
    height,
    originX: -half + cell / 2,
    originZ: -half + cell / 2,
    walkable: new Uint8Array(width * height).fill(1),
  };
  for (let cz = 0; cz < height; cz++) {
    for (let cx = 0; cx < width; cx++) {
      const wx = g.originX + cx * cell;
      const wz = g.originZ + cz * cell;
      for (const o of obstacles) {
        if (
          o.minY < 0.5 &&
          o.maxY > 1.6 &&
          wx > o.minX - inflation &&
          wx < o.maxX + inflation &&
          wz > o.minZ - inflation &&
          wz < o.maxZ + inflation
        ) {
          g.walkable[cz * width + cx] = 0;
          break;
        }
      }
    }
  }
  return g;
}

export function worldToCell(g: NavGrid, x: number, z: number): { cx: number; cz: number } {
  const cx = Math.max(0, Math.min(g.width - 1, Math.round((x - g.originX) / g.cell)));
  const cz = Math.max(0, Math.min(g.height - 1, Math.round((z - g.originZ) / g.cell)));
  return { cx, cz };
}

export function cellToWorld(g: NavGrid, cx: number, cz: number): { x: number; z: number } {
  return { x: g.originX + cx * g.cell, z: g.originZ + cz * g.cell };
}

export function isWalkable(g: NavGrid, cx: number, cz: number): boolean {
  if (cx < 0 || cz < 0 || cx >= g.width || cz >= g.height) return false;
  return g.walkable[cz * g.width + cx] === 1;
}
