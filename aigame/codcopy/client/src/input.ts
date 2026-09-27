import { BTN, DEFAULT_LOADOUT, defaultMagConfig, type InputMsg, type Loadout, type MagConfig } from 'shared';
import type { AssistLevel } from './aimassist';
import { EMPTY_PAD, type PadActions } from './gamepad';

export type EnemyColor = 'red' | 'orange' | 'yellow' | 'purple' | 'cyan';

export interface Settings {
  name: string;
  sensitivity: number;
  volume: number;
  fov: number;
  bots: number;
  difficulty: 'mixed' | 'easy' | 'normal' | 'hard';
  quality: 'low' | 'medium' | 'high';
  killLimit: number;
  matchMinutes: number;
  shadow: boolean;
  tts: boolean;
  brightness: number;
  enemyOutline: boolean;
  enemyColor: EnemyColor;
  loadout: Loadout;
  /** [M12] ADS 灵敏度倍率（相对） */
  adsSens: number;
  /** [M12] 辅助瞄准档位（键鼠默认关；手柄接入时自动开为中档） */
  aimAssist: AssistLevel;
  /** [M12] 手柄视角灵敏度（rad/s @ 满偏） */
  padSensitivity: number;
  /** [M12] 手柄径向死区 */
  padDeadzone: number;
  /** [M12] 手柄震动 */
  padRumble: boolean;
  /** [M14] 各枪弹匣容量（单机启动配置） */
  mags: MagConfig;
}

export const defaultSettings: Settings = {
  name: '玩家',
  sensitivity: 0.0022,
  volume: 0.7,
  fov: 80,
  bots: 7,
  difficulty: 'mixed',
  quality: 'high',
  killLimit: 30,
  matchMinutes: 10,
  shadow: true,
  tts: true,
  brightness: 1,
  enemyOutline: true,
  enemyColor: 'red',
  loadout: { ...DEFAULT_LOADOUT },
  adsSens: 1,
  aimAssist: 'off',
  padSensitivity: 2.4,
  padDeadzone: 0.12,
  padRumble: true,
  mags: defaultMagConfig(),
};

export class InputSystem {
  yaw = 0;
  pitch = 0;
  locked = false;
  settings: Settings;
  scoreboard = false;
  /** [M12] 手柄动作状态（每帧由 main 写入，与键鼠状态合并） */
  pad: PadActions = { ...EMPTY_PAD };
  private keys = new Set<string>();
  private fireHeld = false;
  private adsHeld = false;
  private pendingSlot = 0;
  private qSwapSlot = 0;
  private lastSlotSent = 1;
  private pendingStreak = 0;

  /** [M15] 放置模式：打开请求（0=无，2/3=tier，main 轮询）；激活期间鼠标增量/滚轮/Q-E 转发给俯图 */
  placementRequest = 0;
  placementActive = false;
  private uiDX = 0;
  private uiDY = 0;
  private wheelSteps = 0;
  private confirmPlacement = false;
  private cancelPlacement = false;
  private pendingStreakTarget: { x: number; z: number } | null = null;
  private pendingStreakYaw = 0;

