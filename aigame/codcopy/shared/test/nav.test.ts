import { describe, expect, it } from 'vitest';
import { MAPS, boxCenter, mapToObstacles } from '../src';
import { bakeNavGrid, isWalkable, worldToCell } from '../src/nav/grid';
import { findPath, gridLineOfSight } from '../src/nav/astar';
import { makeGrid } from '../src/nav/grid';

describe('bakeNavGrid', () => {
  it('marks tall obstacle cells blocked and spawn cells walkable', () => {
    const g = bakeNavGrid(MAPS.warehouse.size, mapToObstacles(MAPS.warehouse));
    const wall = MAPS.warehouse.covers[0];
    const c1 = worldToCell(g, wall.pos[0], wall.pos[2]);
    expect(isWalkable(g, c1.cx, c1.cz)).toBe(false);
    const spawn = MAPS.warehouse.spawns[0];
    const c2 = worldToCell(g, spawn[0], spawn[2]);
    expect(isWalkable(g, c2.cx, c2.cz)).toBe(true);
  });

  it('low covers below eye height do not block nav', () => {
    const low = boxCenter(0, 0.55, 0, 3, 1.1, 3);
    const g = bakeNavGrid(20, [low]);
    const c = worldToCell(g, 0, 0);
    expect(isWalkable(g, c.cx, c.cz)).toBe(true);
  });
});

describe('findPath', () => {
  it('returns near-straight path on empty grid', () => {
    const g = makeGrid(20, 20, []);
    const path = findPath(g, { x: 0, z: 0 }, { x: 5, z: 5 })!;
    expect(path).not.toBeNull();
    expect(path.length).toBeLessThanOrEqual(4);
    const last = path[path.length - 1];
    expect(last.x).toBeCloseTo(5, 3);
    expect(last.z).toBeCloseTo(5, 3);
  });

  it('routes around a wall', () => {
    const blocked: string[] = [];
    for (let i = 0; i <= 12; i++) blocked.push(`${10},${i}`);
    const g = makeGrid(20, 20, blocked);
    expect(gridLineOfSight(g, 0.25, 0.25, 9.25, 0.25)).toBe(false);
    const path = findPath(g, { x: 0.25, z: 0.25 }, { x: 9.25, z: 0.25 })!;
    expect(path).not.toBeNull();
    for (let i = 1; i < path.length - 1; i++) {
      const c = worldToCell(g, path[i].x, path[i].z);
      expect(isWalkable(g, c.cx, c.cz)).toBe(true);
    }
    const end = path[path.length - 1];
    expect(end.x).toBeCloseTo(9.25, 3);
  });

  it('returns null when goal unreachable', () => {
    const blocked: string[] = [];
    for (let i = 0; i < 20; i++) {
      blocked.push(`${8},${i}`, `${9},${i}`, `${10},${i}`);
    }
    const g = makeGrid(20, 20, blocked);
    const path = findPath(g, { x: 0.25, z: 0.25 }, { x: 9.25, z: 9.25 });
    expect(path).toBeNull();
  });

  it('paths across the warehouse map between spawns', () => {
    const g = bakeNavGrid(MAPS.warehouse.size, mapToObstacles(MAPS.warehouse));
    const spawns = MAPS.warehouse.spawns;
    const path = findPath(g, { x: spawns[0][0], z: spawns[0][2] }, { x: spawns[3][0], z: spawns[3][2] });
    expect(path).not.toBeNull();
    expect(path!.length).toBeGreaterThan(3);
    for (const p of path!) {
      const c = worldToCell(g, p.x, p.z);
      expect(isWalkable(g, c.cx, c.cz)).toBe(true);
    }
  });
});
