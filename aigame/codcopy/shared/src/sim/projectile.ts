import { GRAVITY } from '../constants';
import type { AABB } from '../physics/aabb';

export interface ProjectileState {
  x: number;
  y: number;
  z: number;
  vx: number;
  vy: number;
  vz: number;
}

function insideExpanded(p: ProjectileState, boxes: AABB[], r: number): AABB | null {
  for (const b of boxes) {
    if (
      p.x > b.minX - r &&
      p.x < b.maxX + r &&
      p.y > b.minY - r &&
      p.y < b.maxY + r &&
      p.z > b.minZ - r &&
      p.z < b.maxZ + r
    ) {
      return b;
    }
  }
  return null;
}

/** 单步投掷物解算：重力 + 地面/AABB 反弹（恢复系数 bounce），轴分离防穿透。双端同码。 */
export function stepProjectile(p: ProjectileState, dt: number, boxes: AABB[], bounce: number, radius: number): void {
  p.vy += GRAVITY * dt;
  const step = (axis: 'x' | 'y' | 'z', delta: number): void => {
    if (delta === 0) return;
    const old = p[axis];
    p[axis] += delta;
    if (insideExpanded(p, boxes, radius)) {
      p[axis] = old;
      const v = 'v' + axis as `v${'x' | 'y' | 'z'}`;
      p[v] = -p[v] * bounce;
      if (axis === 'y') {
        p.vx *= 0.7;
        p.vz *= 0.7;
      }
    }
  };
  step('x', p.vx * dt);
  step('y', p.vy * dt);
  step('z', p.vz * dt);
  if (p.y < radius) {
    p.y = radius;
    if (p.vy < 0) p.vy = -p.vy * bounce;
    p.vx *= 0.7;
    p.vz *= 0.7;
  }
}
