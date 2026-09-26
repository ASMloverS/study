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
} as const;

export const GEARS = ['轻松', '巡航', '发力', '冲刺'] as const;
