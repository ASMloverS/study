import { BTN, TICK_DT, VIEW_DISTANCE, WEAPONS, type InputMsg, type PathPoint, type WeaponId, findPath } from 'shared';
import { mulberry32 } from '../game/rng';
import type { Room, ServerPlayer } from '../game/world';
import { createPerception, updatePerception, type PerceptionState } from './perception';
import { Act, Cond, Selector, Sequence, type BTNode } from './behaviorTree';

export type BotDifficulty = 'easy' | 'normal' | 'hard';

export interface DifficultyDef {
  reaction: number;
  aimErr: number;
  decideEvery: number;
  turnRate: number;
}

export const DIFFICULTIES: Record<BotDifficulty, DifficultyDef> = {
  easy: { reaction: 18, aimErr: 0.1, decideEvery: 6, turnRate: 5 },
  normal: { reaction: 10, aimErr: 0.055, decideEvery: 4, turnRate: 7 },
  hard: { reaction: 5, aimErr: 0.03, decideEvery: 3, turnRate: 9 },
};

const MIXED: BotDifficulty[] = ['easy', 'easy', 'normal', 'normal', 'normal', 'normal', 'hard', 'hard'];

export function mixedDifficulty(i: number): BotDifficulty {
  return MIXED[i % MIXED.length];
}

/** [M11] 武器距离分类：近距 / 中距 / 远距 */
const CLOSE_WEAPONS: WeaponId[] = ['sg', 'smg', 'pistol'];
const FAR_WEAPONS: WeaponId[] = ['sr', 'dmr'];

type WeaponClass = 'close' | 'mid' | 'far';

export function weaponClass(w: WeaponId): WeaponClass {
  if (CLOSE_WEAPONS.includes(w)) return 'close';
  if (FAR_WEAPONS.includes(w)) return 'far';
  return 'mid';
}

/** [M11] 按目标距离从 loadout 主/副武器中选择适配的一把 */
export function pickLoadoutWeapon(loadout: { primary: WeaponId; secondary: WeaponId }, dist: number): WeaponId {
  const want: WeaponClass = dist < 8 ? 'close' : dist > 18 ? 'far' : 'mid';
  if (weaponClass(loadout.primary) === want) return loadout.primary;
  if (weaponClass(loadout.secondary) === want) return loadout.secondary;
  return loadout.primary;
}

export interface BotBrain {
  rng: () => number;
  difficulty: BotDifficulty;
  perception: PerceptionState;
  root: BTNode<Ctx>;
  moveGoal: PathPoint | null;
  path: PathPoint[] | null;
  pathIdx: number;
  pathGoal: PathPoint | null;
  repathAtTick: number;
  aimYaw: number;
  aimPitch: number;
  strafe: number;
  nextStrafeTick: number;
  fireUntilTick: number;
  restUntilTick: number;
  reactionUntilTick: number;
  lastTargetId: number;
  lastDecisionTick: number;
  coverSpot: PathPoint | null;
  peekSpot: PathPoint | null;
  peeking: boolean;
  peekToggleTick: number;
  lastStuckCheckTick: number;
  lastX: number;
  lastZ: number;
  stuck: boolean;
  targetId: number | null;
  threatPos: PathPoint | null;
  wantWeapon: WeaponId | null;
  slideAgainAtTick: number;
}

export interface Ctx {
  room: Room;
  p: ServerPlayer;
  b: BotBrain;
}

function angleDelta(from: number, to: number): number {
  let d = (to - from) % (Math.PI * 2);
  if (d > Math.PI) d -= Math.PI * 2;
  if (d < -Math.PI) d += Math.PI * 2;
  return d;
}

function yawTowards(dx: number, dz: number): number {
  return Math.atan2(-dx, -dz);
}

