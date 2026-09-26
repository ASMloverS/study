import { describe, expect, it } from 'vitest';
import { buildTrack } from './trackData';

describe('buildTrack', () => {
  const t = buildTrack();
  it('length ≈ 4.4 km', () => {
    expect(t.length).toBeGreaterThan(4100);
    expect(t.length).toBeLessThan(4700);
  });
  it('max gradient stays below 9%', () => {
    let max = 0;
    for (let d = 0; d < t.length; d += 10) max = Math.max(max, Math.abs(t.sampleAt(d).gradient));
    expect(max).toBeLessThan(0.09);
  });
  it('finish straight is flat', () => {
    for (let d = t.length - 300; d < t.length; d += 10) {
      expect(Math.abs(t.sampleAt(d).gradient)).toBeLessThan(0.02);
    }
  });
});
