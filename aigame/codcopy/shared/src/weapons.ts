import type { WeaponId } from './protocol';

export interface WeaponDef {
  id: WeaponId;
  name: string;
  damage: number;
  headMul: number;
  rpm: number;
  auto: boolean;
  magSize: number;
  reloadTime: number;
  pellets: number;
  spreadHip: number;
  spreadAds: number;
  adsFov: number;
  switchTime: number;
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
    reloadTime: 2.0,
    pellets: 1,
    spreadHip: 0.03,
    spreadAds: 0.006,
    adsFov: 55,
    switchTime: 0.5,
  },
  sg: {
    id: 'sg',
    name: '霰弹枪',
    damage: 12,
    headMul: 1.5,
    rpm: 70,
    auto: false,
    magSize: 6,
    reloadTime: 2.8,
    pellets: 8,
    spreadHip: 0.1,
    spreadAds: 0.07,
    adsFov: 60,
    switchTime: 0.5,
  },
  sr: {
    id: 'sr',
    name: '狙击枪',
    damage: 100,
    headMul: 1.75,
    rpm: 40,
    auto: true,
    magSize: 5,
    reloadTime: 3.0,
    pellets: 1,
    spreadHip: 0.12,
    spreadAds: 0,
    adsFov: 20,
    switchTime: 0.5,
  },
};
