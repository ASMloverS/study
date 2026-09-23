export type WeaponId = 'ar' | 'sg' | 'sr';

export const BTN = {
  FIRE: 1,
  JUMP: 2,
  CROUCH: 4,
  SPRINT: 8,
  ADS: 16,
  RELOAD: 32,
} as const;

export interface InputMsg {
  seq: number;
  moveX: number;
  moveZ: number;
  yaw: number;
  pitch: number;
  buttons: number;
  slot: number;
}

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export type C2SMessage =
  | { kind: 'join'; name: string }
  | { kind: 'input'; input: InputMsg }
  | { kind: 'ping'; t: number };

export interface PlayerSnap {
  id: number;
  name: string;
  x: number;
  y: number;
  z: number;
  vx: number;
  vy: number;
  vz: number;
  yaw: number;
  pitch: number;
  h: number;
  sl: boolean;
  hp: number;
  a: boolean;
  w: WeaponId;
  m: number;
  k: number;
  d: number;
  sf: number;
  sh: number;
  bs: number;
}

export interface Standing {
  id: number;
  name: string;
  k: number;
  d: number;
  sf: number;
  sh: number;
  bs: number;
}

export type GameEvent =
  | { type: 'shot'; tick: number; shooterId: number; origin: Vec3; end: Vec3; weapon: WeaponId }
  | { type: 'hit'; tick: number; attackerId: number; victimId: number; part: 'head' | 'body'; damage: number; attackerPos: Vec3 }
  | { type: 'kill'; tick: number; killerId: number; victimId: number; weapon: WeaponId; streak: number }
  | { type: 'spawn'; tick: number; playerId: number; pos: Vec3 }
  | { type: 'coverBreak'; tick: number; coverIndex: number; pos: Vec3 }
  | { type: 'explode'; tick: number; coverIndex: number; pos: Vec3; attackerId: number }
  | { type: 'gameOver'; tick: number; winnerId: number | null; standings: Standing[] };

export type S2CMessage =
  | { kind: 'welcome'; playerId: number; mapName: string }
  | { kind: 'snapshot'; tick: number; timeLeft: number; acks: Record<number, number>; players: PlayerSnap[]; destroyed: number[]; dyn: number[] }
  | { kind: 'events'; events: GameEvent[] }
  | { kind: 'pong'; t: number };
