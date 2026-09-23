import { describe, expect, it } from 'vitest';
import { boxCenter, rayBox, raycastBoxes } from '../src';

describe('rayBox', () => {
  const box = boxCenter(0, 1, -5, 2, 2, 2);

  it('hits box from front', () => {
    const t = rayBox(0, 1, 0, 0, 0, -1, box, 100);
    expect(t).toBeCloseTo(4, 5);
  });

  it('misses when off to the side', () => {
    expect(rayBox(5, 1, 0, 0, 0, -1, box, 100)).toBeNull();
  });

  it('misses when beyond maxDist', () => {
    expect(rayBox(0, 1, 0, 0, 0, -1, box, 3)).toBeNull();
  });

  it('returns 0 when origin inside box', () => {
    expect(rayBox(0, 1, -5, 0, 0, -1, box, 100)).toBe(0);
  });

  it('misses when looking away', () => {
    expect(rayBox(0, 1, 0, 0, 0, 1, box, 100)).toBeNull();
  });
});

describe('raycastBoxes', () => {
  it('returns nearest hit', () => {
    const near = boxCenter(0, 1, -3, 1, 1, 1);
    const far = boxCenter(0, 1, -8, 1, 1, 1);
    const hit = raycastBoxes(0, 1, 0, 0, 0, -1, 100, [far, near]);
    expect(hit).not.toBeNull();
    expect(hit!.box).toBe(near);
    expect(hit!.t).toBeCloseTo(2.5, 5);
  });
});
