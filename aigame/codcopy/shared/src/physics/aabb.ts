export interface AABB {
  minX: number;
  minY: number;
  minZ: number;
  maxX: number;
  maxY: number;
  maxZ: number;
}

export function boxCenter(cx: number, cy: number, cz: number, sx: number, sy: number, sz: number): AABB {
  return {
    minX: cx - sx / 2,
    minY: cy - sy / 2,
    minZ: cz - sz / 2,
    maxX: cx + sx / 2,
    maxY: cy + sy / 2,
    maxZ: cz + sz / 2,
  };
}

export interface Body {
  x: number;
  y: number;
  z: number;
  height: number;
  radius: number;
}

export function bodyOverlaps(b: Body, o: AABB): boolean {
  return (
    b.x - b.radius < o.maxX &&
    b.x + b.radius > o.minX &&
    b.y < o.maxY &&
    b.y + b.height > o.minY &&
    b.z - b.radius < o.maxZ &&
    b.z + b.radius > o.minZ
  );
}

export type Axis = 'x' | 'y' | 'z';

export function moveBodyAxis(b: Body, axis: Axis, delta: number, obstacles: readonly AABB[]): boolean {
  if (delta === 0) return false;
  b[axis] += delta;
  let blocked = false;
  for (const o of obstacles) {
    if (!bodyOverlaps(b, o)) continue;
    blocked = true;
    if (axis === 'x') b.x = delta > 0 ? o.minX - b.radius : o.maxX + b.radius;
    else if (axis === 'z') b.z = delta > 0 ? o.minZ - b.radius : o.maxZ + b.radius;
    else b.y = delta > 0 ? o.minY - b.height : o.maxY;
  }
  return blocked;
}

/** [M16] 垂直采样 (x,z) 处最高表面（含掩体顶面，footprint 边界含端点），地面为 0；空袭/集束按此高度引爆 */
export function surfaceYAt(boxes: readonly AABB[], x: number, z: number): number {
  let top = 0;
  for (const b of boxes) {
    if (x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ && b.maxY > top) top = b.maxY;
  }
  return top;
}
