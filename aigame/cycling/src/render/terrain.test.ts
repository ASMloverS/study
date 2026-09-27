import { describe, expect, it } from 'vitest';
import { buildTerrain, baseHills, smoothstep } from './terrain';
import { buildTrack } from '../sim/trackData';
import type { Track } from '../sim/track';

describe('ribbon terrain', () => {
  const track: Track = buildTrack();
  const mesh = buildTerrain(track);
  it('keeps baseHills and smoothstep exports', () => {
    expect(typeof baseHills).toBe('function');
    expect(smoothstep(0.5, 0, 1)).toBeCloseTo(0.5, 6);
  });
  it('builds a ribbon grid mesh', () => {
    const count = mesh.geometry.attributes.position.count;
    expect(count).toBeGreaterThan(2000);
    expect(count).toBeLessThan(120000);
  });
  it('center column follows track elevation tightly', () => {
    const geo = mesh.geometry.attributes.position;
    const pos = geo.array as Float32Array;
    const cols = 13;
    const rows = pos.length / (3 * cols);
    const pts = track.densePoints();
    for (let r = 0; r < rows; r += 25) {
      const o = (r * cols + 6) * 3;
      // Float32 positions: half-ULP at ~59km max |x| is ~3.9mm, so 5mm bound.
      expect(Math.abs(pos[o] - pts[r * 3][0])).toBeLessThan(0.005);
      expect(Math.abs(pos[o + 2] - pts[r * 3][2])).toBeLessThan(0.005);
      expect(Math.abs(pos[o + 1] + 0.15 - pts[r * 3][1])).toBeLessThan(0.005);
    }
  });
});