  constructor(private target: HTMLElement, onStart: () => void, settings: Settings) {
    this.settings = settings;
    document.addEventListener('keydown', (e) => {
      this.keys.add(e.code);
      if (e.code === 'Space' || e.code === 'Tab') e.preventDefault();
      if (e.code === 'Digit1') this.pendingSlot = 1;
      if (e.code === 'Digit2') this.pendingSlot = 2;
      if (e.code === 'Digit4') this.pendingStreak = 1;
      if (e.code === 'Digit5') this.placementRequest = 2;
      if (e.code === 'Digit6') this.placementRequest = 3;
      if (this.placementActive && e.code === 'KeyQ') this.wheelSteps -= 1;
      if (this.placementActive && e.code === 'KeyE') this.wheelSteps += 1;
      if (e.code === 'KeyQ' && this.qSwapSlot > 0 && !this.placementActive) this.pendingSlot = this.qSwapSlot;
    });
    document.addEventListener('keyup', (e) => this.keys.delete(e.code));
    document.addEventListener('mousedown', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        if (e.button === 0) this.confirmPlacement = true;
        if (e.button === 2) this.cancelPlacement = true;
        return;
      }
      if (e.button === 0) this.fireHeld = true;
      if (e.button === 2) this.adsHeld = true;
    });
    document.addEventListener('mouseup', (e) => {
      if (e.button === 0) this.fireHeld = false;
      if (e.button === 2) this.adsHeld = false;
    });
    document.addEventListener('contextmenu', (e) => e.preventDefault());
    document.addEventListener('wheel', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        this.wheelSteps += Math.sign(e.deltaY);
        return;
      }
      void e.deltaY;
      this.pendingSlot = this.lastSlotSent === 1 ? 2 : 1;
    });
    document.addEventListener('mousemove', (e) => {
      if (!this.locked) return;
      if (this.placementActive) {
        this.uiDX += e.movementX;
        this.uiDY += e.movementY;
        return;
      }
      const sens = this.settings.sensitivity * (this.ads ? this.settings.adsSens : 1);
      this.yaw -= e.movementX * sens;
      this.pitch -= e.movementY * sens;
      this.clampPitch();
    });
    document.addEventListener('pointerlockchange', () => {
      this.locked = document.pointerLockElement === this.target;
      this.scoreboard = this.keys.has('Tab');
      if (this.locked) onStart();
    });
  }

  /** [M12] 手柄视角增量（已含灵敏度缩放） */
  applyLook(dYaw: number, dPitch: number): void {
    this.yaw += dYaw;
    this.pitch += dPitch;
    this.clampPitch();
  }

  /** [M12] 手柄切枪请求（1=主 2=副） */
  requestSlot(s: number): void {
    this.pendingSlot = s;
  }

  /** [M12] 手柄连杀激活请求（1-3） */
  requestStreak(t: 0 | 1 | 2 | 3): void {
    this.pendingStreak = t;
  }

  /** [M15] 俯图确认后携带落点激活 */
  requestStreakTargeted(streak: 2 | 3, target: { x: number; z: number }, yaw: number): void {
    this.pendingStreak = streak;
    this.pendingStreakTarget = target;
    this.pendingStreakYaw = yaw;
  }

  /** [M15] 放置模式帧增量（main 每帧轮询后清零） */
  drainPlacementDeltas(): { dx: number; dy: number; wheel: number; confirm: boolean; cancel: boolean } {
    const r = { dx: this.uiDX, dy: this.uiDY, wheel: this.wheelSteps, confirm: this.confirmPlacement, cancel: this.cancelPlacement };
    this.uiDX = 0;
    this.uiDY = 0;
    this.wheelSteps = 0;
    this.confirmPlacement = false;
    this.cancelPlacement = false;
    return r;
  }

  private clampPitch(): void {
    const lim = Math.PI / 2 - 0.01;
    this.pitch = Math.max(-lim, Math.min(lim, this.pitch));
  }

  get currentSlotHint(): number {
    return this.lastSlotSent;
  }

  lock(): void {
    this.target.requestPointerLock();
  }

  get ads(): boolean {
    return this.adsHeld || this.pad.ads;
  }

  get fire(): boolean {
    return this.fireHeld || this.pad.fire;
  }

  get lethalHeld(): boolean {
    return this.keys.has('KeyG') || this.pad.lethal;
  }

  get meleeHeld(): boolean {
    return this.keys.has('KeyV') || this.pad.melee;
  }

  get sprintHeld(): boolean {
    return this.keys.has('ShiftLeft') || this.keys.has('ShiftRight') || this.pad.sprint;
  }

  buildInput(seq: number, swayYaw = 0, swayPitch = 0): InputMsg {
    const k = this.keys;
    const pad = this.pad;
    let moveZ = pad.moveZ;
    let moveX = pad.moveX;
    if (k.has('KeyW')) moveZ += 1;
    if (k.has('KeyS')) moveZ -= 1;
    if (k.has('KeyD')) moveX += 1;
    if (k.has('KeyA')) moveX -= 1;
    const moveLen = Math.hypot(moveX, moveZ);
    if (moveLen > 1) {
      moveX /= moveLen;
      moveZ /= moveLen;
    }
    let buttons = 0;
    if (this.fire) buttons |= BTN.FIRE;
    if (this.ads) buttons |= BTN.ADS;
    if (k.has('Space') || pad.jump) buttons |= BTN.JUMP;
    if (k.has('ControlLeft') || k.has('KeyC') || pad.crouch) buttons |= BTN.CROUCH;
    if (k.has('ShiftLeft') || pad.sprint) buttons |= BTN.SPRINT;
    if (k.has('KeyR') || pad.reload) buttons |= BTN.RELOAD;
    if (this.meleeHeld) buttons |= BTN.MELEE;
    if (this.lethalHeld) buttons |= BTN.LETHAL;
    if (k.has('KeyE') || pad.tactical) buttons |= BTN.TACTICAL;
    if (this.placementActive) buttons &= BTN.JUMP | BTN.CROUCH | BTN.SPRINT;
    const slot = this.placementActive ? 0 : this.pendingSlot;
    if (slot > 0 && slot !== this.lastSlotSent) {
      this.qSwapSlot = this.lastSlotSent;
      this.lastSlotSent = slot;
    }
    this.pendingSlot = 0;
    this.scoreboard = k.has('Tab') || pad.scoreboard;
    const streak = this.pendingStreak;
    const streakTarget = this.pendingStreakTarget ?? undefined;
    const streakYaw = this.pendingStreakTarget ? this.pendingStreakYaw : undefined;
    this.pendingStreak = 0;
    this.pendingStreakTarget = null;
    this.pendingStreakYaw = 0;
    return { seq, moveX, moveZ, yaw: this.yaw + swayYaw, pitch: this.pitch + swayPitch, buttons, slot, streak, streakTarget, streakYaw };
  }
}
