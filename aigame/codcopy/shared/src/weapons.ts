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
  /** [M11] 武器移速倍率（SMG 1.05 / LMG 0.9，其余 1.0） */
  moveMul: number;
  /** [M11] ADS 移速倍率（LMG 0.85，其余 1.0） */
  adsMul: number;
  /** [M12] 开镜时长（秒） */
  adsTime: number;
}

/** [M11] 武器库（7 把），loadout 主/副各任选其一 */
export const WEAPON_LIST: WeaponId[] = ['ar', 'smg', 'lmg', 'dmr', 'sg', 'sr', 'pistol'];

/** @deprecated [M11] 由 WEAPON_LIST + 双武器 loadout 取代；保留过渡引用 */
export const WEAPON_SLOTS: WeaponId[] = WEAPON_LIST;

export const DEFAULT_LOADOUT: { primary: WeaponId; secondary: WeaponId } = { primary: 'ar', secondary: 'pistol' };

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
    reloadTime: 1.8,
    pellets: 1,
    spreadHip: 0.03,
    spreadAds: 0.006,
    adsFov: 55,
    switchTime: 0.38,
    sprintOutTime: 0.3,
    recoilPitch: 0.3,
    recoilYaw: 0.1,
    falloffStart: 15,
    falloffEnd: 45,
    falloffMin: 0.7,
    moveMul: 1,
    adsMul: 1,
    adsTime: 0.24,
  },
  smg: {
    id: 'smg',
    name: '冲锋枪',
    damage: 18,
    headMul: 1.5,
    rpm: 900,
    auto: true,
    magSize: 32,
    reserve: 128,
    reloadTime: 1.5,
    pellets: 1,
    spreadHip: 0.032,
    spreadAds: 0.008,
    adsFov: 55,
    switchTime: 0.3,
    sprintOutTime: 0.25,
    recoilPitch: 0.22,
    recoilYaw: 0.12,
    falloffStart: 12,
    falloffEnd: 36,
    falloffMin: 0.65,
    moveMul: 1.05,
    adsMul: 1,
    adsTime: 0.2,
  },
  lmg: {
    id: 'lmg',
    name: '轻机枪',
    damage: 30,
    headMul: 1.8,
    rpm: 540,
    auto: true,
    magSize: 75,
    reserve: 150,
    reloadTime: 3.8,
    pellets: 1,
    spreadHip: 0.045,
    spreadAds: 0.011,
    adsFov: 55,
    switchTime: 0.72,
    sprintOutTime: 0.5,
    recoilPitch: 0.5,
    recoilYaw: 0.25,
    falloffStart: 20,
    falloffEnd: 50,
    falloffMin: 0.75,
    moveMul: 0.9,
    adsMul: 0.85,
    adsTime: 0.38,
  },
  dmr: {
    id: 'dmr',
    name: '射手步枪',
    damage: 50,
    headMul: 1.9,
    rpm: 240,
    auto: false,
    magSize: 15,
    reserve: 60,
    reloadTime: 2.1,
    pellets: 1,
    spreadHip: 0.05,
    spreadAds: 0.003,
    adsFov: 40,
    switchTime: 0.47,
    sprintOutTime: 0.4,
    recoilPitch: 1.4,
    recoilYaw: 0.3,
    falloffStart: 30,
    falloffEnd: 60,
    falloffMin: 0.8,
    moveMul: 1,
    adsMul: 1,
    adsTime: 0.28,
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
    reloadTime: 2.5,
    pellets: 8,
    spreadHip: 0.1,
    spreadAds: 0.07,
    adsFov: 60,
    switchTime: 0.51,
    sprintOutTime: 0.4,
    recoilPitch: 1.0,
    recoilYaw: 0.3,
    falloffStart: 8,
    falloffEnd: 20,
    falloffMin: 0.5,
    moveMul: 1,
    adsMul: 1,
    adsTime: 0.3,
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
    reloadTime: 2.7,
    pellets: 1,
    spreadHip: 0.12,
    spreadAds: 0,
    adsFov: 20,
    switchTime: 0.6,
    sprintOutTime: 0.5,
    recoilPitch: 2.2,
    recoilYaw: 0.4,
    falloffStart: 1e9,
    falloffEnd: 1e9,
    falloffMin: 1,
    moveMul: 1,
    adsMul: 1,
    adsTime: 0.38,
  },
  pistol: {
    id: 'pistol',
    name: '手枪',
    damage: 30,
    headMul: 1.6,
    rpm: 450,
    auto: false,
    magSize: 12,
    reserve: 48,
    reloadTime: 1.3,
    pellets: 1,
    spreadHip: 0.035,
    spreadAds: 0.01,
    adsFov: 58,
    switchTime: 0.21,
    sprintOutTime: 0.2,
    recoilPitch: 0.8,
    recoilYaw: 0.2,
    falloffStart: 15,
    falloffEnd: 40,
    falloffMin: 0.7,
    moveMul: 1,
    adsMul: 1,
    adsTime: 0.18,
  },
};

export function falloffMul(w: WeaponDef, dist: number): number {
  if (dist <= w.falloffStart) return 1;
  if (dist >= w.falloffEnd) return w.falloffMin;
  const k = (dist - w.falloffStart) / (w.falloffEnd - w.falloffStart);
  return 1 + (w.falloffMin - 1) * k;
}
