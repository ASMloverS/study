export const PHYS = {
  mass: 78,
  g: 9.81,
  Crr: 0.005,
  rho: 1.226,
  CdA: 0.32,
  draftDrag: 0.7,
  minSpeed: 1,
} as const;

export const RACE = {
  dt: 1 / 60,
  gearRatios: [0.6, 1.0, 1.5, 2.5],
  drainBase: 30,
  emptyCapRatio: 0.45,
  lateralMax: 2.2,
  lateralSpeed: 1.8,
  draftGapMin: 1,
  draftGapMax: 15,
  draftLateralWidth: 1.2,
  countdown: 3,
  finishWait: 30,
  trackWidth: 6,
  feedZones: [[560, 610], [2640, 2690]],
  feedRegenRate: 0.04,
} as const;

export const GEARS = ['轻松', '巡航', '发力', '冲刺'] as const;

export const DRIVETRAIN = {
  chainring: 52,
  cassette: [36, 32, 28, 24, 21, 18, 16, 14, 13, 12, 11, 10],
  wheelCirc: 2.096,
  cadFullLo: 60,
  cadFullHi: 115,
  cadFloor: 40,
  cadCeil: 140,
  effMin: 0.55,
  aiTargetCadence: 95,
  aiShiftHysteresis: 3,
  defaultCog: 6,
} as const;