export function createBrain(seed: number, difficulty: BotDifficulty, yaw: number): BotBrain {
  return {
    rng: mulberry32(seed),
    difficulty,
    perception: createPerception(),
    root: buildTree(),
    moveGoal: null,
    path: null,
    pathIdx: 0,
    pathGoal: null,
    repathAtTick: 0,
    aimYaw: yaw,
    aimPitch: 0,
    strafe: 1,
    nextStrafeTick: 0,
    fireUntilTick: 0,
    restUntilTick: 0,
    reactionUntilTick: 0,
    lastTargetId: 0,
    lastDecisionTick: 0,
    coverSpot: null,
    peekSpot: null,
    peeking: false,
    peekToggleTick: 0,
    lastStuckCheckTick: 0,
    lastX: 0,
    lastZ: 0,
    stuck: false,
    targetId: null,
    threatPos: null,
    wantWeapon: null,
    slideAgainAtTick: 0,
  };
}

// ---------- behavior tree ----------

function buildTree(): BTNode<Ctx> {
  return Selector(
    Sequence(Cond(isRetreat), Act(doRetreat)),
    Sequence(Cond(pickTarget), Act(doEngage)),
    Sequence(Cond(hasMemory), Act(doHunt)),
    Act(doPatrol),
  );
}

function isRetreat({ room, p }: Ctx): boolean {
  return p.alive && p.health < 30 && p.lastDamagedTick >= room.tick - 90;
}

function doRetreat(ctx: Ctx): 'running' {
  const { room, b } = ctx;
  const threat = b.threatPos ?? nearestEnemyPos(ctx);
  b.threatPos = threat;
  b.targetId = null;
  if (!b.coverSpot || room.tick > b.repathAtTick + 90) b.coverSpot = findCover(ctx, threat);
  b.moveGoal = b.coverSpot ?? nearestEnemyPos(ctx);
  b.wantWeapon = 'ar';
  return 'running';
}

function pickTarget(ctx: Ctx): boolean {
  const { room, p, b } = ctx;
  let best: { id: number; d: number; pos: PathPoint } | null = null;
  for (const m of b.perception.memories.values()) {
    if (room.tick - m.tick > 15) continue;
    const q = room.players.find((x) => x.id === m.id);
    if (!q || !q.alive) continue;
    const d = Math.hypot(q.st.x - p.st.x, q.st.z - p.st.z);
    if (d > VIEW_DISTANCE) continue;
    if (!room.hasLineOfSight(p, q)) continue;
    if (!best || d < best.d) best = { id: q.id, d, pos: { x: m.x, z: m.z } };
  }
  if (!best) {
    b.targetId = null;
    return false;
  }
  b.targetId = best.id;
  b.threatPos = best.pos;
  return true;
}

function doEngage(ctx: Ctx): 'running' {
  const { room, p, b } = ctx;
  const target = room.players.find((q) => q.id === b.targetId);
  if (!target) return 'running';
  const dist = Math.hypot(target.st.x - p.st.x, target.st.z - p.st.z);
  b.wantWeapon = pickLoadoutWeapon(p.loadout, dist);
  if (b.targetId !== b.lastTargetId) {
    b.lastTargetId = b.targetId ?? 0;
    b.reactionUntilTick = room.tick + DIFFICULTIES[b.difficulty].reaction;
  }
  const magLow = p.mags[p.weapon] <= Math.ceil(magSize(p.weapon) * 0.2);
  if (!b.coverSpot || room.tick > b.repathAtTick + 120) {
    b.coverSpot = findCover(ctx, { x: target.st.x, z: target.st.z });
    if (b.coverSpot) {
      const dx = target.st.x - b.coverSpot.x;
      const dz = target.st.z - b.coverSpot.z;
      const len = Math.hypot(dx, dz) || 1;
      b.peekSpot = { x: b.coverSpot.x + (dx / len) * 1.2, z: b.coverSpot.z + (dz / len) * 1.2 };
    }
  }
  if (b.coverSpot && b.peekSpot) {
    if (room.tick > b.peekToggleTick) {
      b.peeking = !b.peeking;
      b.peekToggleTick = room.tick + (b.peeking ? 18 : 27) + Math.floor(b.rng() * 12);
    }
    b.moveGoal = magLow || p.health < 35 ? b.coverSpot : b.peeking ? b.peekSpot : b.coverSpot;
  } else {
    if (room.tick > b.nextStrafeTick) {
      b.strafe = b.rng() < 0.25 ? 0 : b.rng() < 0.5 ? -1 : 1;
      b.nextStrafeTick = room.tick + 30 + Math.floor(b.rng() * 30);
    }
  }
  return 'running';
}

