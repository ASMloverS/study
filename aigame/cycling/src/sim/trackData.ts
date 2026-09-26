import { Track } from './track';

const PROFILE: [number, number][] = [
  [0, 0], [0.14, 0], [0.26, 28], [0.36, 0], [0.58, 0], [0.66, 16], [0.74, 0], [1, 0],
];

function elevationAt(u: number): number {
  for (let i = 1; i < PROFILE.length; i++) {
    if (u <= PROFILE[i][0]) {
      const [u0, h0] = PROFILE[i - 1];
      const [u1, h1] = PROFILE[i];
      return h0 + ((h1 - h0) * (u - u0)) / (u1 - u0);
    }
  }
  return 0;
}

export function buildTrack(): Track {
  const n = 24;
  const ctrl: [number, number, number][] = [];
  for (let i = 0; i < n; i++) {
    const a = (i / n) * Math.PI * 2;
    const r = 700 + Math.sin(a * 3) * 80 + Math.cos(a * 2) * 60;
    ctrl.push([Math.cos(a) * r, elevationAt(i / n), Math.sin(a) * r]);
  }
  return new Track(ctrl);
}
