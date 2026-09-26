import { DRIVETRAIN } from './sim/params';
import type { GearId, RiderCommand } from './sim/types';

export class InputController {
  private gear: GearId = 1;
  private cog: number = DRIVETRAIN.defaultCog;
  private steer = 0;
  private keys = new Set<string>();

  command(): RiderCommand {
    return { gear: this.gear, steer: this.steer, cog: this.cog };
  }

  attach(): void {
    window.addEventListener('keydown', this.onKeyDown);
    window.addEventListener('keyup', this.onKeyUp);
  }

  reset(): void {
    this.gear = 1;
    this.cog = DRIVETRAIN.defaultCog;
    this.steer = 0;
    this.keys.clear();
  }

  private onKeyDown = (e: KeyboardEvent) => {
    if (e.repeat) return;
    const k = e.key.toLowerCase();
    if (k === 'arrowup' || k === 'w') this.cog = Math.min(DRIVETRAIN.cassette.length - 1, this.cog + 1);
    else if (k === 'arrowdown' || k === 's') this.cog = Math.max(0, this.cog - 1);
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