function hasMemory({ b }: Ctx): boolean {
  return freshestMemory(b) !== null;
}

function doHunt(ctx: Ctx): 'running' {
  const { b, p } = ctx;
  b.targetId = null;
  b.wantWeapon = p.loadout.primary;
  const mem = freshestMemory(b);
  if (mem) b.moveGoal = { x: mem.x, z: mem.z };
  return 'running';
}

function doPatrol(ctx: Ctx): 'running' {
  const { room, b, p } = ctx;
  b.targetId = null;
  b.wantWeapon = p.loadout.primary;
  if (!b.moveGoal || reached(ctx, b.moveGoal) || room.tick > b.repathAtTick + 240) {
    const pois = room.map.interestPoints;
    const poi = pois[Math.floor(b.rng() * pois.length)];
    b.moveGoal = { x: poi[0] + (b.rng() - 0.5) * 6, z: poi[2] + (b.rng() - 0.5) * 6 };
  }
  return 'running';
}

function freshestMemory(b: BotBrain): { id: number; x: number; z: number } | null {
  let best: { id: number; x: number; z: number; tick: number } | null = null;
  for (const m of b.perception.memories.values()) {
    if (!best || m.tick > best.tick) best = m;
  }
  return best ? { id: best.id, x: best.x, z: best.z } : null;
}

function nearestEnemyPos({ room, p }: Ctx): PathPoint {
  let best: PathPoint = { x: 0, z: 0 };
  let bestD = Infinity;
  for (const q of room.players) {
    if (q === p || !q.alive) continue;
    const d = Math.hypot(q.st.x - p.st.x, q.st.z - p.st.z);
    if (d < bestD) {
      bestD = d;
      best = { x: q.st.x, z: q.st.z };
    }
  }
  return best;
}

function magSize(w: WeaponId): number {
  return WEAPONS[w].magSize;
}

// ---------- locomotion & input assembly ----------

function reached({ p }: Ctx, goal: PathPoint): boolean {
  return Math.hypot(goal.x - p.st.x, goal.z - p.st.z) < 1.5;
}

function followPath(ctx: Ctx): { dirX: number; dirZ: number; sprint: boolean } {
  const { room, p, b } = ctx;
  const goal = b.moveGoal;
  if (!goal) return { dirX: 0, dirZ: 0, sprint: false };
  const goalChanged = !b.pathGoal || b.pathGoal.x !== goal.x || b.pathGoal.z !== goal.z;
  if (goalChanged || !b.path || room.tick > b.repathAtTick) {
    b.path = findPath(room.nav, { x: p.st.x, z: p.st.z }, goal);
    b.pathIdx = 1;
    b.pathGoal = { x: goal.x, z: goal.z };
    b.repathAtTick = room.tick + 45;
  }
  if (!b.path || b.path.length < 2 || b.pathIdx >= b.path.length) return { dirX: 0, dirZ: 0, sprint: false };
  let wp = b.path[b.pathIdx];
  while (Math.hypot(wp.x - p.st.x, wp.z - p.st.z) < 0.7 && b.pathIdx < b.path.length - 1) {
    b.pathIdx++;
    wp = b.path[b.pathIdx];
  }
  const dx = wp.x - p.st.x;
  const dz = wp.z - p.st.z;
  const len = Math.hypot(dx, dz) || 1;
  const far = Math.hypot(goal.x - p.st.x, goal.z - p.st.z) > 8;
  const evade = b.difficulty === 'hard' && p.lastDamagedTick >= room.tick - 30;
  return { dirX: dx / len, dirZ: dz / len, sprint: far && (b.targetId === null || evade) };
}

