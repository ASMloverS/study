import type { WeaponId } from './protocol';

export interface WeaponDef {
  id: WeaponId;
  name: string;
  damage: number;
  headMul: number;
  rpm: number;
  auto: boolean;
  magSize: number;
  reserve: number;
  reloadTime: number;
  pellets: number;
  spreadHip: number;
  spreadAds: number;
  adsFov: number;
  switchTime: number;
  sprintOutTime: number;
  recoilPitch: number;
  recoilYaw: number;
  falloffStart: number;
  falloffEnd: number;
  falloffMin: number;
}

export const WEAPON_SLOTS: WeaponId[] = ['ar', 'sg', 'sr'];

export const WEAPONS: Record<WeaponId, WeaponDef> = {
  ar: {
    id: 'ar',
    name: '突击步枪',
    damage: 25,
    headMul: 2,
    rpm: 600,
    auto: true,
    magSize: 30,
    reserve: 90,
    reloadTime: 2.0,
    pellets: 1,
    spreadHip: 0.03,
    spreadAds: 0.006,
    adsFov: 55,
    switchTime: 0.45,
    sprintOutTime: 0.3,
    recoilPitch: 0.35,
    recoilYaw: 0.15,
    falloffStart: 15,
    falloffEnd: 45,
    falloffMin: 0.7,
  },
  sg: {
    id: 'sg',
    name: '霰弹枪',
    damage: 12,
    headMul: 1.5,
    rpm: 70,
    auto: false,
    magSize: 6,
    reserve: 24,
    reloadTime: 2.8,
    pellets: 8,
    spreadHip: 0.1,
    spreadAds: 0.07,
    adsFov: 60,
    switchTime: 0.6,
    sprintOutTime: 0.4,
    recoilPitch: 1.2,
    recoilYaw: 0.3,
    falloffStart: 8,
    falloffEnd: 20,
    falloffMin: 0.5,
  },
  sr: {
    id: 'sr',
    name: '狙击枪',
    damage: 100,
    headMul: 1.75,
    rpm: 40,
    auto: false,
    magSize: 5,
    reserve: 20,
    reloadTime: 3.0,
    pellets: 1,
    spreadHip: 0.12,
    spreadAds: 0,
    adsFov: 20,
    switchTime: 0.7,
    sprintOutTime: 0.5,
    recoilPitch: 2.5,
    recoilYaw: 0.4,
    falloffStart: 1e9,
    falloffEnd: 1e9,
    falloffMin: 1,
  },
};

export function falloffMul(w: WeaponDef, dist: number): number {
  if (dist <= w.falloffStart) return 1;
  if (dist >= w.falloffEnd) return w.falloffMin;
  const k = (dist - w.falloffStart) / (w.falloffEnd - w.falloffStart);
  return 1 + (w.falloffMin - 1) * k;
}
