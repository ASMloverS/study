import { describe, expect, it } from 'vitest';
import { BTN, WEAPONS, WEAPON_LIST, stepMovement, createMoveState, type WeaponId } from 'shared';
import { Room } from '../src';
import { pickLoadoutWeapon, weaponClass } from '../src/ai/controller';

describe('weapon table (M11)', () => {
  it('has 7 weapons with valid fields', () => {
    expect(WEAPON_LIST.length).toBe(7);
    for (const id of WEAPON_LIST) {
      const w = WEAPONS[id];
      expect(w.id).toBe(id);
      expect(w.damage).toBeGreaterThan(0);
      expect(w.rpm).toBeGreaterThan(0);
      expect(w.magSize).toBeGreaterThan(0);
      expect(w.moveMul).toBeGreaterThan(0);
      expect(w.adsMul).toBeGreaterThan(0);
      expect(w.adsTime).toBeGreaterThan(0);
    }
  });

  it('TTK ordering is sane across ranges', () => {
    // 近距：SG 一发 > SMG 泼水最快；远距：SR 一发秒杀
    const smgTtk = Math.ceil(100 / WEAPONS.smg.damage) * (60 / WEAPONS.smg.rpm);
    const arTtk = Math.ceil(100 / WEAPONS.ar.damage) * (60 / WEAPONS.ar.rpm);
    expect(smgTtk).toBeLessThan(arTtk + 0.1);
    const dmrBody = WEAPONS.dmr.damage * 2;
    expect(dmrBody).toBeGreaterThanOrEqual(100);
    expect(WEAPONS.dmr.damage * WEAPONS.dmr.headMul).toBeLessThan(100);
    expect(WEAPONS.sr.damage).toBeGreaterThanOrEqual(100);
  });
});

describe('weapon speed multipliers (M11)', () => {
  const mk = (weapon: WeaponId, buttons = 0) => {
    const s = createMoveState(0, 0, 0);
    const input = { seq: 1, moveX: 0, moveZ: 1, yaw: 0, pitch: 0, buttons, slot: 0 };
    for (let i = 0; i < 120; i++) stepMovement(s, input, [], undefined, weapon);
    return Math.hypot(s.vx, s.vz);
  };
  const walk = (w: WeaponId) => mk(w);
  const ads = (w: WeaponId) => mk(w, BTN.ADS);

  it('smg walks faster, lmg slower', () => {
    const ar = walk('ar');
    expect(walk('smg')).toBeGreaterThan(ar);
    expect(walk('lmg')).toBeLessThan(ar);
  });
  it('lmg ads speed reduced by adsMul', () => {
    expect(ads('lmg')).toBeLessThan(ads('ar'));
  });
});

describe('loadout (M11)', () => {
  it('spawn uses loadout primary; mags filled for all weapons', () => {
    const room = new Room(undefined, { seed: 11, bots: 0 });
    const p = room.addPlayer('H', false, 'normal', { primary: 'smg', secondary: 'dmr' });
    expect(p.weapon).toBe('smg');
    expect(p.mags.smg).toBe(WEAPONS.smg.magSize);
    expect(p.mags.dmr).toBe(WEAPONS.dmr.magSize);
    expect(p.mags.pistol).toBe(WEAPONS.pistol.magSize);
  });

  it('setLoadout applies only when dead; next spawn uses new primary', () => {
    const room = new Room(undefined, { seed: 12, bots: 0 });
    const p = room.addPlayer('H', false, 'normal', { primary: 'ar', secondary: 'pistol' });
    expect(p.alive).toBe(true);
    const rejected = room.setLoadout(p.id, { primary: 'lmg', secondary: 'sr' });
    expect(rejected.primary).toBe('ar');
    p.alive = false;
    const applied = room.setLoadout(p.id, { primary: 'lmg', secondary: 'sr' });
    expect(applied).toEqual({ primary: 'lmg', secondary: 'sr' });
    room.spawn(p);
    expect(p.weapon).toBe('lmg');
  });

  it('bots get a random loadout each spawn from the weapon pool', () => {
    const room = new Room(undefined, { seed: 13, bots: 4 });
    for (const p of room.players) {
      expect(WEAPON_LIST).toContain(p.loadout.primary);
      expect(WEAPON_LIST).toContain(p.loadout.secondary);
    }
  });

  it('AI distance weapon pick uses loadout classes', () => {
    expect(weaponClass('sg')).toBe('close');
    expect(weaponClass('sr')).toBe('far');
    expect(weaponClass('ar')).toBe('mid');
    const l = { primary: 'ar', secondary: 'pistol' } as const;
    expect(pickLoadoutWeapon(l, 5)).toBe('pistol');
    expect(pickLoadoutWeapon(l, 12)).toBe('ar');
    expect(pickLoadoutWeapon(l, 30)).toBe('ar');
    const sniperOnly = { primary: 'sr', secondary: 'sr' } as const;
    expect(pickLoadoutWeapon(sniperOnly, 5)).toBe('sr');
  });
});
