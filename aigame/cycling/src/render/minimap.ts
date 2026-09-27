import type { Track } from '../sim/track';
import type { RiderState } from '../sim/types';
import { ITEM_BOXES } from '../sim/params';

export interface Fit {
  scale: number;
  cx: number;
  cz: number;
  w: number;
  h: number;
  pad: number;
}

type Pt = [number, number, number];

export function fitTrack(pts: readonly Pt[], w: number, h: number, pad: number): Fit {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const [x, , z] of pts) {
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
  }
  const scale = Math.min((w - pad * 2) / (maxX - minX || 1), (h - pad * 2) / (maxZ - minZ || 1));
  return { scale, cx: (minX + maxX) / 2, cz: (minZ + maxZ) / 2, w, h, pad };
}

export function project(x: number, z: number, f: Fit): [number, number] {
  return [f.w / 2 + (x - f.cx) * f.scale, f.h / 2 + (z - f.cz) * f.scale];
}

const COLORS = ['#ffd54a', '#e0533d', '#4d8fd6', '#8a5cd6', '#4dbd8a', '#d68a4d', '#d64d9e', '#5a6a7a'];

export class Minimap {
  private ctx: CanvasRenderingContext2D | null;
  private fit: Fit;
  private route: [number, number][] = [];
  private boxes: [number, number][] = [];

  constructor(private canvas: HTMLCanvasElement, track: Track) {
    this.ctx = canvas.getContext('2d');
    const pts = track.densePoints();
    this.fit = fitTrack(pts, canvas.width, canvas.height, 8);
    for (let i = 0; i < pts.length; i += 20) this.route.push(project(pts[i][0], pts[i][2], this.fit));
    const seen = new Set<number>();
    for (const b of ITEM_BOXES) {
      if (seen.has(b.d)) continue;
      seen.add(b.d);
      const s = track.sampleAt(b.d);
      this.boxes.push(project(s.x, s.z, this.fit));
    }
  }

  draw(riders: readonly RiderState[], track: Track): void {
    const ctx = this.ctx;
    if (!ctx) return;
    const { width: w, height: h } = this.canvas;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.75)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    this.route.forEach(([x, y], i) => (i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)));
    ctx.stroke();
    ctx.fillStyle = '#4dd2ff';
    for (const [x, y] of this.boxes) ctx.fillRect(x - 1, y - 1, 2, 2);
    for (const r of riders) {
      const s = track.sampleAt(r.dist);
      const [x, y] = project(s.x, s.z, this.fit);
      ctx.fillStyle = COLORS[r.id % COLORS.length];
      ctx.beginPath();
      ctx.arc(x, y, r.isPlayer ? 4.5 : 3, 0, Math.PI * 2);
      ctx.fill();
      if (r.isPlayer) {
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(x, y, 6.5, 0, Math.PI * 2);
        ctx.stroke();
      }
    }
  }
}
