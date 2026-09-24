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
export const GROUND_ACCEL_K = 18;
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
export const MATCH_KILL_LIMIT = 20;
export const MATCH_DURATION = 600;
export const MAX_PLAYERS = 8;
export const VIEW_DISTANCE = 40;
export const SHOT_MAX_DISTANCE = 200;
export const REGEN_DELAY_TICKS = 150;
export const REGEN_PER_TICK = 20 * TICK_DT;
export const SOUND_SHOT_RADIUS = 25;
export const SOUND_STEP_RADIUS = 8;
export const MEMORY_DECAY_TICKS = 240;

export const EMPTY_RELOAD_EXTRA = 0.6;
export const RECOIL_RECOVERY = 14;
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
