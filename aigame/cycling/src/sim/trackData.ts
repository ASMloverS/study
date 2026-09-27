import { Track } from './track';
import { RACE } from './params';

const COUNT = 200;

const PROFILE_U: [number, number][] = [
  [0, 0], [0.05, 0.10], [0.14, 0.05], [0.28, 0.30], [0.48, 0.97], [0.56, 0.90],
  [0.64, 1.0], [0.70, 0.92], [0.88, 0.25], [0.94, 0.10], [1, 0],
];

function elevationAt(u: number, amp: number): number {
  if (u <= 0) return 0;
  if (u >= 1) return 0;
  for (let i = 1; i < PROFILE_U.length; i++) {
    if (u <= PROFILE_U[i][0]) {
      const [u0, f0] = PROFILE_U[i - 1];
      const [u1, f1] = PROFILE_U[i];
      const t = (u - u0) / (u1 - u0);
      const s = 0.5 - 0.5 * Math.cos(Math.PI * t);
      return (f0 + (f1 - f0) * s) * amp;
    }
  }
  return 0;
}

function turn(u: number, lo: number, hi: number): number {
  const t = Math.min(1, Math.max(0, (u - lo) / (hi - lo)));
  return t * t * (3 - 2 * t);
}

export function buildTrack(stageKm: number = RACE.stageKm): Track {
  const spacing = (stageKm * 1000) / COUNT;
  const amp = Math.min(999.5, 22 * stageKm);
  const ctrl: [number, number, number][] = [];
  let x = 0;
  let z = 0;
  const headingAt = (u: number) =>
    0.6 * Math.sin(u * Math.PI * 6) + Math.PI * turn(u, 0.42, 0.46) + Math.PI * turn(u, 0.78, 0.82);
  for (let i = 0; i < COUNT; i++) {
    ctrl.push([x, elevationAt(i / COUNT, amp), z]);
    const mid = headingAt((i + 0.5) / COUNT);
    x += Math.cos(mid) * spacing;
    z += Math.sin(mid) * spacing;
  }
  return new Track(ctrl, 8000, false);
}
