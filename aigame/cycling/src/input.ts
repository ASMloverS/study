import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cadDelta = 0;
  private cogDelta = 0;
  private steer = 0;
  private keys = new Set<string>();

  command(): RiderCommand {
    const cmd = { gear: this.gear, steer: this.steer, cadDelta: this.cadDelta, cogDelta: this.cogDelta };
    this.cadDelta = 0;
    this.cogDelta = 0;
    return cmd;
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.cadDelta = 0;
    this.cogDelta = 0;
    this.steer = 0;
    this.keys.clear();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cadDelta += DRIVETRAIN.cadenceStep;
    else if (k === 'arrowdown' || k === 's') this.cadDelta -= DRIVETRAIN.cadenceStep;
    else if (k === 'e') this.cogDelta += 1;
    else if (k === 'q') this.cogDelta -= 1;
    else if (k === '1' || k === '2' || k === '3' || k === '4') this.gear = (Number(k) - 1) as GearId;
    else {
      this.keys.add(k);
      this.steer = this.computeSteer();
    }
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
