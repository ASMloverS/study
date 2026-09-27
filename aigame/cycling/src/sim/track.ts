export interface TrackSample {
  x: number;
  y: number;
  z: number;
  gradient: number;
  heading: number;
}

type Pt = [number, number, number];

function catmull(p0: number, p1: number, p2: number, p3: number, u: number): number {
  const u2 = u * u;
  const u3 = u2 * u;
  return 0.5 * ((2 * p1) + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2 + (-p0 + 3 * p1 - 3 * p2 + p3) * u3);
}

export class Track {
  readonly length: number;
  private readonly pts: Pt[];
  private readonly cum: number[];
  private readonly grad: number[];

  constructor(control: Pt[], samples = 2400, closed = true) {
    const n = control.length;
    const span = closed ? n : n - 1;
    this.pts = [];
    for (let i = 0; i <= samples; i++) {
      const t = (i / samples) * span;
      const j = Math.floor(t);
      const u = t - j;
      const g = (k: number) =>
        closed ? control[(j + k + n) % n] : control[Math.min(n - 1, Math.max(0, j + k))];
      const [p0, p1, p2, p3] = [g(-1), g(0), g(1), g(2)];
      this.pts.push([
        catmull(p0[0], p1[0], p2[0], p3[0], u),
        catmull(p0[1], p1[1], p2[1], p3[1], u),
        catmull(p0[2], p1[2], p2[2], p3[2], u),
      ]);
    }
    this.cum = [0];
    for (let i = 1; i <= samples; i++) {
      const a = this.pts[i - 1];
      const b = this.pts[i];
      this.cum.push(this.cum[i - 1] + Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
    }
    this.length = this.cum[samples];
    this.grad = [];
    for (let i = 0; i <= samples; i++) {
      const a = Math.max(0, i - 2);
      const b = Math.min(samples, i + 2);
      const pa = this.pts[a];
      const pb = this.pts[b];
      const run = Math.hypot(pb[0] - pa[0], pb[2] - pa[2]) || 1e-6;
      this.grad.push((pb[1] - pa[1]) / run);
    }
  }

  sampleAt(dist: number): TrackSample {
    const d = ((dist % this.length) + this.length) % this.length;
    let lo = 0;
    let hi = this.cum.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (this.cum[mid] <= d) lo = mid;
      else hi = mid;
    }
    const a = this.pts[lo];
    const b = this.pts[hi];
    const seg = this.cum[hi] - this.cum[lo] || 1;
    const u = (d - this.cum[lo]) / seg;
    return {
      x: a[0] + (b[0] - a[0]) * u,
      y: a[1] + (b[1] - a[1]) * u,
      z: a[2] + (b[2] - a[2]) * u,
      gradient: this.grad[lo] + (this.grad[hi] - this.grad[lo]) * u,
      heading: Math.atan2(b[2] - a[2], b[0] - a[0]),
    };
  }

  densePoints(): readonly Pt[] {
    return this.pts;
  }

  nearest(x: number, z: number): { y: number; dist: number } {
    let bestI = 0;
    let bestD = Infinity;
    for (let i = 0; i < this.pts.length - 1; i += 8) {
      const p = this.pts[i];
      const d = Math.hypot(p[0] - x, p[2] - z);
      if (d < bestD) {
        bestD = d;
        bestI = i;
      }
    }
    let best = { y: this.pts[bestI][1], dist: bestD };
    const lo = Math.max(0, bestI - 8);
    const hi = Math.min(this.pts.length - 2, bestI + 8);
    for (let i = lo; i <= hi; i++) {
      const p = this.pts[i];
      const d = Math.hypot(p[0] - x, p[2] - z);
      if (d < best.dist) best = { y: p[1], dist: d };
    }
    return best;
  }
}
