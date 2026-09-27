import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack hyperspeed stage', () => {
  const track = buildTrack();
  it('is a ~150km point-to-point course', () => {
    expect(track.length).toBeGreaterThan(148500);
    expect(track.length).toBeLessThan(151500);
  });
  it('has mountain profile between 900 and 1000m max elevation', () => {
    let max = -Infinity;
    for (let d = 0; d < track.length; d += 100) max = Math.max(max, track.sampleAt(d).y);
    expect(max).toBeGreaterThan(900);
    expect(max).toBeLessThan(1000);
  });
  it('keeps gradients within ±16% with steep climbs and descents', () => {
    let maxG = 0;
    let minG = 0;
    for (let d = 0; d < track.length; d += 20) {
      const g = track.sampleAt(d).gradient;
      maxG = Math.max(maxG, g);
      minG = Math.min(minG, g);
    }
    expect(maxG).toBeLessThanOrEqual(0.16);
    expect(minG).toBeGreaterThanOrEqual(-0.16);
    expect(maxG).toBeGreaterThanOrEqual(0.07);
    expect(minG).toBeLessThanOrEqual(-0.05);
  });
});
