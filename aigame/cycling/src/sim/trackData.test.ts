import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack parameterized stage', () => {
  const track = buildTrack();
  it('is a ~15km point-to-point course', () => {
    expect(track.length).toBeGreaterThan(14400);
    expect(track.length).toBeLessThan(15600);
  });
  it('has mountain profile between 280 and 380m max elevation', () => {
    let max = -Infinity;
    for (let d = 0; d < track.length; d += 50) max = Math.max(max, track.sampleAt(d).y);
    expect(max).toBeGreaterThan(280);
    expect(max).toBeLessThan(380);
  });
  it('keeps gradients within ±16% with steep climbs and descents', () => {
    let maxG = 0;
    let minG = 0;
    for (let d = 0; d < track.length; d += 10) {
      const g = track.sampleAt(d).gradient;
      maxG = Math.max(maxG, g);
      minG = Math.min(minG, g);
    }
    expect(maxG).toBeLessThanOrEqual(0.16);
    expect(minG).toBeGreaterThanOrEqual(-0.16);
    expect(maxG).toBeGreaterThanOrEqual(0.07);
    expect(minG).toBeLessThanOrEqual(-0.05);
  });
  it('supports a 150km endurance configuration', () => {
    const long = buildTrack(150);
    expect(long.length).toBeGreaterThan(148500);
    expect(long.length).toBeLessThan(151500);
    let max = -Infinity;
    for (let d = 0; d < long.length; d += 500) max = Math.max(max, long.sampleAt(d).y);
    expect(max).toBeGreaterThan(900);
    expect(max).toBeLessThanOrEqual(1000);
  });
});
