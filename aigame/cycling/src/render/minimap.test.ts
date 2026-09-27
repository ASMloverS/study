import { describe, expect, it } from 'vitest';
import { fitTrack, project } from './minimap';
import { buildTrack } from '../sim/trackData';

describe('minimap projection', () => {
  const track = buildTrack();
  const fit = fitTrack(track.densePoints(), 200, 140, 8);
  it('fits all track points inside the canvas with padding', () => {
    for (const [x, , z] of track.densePoints()) {
      const [px, py] = project(x, z, fit);
      expect(px).toBeGreaterThanOrEqual(8);
      expect(px).toBeLessThanOrEqual(192);
      expect(py).toBeGreaterThanOrEqual(8);
      expect(py).toBeLessThanOrEqual(132);
    }
  });
  it('maps the start point near the padded origin consistently', () => {
    const pts = track.densePoints();
    const [px, py] = project(pts[0][0], pts[0][2], fit);
    expect(px).toBeGreaterThanOrEqual(8);
    expect(px).toBeLessThanOrEqual(192);
    expect(py).toBeGreaterThanOrEqual(8);
    expect(py).toBeLessThanOrEqual(132);
  });
});
