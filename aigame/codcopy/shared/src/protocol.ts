export type WeaponId = 'ar' | 'smg' | 'lmg' | 'dmr' | 'sg' | 'sr' | 'pistol';

/** [M14] 房间级弹匣容量配置（每枪膛数，人机同规则） */
export type MagConfig = Record<WeaponId, number>;

/** [M11] 双武器 loadout：主/副各任选（可双长枪） */
export interface Loadout {
  primary: WeaponId;
  secondary: WeaponId;
}

export const BTN = {
  FIRE: 1,
  JUMP: 2,
  CROUCH: 4,
  SPRINT: 8,
  ADS: 16,
  RELOAD: 32,
  MELEE: 64,
  LETHAL: 128,
  TACTICAL: 256,
} as const;

export interface InputMsg {
  seq: number;
  moveX: number;
  moveZ: number;
  yaw: number;
  pitch: number;
  buttons: number;
  slot: number;
  streak?: number;
  /** [M15] 放置模式落点（世界坐标，服务端激活时二次 clamp）与空袭航线角（rad） */
  streakTarget?: { x: number; z: number };
  streakYaw?: number;
}

export interface MatchConfig {
  killLimit: number;
  durationSec: number;
  mags?: MagConfig;
}

export type KillCause = WeaponId | 'melee' | 'grenade' | 'airstrike' | 'cluster' | 'barrel' | 'suicide';

export interface NadeSnap {
  i: number;
  x: number;
  y: number;
  z: number;
  k: 'frag' | 'flash';
}

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export type LobbyDifficulty = 'mixed' | 'easy' | 'normal' | 'hard';

export type C2SMessage =
  | { kind: 'join'; name: string; loadout?: Loadout }
  | { kind: 'input'; input: InputMsg }
  | { kind: 'ping'; t: number; rtt?: number }
  | { kind: 'loadout'; loadout: Loadout }
  | { kind: 'lobby'; bots?: number; difficulty?: LobbyDifficulty; killLimit?: number; matchMinutes?: number; mags?: MagConfig };

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
  rs: number;
  rl: number;
  k: number;
  d: number;
  sf: number;
  sh: number;
  bs: number;
  sp?: number;
  st?: number;
  sv?: number;
  le?: number;
  ta?: number;
  ps?: number;
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
  | { type: 'kill'; tick: number; killerId: number; victimId: number; weapon: WeaponId; streak: number; hs?: boolean; cause?: KillCause }
  | { type: 'spawn'; tick: number; playerId: number; pos: Vec3 }
  | { type: 'coverBreak'; tick: number; coverIndex: number; pos: Vec3 }
  | { type: 'explode'; tick: number; coverIndex: number; pos: Vec3; attackerId: number }
  | { type: 'blast'; tick: number; pos: Vec3; attackerId: number | null; cause: KillCause }
  | { type: 'melee'; tick: number; attackerId: number; victimId: number | null }
  | { type: 'grenadeThrow'; tick: number; ownerId: number; nadeId: number; kind: 'frag' | 'flash'; pos: Vec3; vel: Vec3 }
  | { type: 'flashPop'; tick: number; ownerId: number; pos: Vec3 }
  | { type: 'streakEarned'; tick: number; playerId: number; tier: 1 | 2 | 3 }
  | { type: 'streakUse'; tick: number; playerId: number; tier: 1 | 2 | 3; target?: { x: number; z: number }; yaw?: number }
  | { type: 'gameOver'; tick: number; winnerId: number | null; standings: Standing[] };

export type S2CMessage =
  | { kind: 'welcome'; playerId: number; mapName: string; cfg?: MatchConfig; loadout?: Loadout }
  | { kind: 'snapshot'; tick: number; timeLeft: number; acks: Record<number, number>; players: PlayerSnap[]; destroyed: number[]; dyn: number[]; nades?: NadeSnap[] }
  | { kind: 'events'; events: GameEvent[] }
  | { kind: 'pong'; t: number }
  | { kind: 'loadoutAck'; loadout: Loadout }
  | { kind: 'lobbyState'; hostId: number | null; bots: number; difficulty: LobbyDifficulty; killLimit: number; matchMinutes: number; pendingKillLimit?: number; pendingMatchMinutes?: number; mags?: MagConfig; pendingMags?: MagConfig };
