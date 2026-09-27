import { describe, expect, it } from 'vitest';
import { WEAPONS, WEAPON_LIST, defaultMagConfig, effectiveReserve, sanitizeMagConfig } from '../src';

describe('[M14] mag config helpers', () => {
  it('defaultMagConfig equals WEAPONS table', () => {
    const m = defaultMagConfig();
    for (const id of WEAPON_LIST) expect(m[id]).toBe(WEAPONS[id].magSize);
  });

  it('sanitize clamps 1~999, rounds, falls back to defaults for invalid/missing', () => {
    const m = sanitizeMagConfig({ ar: 0, smg: 1000, lmg: 45.6, dmr: 'x', sg: -5 } as never);
    expect(m.ar).toBe(1);
    expect(m.smg).toBe(999);
    expect(m.lmg).toBe(46);
    expect(m.dmr).toBe(WEAPONS.dmr.magSize);
    expect(m.sg).toBe(1);
    expect(m.sr).toBe(WEAPONS.sr.magSize);
    expect(m.pistol).toBe(WEAPONS.pistol.magSize);
    expect(sanitizeMagConfig(null)).toEqual(defaultMagConfig());
    expect(sanitizeMagConfig('junk')).toEqual(defaultMagConfig());
    expect(sanitizeMagConfig(123)).toEqual(defaultMagConfig());
  });

  it('effectiveReserve scales proportionally with rounding', () => {
    expect(effectiveReserve('ar', 30)).toBe(90);
    expect(effectiveReserve('ar', 60)).toBe(180);
    expect(effectiveReserve('sg', 7)).toBe(28);
    expect(effectiveReserve('sg', 3)).toBe(12);
    expect(effectiveReserve('pistol', 1)).toBe(4);
  });
});
