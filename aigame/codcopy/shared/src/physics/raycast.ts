import type { AABB } from './aabb';

export interface RayHitBox {
  t: number;
  box: AABB;
}

export function rayBox(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  b: AABB,
  maxDist: number,
): number | null {
  let tmin = 0;
  let tmax = maxDist;
  const o = [ox, oy, oz];
  const d = [dx, dy, dz];
  const bmin = [b.minX, b.minY, b.minZ];
  const bmax = [b.maxX, b.maxY, b.maxZ];
  for (let i = 0; i < 3; i++) {
    if (Math.abs(d[i]) < 1e-9) {
      if (o[i] < bmin[i] || o[i] > bmax[i]) return null;
    } else {
      let t1 = (bmin[i] - o[i]) / d[i];
      let t2 = (bmax[i] - o[i]) / d[i];
      if (t1 > t2) {
        const tmp = t1;
        t1 = t2;
        t2 = tmp;
      }
      if (t1 > tmin) tmin = t1;
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return null;
    }
  }
  return tmin;
}

export function raycastBoxes(
  ox: number,
  oy: number,
  oz: number,
  dx: number,
  dy: number,
  dz: number,
  maxDist: number,
  boxes: readonly AABB[],
): RayHitBox | null {
  let best: RayHitBox | null = null;
  for (const b of boxes) {
    const t = rayBox(ox, oy, oz, dx, dy, dz, b, maxDist);
    if (t !== null && (best === null || t < best.t)) best = { t, box: b };
  }
  return best;
}
