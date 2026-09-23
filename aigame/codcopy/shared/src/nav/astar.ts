import { cellToWorld, isWalkable, worldToCell, type NavGrid } from './grid';

export interface PathPoint {
  x: number;
  z: number;
}

interface Node {
  cx: number;
  cz: number;
  g: number;
  f: number;
  parent: number | null;
  closed: boolean;
}

export function gridLineOfSight(g: NavGrid, ax: number, az: number, bx: number, bz: number): boolean {
  const dist = Math.hypot(bx - ax, bz - az);
  const steps = Math.max(1, Math.ceil(dist / (g.cell * 0.5)));
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const { cx, cz } = worldToCell(g, ax + (bx - ax) * t, az + (bz - az) * t);
    if (!isWalkable(g, cx, cz)) return false;
  }
  return true;
}

function nearestWalkable(g: NavGrid, cx: number, cz: number): { cx: number; cz: number } | null {
  if (isWalkable(g, cx, cz)) return { cx, cz };
  for (let r = 1; r < 12; r++) {
    for (let dz = -r; dz <= r; dz++) {
      for (let dx = -r; dx <= r; dx++) {
        if (Math.max(Math.abs(dx), Math.abs(dz)) !== r) continue;
        if (isWalkable(g, cx + dx, cz + dz)) return { cx: cx + dx, cz: cz + dz };
      }
    }
  }
  return null;
}

export function findPath(g: NavGrid, from: PathPoint, to: PathPoint): PathPoint[] | null {
  const sc = worldToCell(g, from.x, from.z);
  const tc = worldToCell(g, to.x, to.z);
  const s = nearestWalkable(g, sc.cx, sc.cz);
  const t = nearestWalkable(g, tc.cx, tc.cz);
  if (!s || !t) return null;
  const idx = (cx: number, cz: number) => cz * g.width + cx;
  const nodes = new Map<number, Node>();
  const open: number[] = [];
  const heuristic = (cx: number, cz: number) => {
    const dx = Math.abs(cx - t.cx);
    const dz = Math.abs(cz - t.cz);
    return (dx + dz) + (Math.SQRT2 - 2) * Math.min(dx, dz);
  };
  const start: Node = { cx: s.cx, cz: s.cz, g: 0, f: heuristic(s.cx, s.cz), parent: null, closed: false };
  nodes.set(idx(s.cx, s.cz), start);
  open.push(idx(s.cx, s.cz));
  let found: Node | null = null;
  let guard = 0;
  while (open.length > 0 && guard++ < 20000) {
    let bestI = 0;
    for (let i = 1; i < open.length; i++) {
      if (nodes.get(open[i])!.f < nodes.get(open[bestI])!.f) bestI = i;
    }
    const curIdx = open.splice(bestI, 1)[0];
    const cur = nodes.get(curIdx)!;
    cur.closed = true;
    if (cur.cx === t.cx && cur.cz === t.cz) {
      found = cur;
      break;
    }
    for (let dz = -1; dz <= 1; dz++) {
      for (let dx = -1; dx <= 1; dx++) {
        if (dx === 0 && dz === 0) continue;
        const nx = cur.cx + dx;
        const nz = cur.cz + dz;
        if (!isWalkable(g, nx, nz)) continue;
        if (dx !== 0 && dz !== 0 && (!isWalkable(g, cur.cx + dx, cur.cz) || !isWalkable(g, cur.cx, cur.cz + dz))) continue;
        const step = dx !== 0 && dz !== 0 ? Math.SQRT2 : 1;
        const nIdx = idx(nx, nz);
        const existing = nodes.get(nIdx);
        const ng = cur.g + step;
        if (existing) {
          if (!existing.closed && ng < existing.g) {
            existing.g = ng;
            existing.f = ng + heuristic(nx, nz);
            existing.parent = curIdx;
          }
        } else {
          const node: Node = { cx: nx, cz: nz, g: ng, f: ng + heuristic(nx, nz), parent: curIdx, closed: false };
          nodes.set(nIdx, node);
          open.push(nIdx);
        }
      }
    }
  }
  if (!found) return null;
  const cells: Node[] = [];
  let n: Node | null = found;
  while (n) {
    cells.unshift(n);
    n = n.parent !== null ? nodes.get(n.parent)! : null;
  }
  const pts: PathPoint[] = cells.map((c) => cellToWorld(g, c.cx, c.cz));
  pts[0] = { x: from.x, z: from.z };
  pts[pts.length - 1] = { x: to.x, z: to.z };
  return smoothPath(g, pts);
}

export function smoothPath(g: NavGrid, pts: PathPoint[]): PathPoint[] {
  if (pts.length <= 2) return pts;
  const out: PathPoint[] = [pts[0]];
  let anchor = 0;
  for (let i = 2; i < pts.length; i++) {
    if (!gridLineOfSight(g, pts[anchor].x, pts[anchor].z, pts[i].x, pts[i].z)) {
      out.push(pts[i - 1]);
      anchor = i - 1;
    }
  }
  out.push(pts[pts.length - 1]);
  return out;
}
