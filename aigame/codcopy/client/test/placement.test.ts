import { describe, expect, it } from 'vitest';
import { MAPS } from 'shared';
import { Placement, clampToMap, HEADING_STEP } from '../src/placement';

const stubCanvas = { width: 400, style: { display: 'none' } } as unknown as HTMLCanvasElement;

describe('[M15] placement math', () => {
  it('clampToMap bounds to ±(size/2 - 1)', () => {
    expect(clampToMap(999, 48)).toBe(23);
    expect(clampToMap(-999, 48)).toBe(-23);
    expect(clampToMap(10, 48)).toBe(10);
  });

  it('moveCursor translates pixels to world units and clamps to map', () => {
    const p = new Placement(MAPS.warehouse, stubCanvas);
    p.open(2, { x: 0, z: 0, yaw: 0 }, []);
    p.moveCursor(100, -100); // 400px ↔ 48m → 0.12 m/px → (12, -12)
    expect(p.cursor.x).toBeCloseTo(12);
    expect(p.cursor.z).toBeCloseTo(-12);
    p.moveCursor(100000, 100000);
    expect(p.cursor.x).toBe(23);
    expect(p.cursor.z).toBe(23);
  });

  it('rotate steps heading; confirm returns result and closes', () => {
    const p = new Placement(MAPS.warehouse, stubCanvas);
    p.open(2, { x: 1, z: 2, yaw: 0.5 }, []);
    p.rotate(1);
    expect(p.heading).toBeCloseTo(0.5 + HEADING_STEP);
    const r = p.confirm();
    expect(r.streak).toBe(2);
    expect(r.streakTarget).toEqual({ x: 1, z: 2 });
    expect(r.streakYaw).toBeCloseTo(0.5 + HEADING_STEP);
    expect(p.active).toBe(false);
  });

  it('confirm after close returns null', () => {
    const p = new Placement(MAPS.warehouse, stubCanvas);
    p.open(3, { x: 0, z: 0, yaw: 0 }, []);
    p.close();
    expect(p.confirm()).toBeNull();
  });
});
