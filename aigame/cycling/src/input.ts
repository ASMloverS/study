import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cadPulse = 0;
  private cogPulse = 0;
  private steer = 0;
  private keys = new Set<string>();
  private lastNow: number;

  constructor(private now: () => number = () => performance.now() / 1000) {
    this.lastNow = now();
  }

  command(): RiderCommand {
    const now = this.now();
    const held = Math.min(0.1, Math.max(0, now - this.lastNow));
    this.lastNow = now;
    let cogDelta = this.cogPulse;
    let cadDelta = this.cadPulse;
    const cogUp = (this.keys.has('q') ? 1 : 0) - (this.keys.has('e') ? 1 : 0);
    const cadUp = (this.keys.has('w') || this.keys.has('arrowup') ? 1 : 0) - (this.keys.has('s') || this.keys.has('arrowdown') ? 1 : 0);
    cogDelta += cogUp * DRIVETRAIN.cogHoldRate * held;
    cadDelta += cadUp * DRIVETRAIN.cadHoldRate * held;
    this.cogPulse = 0;
    this.cadPulse = 0;
    return { gear: this.gear, steer: this.steer, cadDelta, cogDelta };
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('blur', () => {
      this.keys.clear();
      this.steer = 0;
    });
  }

  reset(): void {
    this.gear = 1;
    this.cadPulse = 0;
    this.cogPulse = 0;
    this.steer = 0;
    this.keys.clear();
    this.lastNow = this.now();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cadPulse += DRIVETRAIN.cadenceStep;
    else if (k === 'arrowdown' || k === 's') this.cadPulse -= DRIVETRAIN.cadenceStep;
    else if (k === 'q') this.cogPulse += DRIVETRAIN.cogStep;
    else if (k === 'e') this.cogPulse -= DRIVETRAIN.cogStep;
    else if (k === '1' || k === '2' || k === '3' || k === '4') this.gear = (Number(k) - 1) as GearId;
    this.keys.add(k);
    this.steer = this.computeSteer();
  };

  private onKeyUp = (e: KeyboardEvent) => {
    this.keys.delete(e.key.toLowerCase());
    this.steer = this.computeSteer();
  };

  private computeSteer(): number {
    const left = this.keys.has('arrowleft') || this.keys.has('a');
    const right = this.keys.has('arrowright') || this.keys.has('d');
    return right && !left ? 1 : left && !right ? -1 : 0;
  }
}
