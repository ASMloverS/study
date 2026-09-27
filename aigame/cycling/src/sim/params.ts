export const PHYS = {
  mass: 78,
  g: 9.81,
  Crr: 0.005,
  rho: 1.226,
  CdA: 0.14,
  draftDrag: 0.7,
  minSpeed: 1,
  maxDriveForce: 450,
} as const;

export const RACE = {
  dt: 1 / 60,
  // power multiplier per gear (ftp × r = target watts)
  gearRatios: [0.6, 1.0, 1.5, 2.5],
  drainBase: 12,
  // drain key per gear, squared in drainRate — intentionally mirrors gearRatios tiers
  drainRatios: [0.6, 1.0, 1.5, 2.5],
  regenRate: 0.0015,
  emptyCapRatio: 0.35,
  lateralMax: 2.2,
  lateralSpeed: 1.8,
  draftGapMin: 1,
  draftGapMax: 15,
  draftLateralWidth: 1.2,
  countdown: 3,
  finishWait: 30,
  trackWidth: 6,
  stageKm: 15,
  boxOffsets: [-1.7, 0, 1.7],
  boxRadius: 0.75,
  boxGain: 0.1,
} as const;

export interface ItemBox {
  d: number;
  lat: number;
}

// Derived from default stageKm; endurance tracks (buildTrack(150)) reuse these 15km-relative positions (known limitation).
export const ITEM_BOXES: ItemBox[] = (() => {
  const L = RACE.stageKm * 1000;
  const boxes: ItemBox[] = [];
  for (let k = 1; k <= 9; k++) {
    for (const lat of RACE.boxOffsets) boxes.push({ d: L * 0.1 * k, lat });
  }
  return boxes;
})();

export const GEARS = ['轻松', '巡航', '发力', '冲刺'] as const;

export const DRIVETRAIN = {
  chainring: 52,
  cogFloor: 1,
  cogStep: 1,
  cogHoldRate: 6,
  cadHoldRate: 30,
  defaultCogTeeth: 16,
  wheelCirc: 2.096,
  aiTargetCadence: 95,
  aiShiftHysteresis: 3,
  aiShiftMinSpeed: 0.5,
  aiCogMin: 10,
  aiCogMax: 36,
  climbCadence: 90,
  flatCadence: 110,
  descentCadence: 120,
  climbGradient: 0.02,
  descentGradient: -0.02,
  cadenceStep: 5,
  cadOffsetMax: 30,
} as const;
