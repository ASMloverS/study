import { BTN, WEAPON_SLOTS, type InputMsg } from 'shared';

export interface Settings {
  sensitivity: number;
  volume: number;
  fov: number;
  bots: number;
  difficulty: 'mixed' | 'easy' | 'normal' | 'hard';
  quality: 'low' | 'medium' | 'high';
  killLimit: number;
  matchMinutes: number;
}

export const defaultSettings: Settings = {
  sensitivity: 0.0022,
  volume: 0.7,
  fov: 80,
  bots: 7,
  difficulty: 'mixed',
  quality: 'high',
  killLimit: 30,
  matchMinutes: 10,
};

export class InputSystem {
  yaw = 0;
  pitch = 0;
  locked = false;
  settings: Settings;
  scoreboard = false;
  private keys = new Set<string>();
  private fireHeld = false;
  private adsHeld = false;
  private pendingSlot = 0;
  private qSwapSlot = 0;
  private lastSlotSent = 1;
  private pendingStreak = 0;

  constructor(private target: HTMLElement, onStart: () => void, settings: Settings) {
    this.settings = settings;
    document.addEventListener('keydown', (e) => {
      this.keys.add(e.code);
      if (e.code === 'Space' || e.code === 'Tab') e.preventDefault();
      if (e.code === 'Digit1') this.pendingSlot = 1;
      if (e.code === 'Digit2') this.pendingSlot = 2;
      if (e.code === 'Digit3') this.pendingSlot = 3;
      if (e.code === 'Digit4') this.pendingStreak = 1;
      if (e.code === 'Digit5') this.pendingStreak = 2;
      if (e.code === 'Digit6') this.pendingStreak = 3;
      if (e.code === 'KeyQ' && this.qSwapSlot > 0) this.pendingSlot = this.qSwapSlot;
    });
    document.addEventListener('keyup', (e) => this.keys.delete(e.code));
    document.addEventListener('mousedown', (e) => {
      if (!this.locked) return;
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
      const dir = e.deltaY > 0 ? 1 : -1;
      const next = this.currentSlotHint + dir;
      this.pendingSlot = ((next + WEAPON_SLOTS.length) % WEAPON_SLOTS.length) + 1;
    });
    document.addEventListener('mousemove', (e) => {
      if (!this.locked) return;
      this.yaw -= e.movementX * this.settings.sensitivity;
      this.pitch -= e.movementY * this.settings.sensitivity;
      const lim = Math.PI / 2 - 0.01;
      this.pitch = Math.max(-lim, Math.min(lim, this.pitch));
    });
    document.addEventListener('pointerlockchange', () => {
      this.locked = document.pointerLockElement === this.target;
      this.scoreboard = this.keys.has('Tab');
      if (this.locked) onStart();
    });
  }

  get currentSlotHint(): number {
    return this.lastSlotSent;
  }

  lock(): void {
    this.target.requestPointerLock();
  }

  get ads(): boolean {
    return this.adsHeld;
  }

  get fire(): boolean {
    return this.fireHeld;
  }

  get lethalHeld(): boolean {
    return this.keys.has('KeyG');
  }

  get meleeHeld(): boolean {
    return this.keys.has('KeyV');
  }

  get sprintHeld(): boolean {
    return this.keys.has('ShiftLeft') || this.keys.has('ShiftRight');
  }

  buildInput(seq: number, swayYaw = 0, swayPitch = 0): InputMsg {
    const k = this.keys;
    let moveZ = 0;
    let moveX = 0;
    if (k.has('KeyW')) moveZ += 1;
    if (k.has('KeyS')) moveZ -= 1;
    if (k.has('KeyD')) moveX += 1;
    if (k.has('KeyA')) moveX -= 1;
    let buttons = 0;
    if (this.fireHeld) buttons |= BTN.FIRE;
    if (this.adsHeld) buttons |= BTN.ADS;
    if (k.has('Space')) buttons |= BTN.JUMP;
    if (k.has('ControlLeft') || k.has('KeyC')) buttons |= BTN.CROUCH;
    if (k.has('ShiftLeft')) buttons |= BTN.SPRINT;
    if (k.has('KeyR')) buttons |= BTN.RELOAD;
    if (k.has('KeyV')) buttons |= BTN.MELEE;
    if (k.has('KeyG')) buttons |= BTN.LETHAL;
    if (k.has('KeyE')) buttons |= BTN.TACTICAL;
    const slot = this.pendingSlot;
    if (slot > 0 && slot !== this.lastSlotSent) {
      this.qSwapSlot = this.lastSlotSent;
      this.lastSlotSent = slot;
    }
    this.pendingSlot = 0;
    this.scoreboard = k.has('Tab');
    const streak = this.pendingStreak;
    this.pendingStreak = 0;
    return { seq, moveX, moveZ, yaw: this.yaw + swayYaw, pitch: this.pitch + swayPitch, buttons, slot, streak };
  }
}
