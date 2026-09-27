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
  drainBase: 40,
  emptyCapRatio: 0.35,
  lateralMax: 2.2,
  lateralSpeed: 1.8,
  draftGapMin: 1,
  draftGapMax: 15,
  draftLateralWidth: 1.2,
  countdown: 3,
  finishWait: 30,
  trackWidth: 6,
  feedZones: [[560, 610], [2640, 2690]],
  feedZoneGain: 0.25,
} as const;

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
