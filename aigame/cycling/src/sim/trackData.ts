import { Track } from './track';

const SPACING = 750;
const COUNT = 200;

const PROFILE_KM: [number, number][] = [
  [0, 0], [10, 50], [20, 20], [30, 80], [35, 300], [38, 520], [41, 540], [44, 700],
  [47, 680], [50, 830], [53, 850], [56, 950], [60, 930], [65, 900], [72, 880], [80, 700],
  [85, 420], [90, 300], [95, 150], [100, 90], [105, 80], [112, 160], [120, 60], [128, 150],
  [135, 40], [142, 30], [150, 0],
];

function elevationAtKm(km: number): number {
  if (km <= 0) return PROFILE_KM[0][1];
  if (km >= 150) return PROFILE_KM[PROFILE_KM.length - 1][1];
  for (let i = 1; i < PROFILE_KM.length; i++) {
    if (km <= PROFILE_KM[i][0]) {
      const [k0, h0] = PROFILE_KM[i - 1];
      const [k1, h1] = PROFILE_KM[i];
      const t = (km - k0) / (k1 - k0);
      const s = 0.5 - 0.5 * Math.cos(Math.PI * t);
      return h0 + (h1 - h0) * s;
    }
  }
  return 0;
}

function turn(u: number, lo: number, hi: number): number {
  const t = Math.min(1, Math.max(0, (u - lo) / (hi - lo)));
  return t * t * (3 - 2 * t);
}

export function buildTrack(): Track {
  const ctrl: [number, number, number][] = [];
  let x = 0;
  let z = 0;
  const headingAt = (u: number) =>
    0.6 * Math.sin(u * Math.PI * 6) + Math.PI * turn(u, 0.42, 0.46) + Math.PI * turn(u, 0.78, 0.82);
  for (let i = 0; i < COUNT; i++) {
    ctrl.push([x, elevationAtKm((i / COUNT) * 150), z]);
    const mid = headingAt((i + 0.5) / COUNT);
    x += Math.cos(mid) * SPACING;
    z += Math.sin(mid) * SPACING;
  }
  return new Track(ctrl, 8000, false);
}