export function findCover(ctx: Ctx, threat: PathPoint): PathPoint | null {
  const { room, p } = ctx;
  let best: PathPoint | null = null;
  let bestD = Infinity;
  for (const c of room.map.covers) {
    if (c.size[1] < 1.0) continue;
    const cx = c.pos[0];
    const cz = c.pos[2];
    const hx = c.size[0] / 2;
    const hz = c.size[2] / 2;
    let bx = p.st.x - cx;
    let bz = p.st.z - cz;
    const bl = Math.hypot(bx, bz) || 1;
    bx /= bl;
    bz /= bl;
    const spot = {
      x: cx + bx * (Math.abs(bx) * hx + Math.abs(bz) * hz + 0.7),
      z: cz + bz * (Math.abs(bx) * hx + Math.abs(bz) * hz + 0.7),
    };
    const d = Math.hypot(spot.x - p.st.x, spot.z - p.st.z);
    if (d < 0.8 || d > 15 || d >= bestD) continue;
    if (!room.isNavWalkable(spot.x, spot.z)) continue;
    const dx = threat.x - spot.x;
    const dz = threat.z - spot.z;
    const dist = Math.hypot(dx, dz);
    if (dist < 1.5) continue;
    if (room.blocked(spot.x, 1.5, spot.z, dx / dist, dz / dist, dist)) {
      best = spot;
      bestD = d;
    }
  }
  return best;
}

