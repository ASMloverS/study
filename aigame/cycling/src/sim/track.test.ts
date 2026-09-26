import { describe, expect, it } from 'vitest';
import { Track } from './track';

function circle(r: number, n: number, elev?: (i: number) => number): [number, number, number][] {
  return Array.from({ length: n }, (_, i) => {
    const a = (i / n) * Math.PI * 2;
    return [Math.cos(a) * r, elev ? elev(i) : 0, Math.sin(a) * r] as [number, number, number];
  });
}

describe('Track', () => {
  it('circle circumference ≈ 2πr', () => {
    const t = new Track(circle(100, 8));
    expect(t.length).toBeGreaterThan(610);
    expect(t.length).toBeLessThan(645);
  });
  it('sampleAt(0) equals first control point', () => {
    const s = new Track(circle(100, 8)).sampleAt(0);
    expect(s.x).toBeCloseTo(100, 3);
    expect(s.y).toBeCloseTo(0, 3);
    expect(s.z).toBeCloseTo(0, 3);
  });
  it('wraps around the loop', () => {
    const t = new Track(circle(100, 8));
    const a = t.sampleAt(5);
    const b = t.sampleAt(t.length + 5);
    expect(b.x).toBeCloseTo(a.x, 3);
    expect(b.z).toBeCloseTo(a.z, 3);
  });
  it('reports positive gradient on climb', () => {
    const t = new Track(circle(100, 8, (i) => (i / 8) * 50));
    expect(t.sampleAt(t.length * 0.25).gradient).toBeGreaterThan(0.05);
  });
  it('heading tracks ccw tangent direction', () => {
    const t = new Track(circle(100, 8));
    expect(t.sampleAt(0).heading).toBeCloseTo(Math.PI / 2, 2);
    expect(t.sampleAt(t.length * 0.5).heading).toBeCloseTo(-Math.PI / 2, 2);
  });
  it('nearest finds close point', () => {
    const n = new Track(circle(100, 8)).nearest(100, 0);
    expect(n.dist).toBeLessThan(3);
    expect(n.y).toBeCloseTo(0, 3);
    const mid = new Track(circle(100, 8)).sampleAt(3);
    const nm = new Track(circle(100, 8)).nearest(mid.x, mid.z);
    expect(nm.dist).toBeLessThan(2);
  });
});
