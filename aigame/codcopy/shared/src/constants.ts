export const TICK_RATE = 30;
export const TICK_DT = 1 / TICK_RATE;
export const SNAPSHOT_EVERY_TICKS = 2;

export const PLAYER_RADIUS = 0.4;
export const PLAYER_STAND_HEIGHT = 1.8;
export const PLAYER_CROUCH_HEIGHT = 1.2;
export const PLAYER_EYE_RATIO = 0.9;

export const WALK_SPEED = 4.2;
export const SPRINT_SPEED = 7.4;
export const CROUCH_SPEED = 2.2;
export const ADS_SPEED = 2.8;
export const JUMP_VELOCITY = 6.0;
export const GRAVITY = -18;
export const TERMINAL_VELOCITY = -40;
export const GROUND_ACCEL_K = 25;
export const AIR_ACCEL = 4;

export const SLIDE_START_MUL = 1.15;
export const SLIDE_MIN_START_SPEED = 6.0;
export const SLIDE_END_SPEED = 2.0;
export const SLIDE_FRICTION = 2.2;
export const SLIDE_COOLDOWN = 0.6;

export const MANTLE_MIN_RISE = 0.35;
export const MANTLE_MAX_RISE = 1.3;
export const MANTLE_REACH = 0.75;
export const MANTLE_LAND_INSET = 0.35;
export const MANTLE_COOLDOWN = 0.5;

export const MAX_HEALTH = 100;
export const RESPAWN_DELAY_TICKS = 90;
export const DEFAULT_KILL_LIMIT = 30;
export const DEFAULT_MATCH_DURATION = 600;
export const SPAWN_PROTECT_TICKS = 60;
export const MAX_PLAYERS = 8;
export const VIEW_DISTANCE = 40;
export const SHOT_MAX_DISTANCE = 200;
export const REGEN_DELAY_TICKS = 150;
export const REGEN_PER_TICK = 20 * TICK_DT;
export const SOUND_SHOT_RADIUS = 25;
export const SOUND_STEP_RADIUS = 8;
export const MEMORY_DECAY_TICKS = 240;

export const EMPTY_RELOAD_EXTRA = 0.6;
export const RECOIL_RECOVERY = 18;
export const RECOIL_RECOVERY_DELAY = 0.15;
export const RECOIL_MAX_PITCH = 6;
export const RECOIL_MAX_YAW = 3;
export const SPREAD_MUL_MOVE = 1.3;
export const SPREAD_MUL_JUMP = 2.0;
export const SPREAD_MUL_AIR = 2.5;
export const SPREAD_MUL_SLIDE = 1.5;
export const DEG2RAD = Math.PI / 180;
export const LAG_COMP_MAX_RTT_MS = 500;
export const LAG_COMP_MAX_TICKS = 8;

// M6.2 近战
export const MELEE_RANGE = 1.2;
export const MELEE_DAMAGE = 100;
export const MELEE_LOCK_TICKS = 18;
export const MELEE_HIT_TICK = 7;
export const MELEE_COOLDOWN_TICKS = 24;

// M6.2 投掷物
export const GRENADE_THROW_SPEED = 14;
export const GRENADE_BOUNCE = 0.35;
export const GRENADE_RADIUS = 0.09;
export const FRAG_FUSE_TICKS = 105;
export const FRAG_RADIUS = 3.5;
export const FRAG_MAX_DMG = 100;
export const FLASH_FUSE_TICKS = 54;
export const FLASH_RADIUS = 12;
export const FLASH_MAX_BLIND_MS = 2500;

// M6.3 连杀奖励
export const KILLSTREAK_UAV_KILLS = 3;
export const KILLSTREAK_AIRSTRIKE_KILLS = 5;
export const KILLSTREAK_CLUSTER_KILLS = 6;
export const UAV_DURATION_TICKS = 450;
export const AIRSTRIKE_DELAY_TICKS = 90;
export const AIRSTRIKE_COUNT = 5;
export const AIRSTRIKE_SPACING = 2.2;
export const AIRSTRIKE_RADIUS = 4;
export const AIRSTRIKE_DMG = 90;
export const CLUSTER_COUNT = 8;
export const CLUSTER_SCATTER = 8;
export const CLUSTER_RADIUS = 3;
export const CLUSTER_DMG = 70;