export function updateBot(room: Room, p: ServerPlayer): InputMsg {
  const b = p.bot!;
  const def = DIFFICULTIES[b.difficulty];
  const t = room.tick;

  if (t - b.lastDecisionTick >= def.decideEvery) {
    b.lastDecisionTick = t;
    if (room.isBlinded(p.id)) {
      b.perception.memories.clear();
      b.targetId = null;
    } else {
      updatePerception(room, p, b.perception, room.currentSounds);
    }
    b.root.tick({ room, p, b });
    if (b.targetId === null) b.coverSpot = null;
  }

  if (t - b.lastStuckCheckTick >= 45) {
    const moved = Math.hypot(p.st.x - b.lastX, p.st.z - b.lastZ);
    b.stuck = moved < 0.6 && b.moveGoal !== null && !reached({ room, p, b }, b.moveGoal);
    b.lastX = p.st.x;
    b.lastZ = p.st.z;
    b.lastStuckCheckTick = t;
    if (b.stuck) b.repathAtTick = 0;
  }

  const { dirX, dirZ, sprint } = followPath({ room, p, b });

  const target = b.targetId !== null ? room.players.find((q) => q.id === b.targetId && q.alive) : undefined;
  const dist = target ? Math.hypot(target.st.x - p.st.x, target.st.z - p.st.z) : 0;
  let desiredYaw = dirX === 0 && dirZ === 0 ? b.aimYaw : yawTowards(dirX, dirZ);
  let desiredPitch = 0;
  let aimClose = false;
  if (target) {
    const err0 = def.aimErr * (0.6 + dist / 25);
    const err = p.weapon === 'sr' && b.difficulty === 'hard' ? err0 * 0.6 : err0;
    const aimY = target.st.y + target.st.height * 0.7;
    const dy = aimY - (p.st.y + p.st.height * 0.9);
    const dx = target.st.x - p.st.x;
    const dz = target.st.z - p.st.z;
    desiredYaw = yawTowards(dx, dz) + (b.rng() - 0.5) * 2 * err;
    desiredPitch = Math.atan2(dy, Math.hypot(dx, dz)) + (b.rng() - 0.5) * 2 * err;
    aimClose = Math.abs(angleDelta(b.aimYaw, yawTowards(dx, dz))) < 0.09;
  }
  b.aimYaw += angleDelta(b.aimYaw, desiredYaw) * Math.min(1, def.turnRate * TICK_DT);
  b.aimPitch += (desiredPitch - b.aimPitch) * Math.min(1, def.turnRate * TICK_DT);

  const fx = -Math.sin(b.aimYaw);
  const fz = -Math.cos(b.aimYaw);
  const rx = Math.cos(b.aimYaw);
  const rz = -Math.sin(b.aimYaw);
  let moveX = dirX * rx + dirZ * rz;
  let moveZ = dirX * fx + dirZ * fz;
  if (target && !b.moveGoal) {
    moveX = b.strafe;
    moveZ = dist > 22 ? 1 : dist < 6 ? -1 : 0;
  }
  moveX = Math.max(-1, Math.min(1, moveX));
  moveZ = Math.max(-1, Math.min(1, moveZ));

  let buttons = 0;
  if (sprint) buttons |= BTN.SPRINT;
  if (b.stuck && p.st.onGround) buttons |= BTN.JUMP;

  if (b.fireUntilTick > 0 && t >= b.fireUntilTick) {
    b.restUntilTick = t + 9;
    b.fireUntilTick = 0;
  }
  if (b.restUntilTick <= t && b.fireUntilTick === 0) b.fireUntilTick = t + 12;
  const hiding = b.coverSpot !== null && !b.peeking && b.moveGoal === b.coverSpot;
  const rangeOk = CLOSE_WEAPONS.includes(p.weapon) ? dist < 14 : p.weapon === 'sr' ? dist > 8 : true;
  if (target && aimClose && rangeOk && !hiding && t >= b.reactionUntilTick && b.fireUntilTick > t && !room.isBlinded(p.id)) {
    buttons |= BTN.FIRE;
  }

  if (
    (p.mags[p.weapon] === 0 && p.reserve[p.weapon] > 0) ||
    (target && dist > 25 && p.mags[p.weapon] <= Math.ceil(magSize(p.weapon) * 0.2) && p.reserve[p.weapon] > 0)
  ) {
    buttons |= BTN.RELOAD;
  }

  // 交战 ADS：中远距离开镜获得精度加成
  if (target && dist > 8 && !hiding) buttons |= BTN.ADS;
  // 掩体后隐藏 / 换弹时蹲下（低矮掩体完全遮蔽）
  if (p.st.onGround && (hiding || ((buttons & BTN.RELOAD) !== 0 && b.coverSpot !== null))) {
    buttons |= BTN.CROUCH;
  }
  // 受压滑铲：困难 AI 在高速受击时滑铲换位（冲刺中按蹲触发）
  if (
    b.difficulty === 'hard' &&
    p.lastDamagedTick >= t - 15 &&
    p.st.onGround &&
    Math.hypot(p.st.vx, p.st.vz) > 5.5 &&
    t >= b.slideAgainAtTick
  ) {
    buttons |= BTN.CROUCH;
    b.slideAgainAtTick = t + 45;
  }

  const hasAmmo = (wid: WeaponId): boolean => p.mags[wid] > 0 || p.reserve[wid] > 0;
  const slotOf = (wid: WeaponId): number => (wid === p.loadout.primary ? 1 : wid === p.loadout.secondary ? 2 : 0);
  let slot = 0;
  if (b.wantWeapon && b.wantWeapon !== p.weapon && hasAmmo(b.wantWeapon)) {
    slot = slotOf(b.wantWeapon);
  } else if (!hasAmmo(p.weapon)) {
    const alt = [p.loadout.primary, p.loadout.secondary].find((wid) => wid !== p.weapon && hasAmmo(wid));
    if (alt) slot = slotOf(alt);
  }

  p.ackSeq++;
  return { seq: p.ackSeq, moveX, moveZ, yaw: b.aimYaw, pitch: b.aimPitch, buttons, slot };
}
