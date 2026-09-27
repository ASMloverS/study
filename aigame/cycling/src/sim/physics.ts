import { PHYS } from './params';

export function stepSpeed(speed: number, power: number, gradient: number, drafting: boolean, dt: number): number {
  const theta = Math.atan(gradient);
  const dragArea = PHYS.CdA * (drafting ? PHYS.draftDrag : 1);
  const fDrive = Math.min(power / Math.max(speed, PHYS.minSpeed), PHYS.maxDriveForce);
  const fRoll = PHYS.Crr * PHYS.mass * PHYS.g * Math.cos(theta);
  const fDrag = 0.5 * PHYS.rho * dragArea * speed * speed;
  const fGrav = PHYS.mass * PHYS.g * Math.sin(theta);
  const a = (fDrive - fRoll - fDrag - fGrav) / PHYS.mass;
  return Math.max(0, speed + a * dt);
}
