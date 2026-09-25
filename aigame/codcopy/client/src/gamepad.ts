import type { Settings } from './input';

/** [M12] 手柄支持：Xbox 标准布局（PS 按标识映射）、死区/灵敏度、震动、与键鼠自动切换。 */

export interface PadActions {
  moveX: number;
  moveZ: number;
  lookDX: number;
  lookDY: number;
  fire: boolean;
  ads: boolean;
  jump: boolean;
  crouch: boolean;
  sprint: boolean;
  reload: boolean;
  melee: boolean;
  lethal: boolean;
  tactical: boolean;
  switchWeapon: boolean;
  streak: 0 | 1 | 2 | 3;
  scoreboard: boolean;
  pause: boolean;
}

export const EMPTY_PAD: PadActions = {
  moveX: 0,
  moveZ: 0,
  lookDX: 0,
  lookDY: 0,
  fire: false,
  ads: false,
  jump: false,
  crouch: false,
  sprint: false,
  reload: false,
  melee: false,
  lethal: false,
  tactical: false,
  switchWeapon: false,
  streak: 0,
  scoreboard: false,
  pause: false,
};

/** 径向死区：超区后按 (len-dz)/(1-dz) 重标定，保持方向 */
export function deadzone(x: number, y: number, dz: number): { x: number; y: number } {
  const len = Math.hypot(x, y);
  if (len <= dz || len < 1e-6) return { x: 0, y: 0 };
  const k = Math.min(1, (len - dz) / (1 - dz)) / len;
  return { x: x * k, y: y * k };
}

interface PadLike {
  buttons: readonly { pressed: boolean; value: number }[];
  axes: number[] | readonly number[];
}

/**
 * 标准 mapping（navigator.getGamepads() 规范布局）→ 动作。
 * 按钮：0=A 1=B 2=X 3=Y 4=LB 5=RB 6=LT 7=RT 8=View 9=Menu 10=L3 11=R3 12=↑ 13=↓ 14=← 15=→
 * 轴：0/1 左摇杆 2/3 右摇杆
 */
export function mapGamepad(pad: PadLike, deadzoneV: number, sensitivity: number, dt: number): PadActions {
  const b = (i: number): boolean => i < pad.buttons.length && (pad.buttons[i].pressed || pad.buttons[i].value > 0.5);
  const move = deadzone(pad.axes[0] ?? 0, pad.axes[1] ?? 0, deadzoneV);
  const look = deadzone(pad.axes[2] ?? 0, pad.axes[3] ?? 0, deadzoneV);
  const streak: 0 | 1 | 2 | 3 = b(12) ? 1 : b(15) ? 2 : b(13) ? 3 : 0;
  return {
    moveX: move.x,
    moveZ: -move.y || 0,
    lookDX: (-look.x * sensitivity * dt) || 0,
    lookDY: (-look.y * sensitivity * dt) || 0,
    fire: b(7),
    ads: b(6),
    jump: b(0),
    crouch: b(1),
    sprint: b(10),
    reload: b(2),
    melee: b(11),
    lethal: b(4),
    tactical: b(5),
    switchWeapon: b(3),
    streak,
    scoreboard: b(8),
    pause: b(9),
  };
}

export class GamepadSys {
  active = false;
  private prevActions: PadActions = { ...EMPTY_PAD };
  private rumbleAt = 0;

  /** 每帧轮询；返回当前动作（无手柄时返回空动作且 active=false） */
  poll(dt: number, settings: Settings): PadActions {
    const pads = typeof navigator !== 'undefined' && navigator.getGamepads ? navigator.getGamepads() : [];
    let pad: Gamepad | null = null;
    for (const p of pads) {
      if (p && p.connected) {
        pad = p;
        break;
      }
    }
    if (!pad) {
      this.active = false;
      return { ...EMPTY_PAD };
    }
    const acts = mapGamepad(pad, settings.padDeadzone, settings.padSensitivity, dt);
    const hadInput =
      acts.fire || acts.ads || acts.jump || acts.crouch || acts.reload || acts.melee || acts.lethal || acts.pause || acts.switchWeapon;
    if (hadInput || Math.hypot(acts.moveX, acts.moveZ) > 0.1 || Math.hypot(acts.lookDX, acts.lookDY) > 1e-4) {
      this.active = true;
    }
    return acts;
  }

  /** 震动（不可用静默跳过；简单限频避免效果堆叠） */
  rumble(now: number, durationMs: number, weak = 0.5, strong = 0.5): void {
    if (now < this.rumbleAt) return;
    this.rumbleAt = now + durationMs;
    const pads = typeof navigator !== 'undefined' && navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) {
      const actuator = (p as (Gamepad & { vibrationActuator?: { playEffect: (t: string, o: object) => Promise<unknown> } }) | null)
        ?.vibrationActuator;
      if (actuator) {
        try {
          actuator.playEffect('dual-rumble', { duration: durationMs, strongMagnitude: strong, weakMagnitude: weak });
        } catch {
          void 0;
        }
      }
    }
  }

  /** 边沿检测辅助：本帧按下且上一帧未按下 */
  edgeOn(cur: boolean, key: keyof PadActions): boolean {
    return cur && !this.prevActions[key];
  }

  markPrev(acts: PadActions): void {
    this.prevActions = { ...acts };
  }
}
