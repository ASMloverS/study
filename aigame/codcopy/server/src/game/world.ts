import {
  BTN,
  DEFAULT_KILL_LIMIT,
  DEFAULT_MATCH_DURATION,
  DEG2RAD,
  EMPTY_RELOAD_EXTRA,
  type GameEvent,
  type InputMsg,
  type KillCause,
  LAG_COMP_MAX_RTT_MS,
  LAG_COMP_MAX_TICKS,
  MAPS,
  type MapDef,
  MAX_HEALTH,
  MAX_PLAYERS,
  type PlayerSnap,
  REGEN_DELAY_TICKS,
  REGEN_PER_TICK,
  RESPAWN_DELAY_TICKS,
  FLASH_FUSE_TICKS,
  FLASH_MAX_BLIND_MS,
  FLASH_RADIUS,
  FRAG_FUSE_TICKS,
  FRAG_MAX_DMG,
  FRAG_RADIUS,
  GRENADE_BOUNCE,
  GRENADE_RADIUS,
  GRENADE_THROW_SPEED,
  MELEE_COOLDOWN_TICKS,
  MELEE_DAMAGE,
  MELEE_HIT_TICK,
  MELEE_LOCK_TICKS,
  MELEE_RANGE,
  type NadeSnap,
  SHOT_MAX_DISTANCE,
  SPAWN_PROTECT_TICKS,
  type Standing,
  AIRSTRIKE_COUNT,
  AIRSTRIKE_DELAY_TICKS,
  AIRSTRIKE_DMG,
  AIRSTRIKE_RADIUS,
  AIRSTRIKE_SPACING,
  CLUSTER_COUNT,
  CLUSTER_DMG,
  CLUSTER_RADIUS,
  CLUSTER_SCATTER,
  stepProjectile,
  TICK_RATE,
  TICK_DT,
  WEAPONS,
  WEAPON_LIST,
  DEFAULT_LOADOUT,
  type Loadout,
  type WeaponId,
  type MagConfig,
  effectiveReserve,
  sanitizeMagConfig,
  addRecoilShot,
  bakeNavGrid,
  boxCenter,
  coverToAABB,
  createMoveState,
  createRecoilState,
  eyeY,
  falloffMul,
  jitterDir,
  type MoveState,
  type NavGrid,
  type RecoilState,
  rayBox,
  raycastBoxes,
  type AABB,
  spreadMulFor,
  stepMovement,
  updateRecoil,
  viewDir,
  isWalkable,
  worldToCell,
  type S2CMessage,
} from 'shared';
import { mulberry32 } from './rng';
import type { ProjectileState } from 'shared';
import { createBrain, mixedDifficulty, updateBot, type BotBrain, type BotDifficulty } from '../ai/controller';
import type { SoundEvent } from '../ai/perception';
import { createDestructibles, barrelDamage, CHAIN_DELAY_TICKS, BARREL_BLAST_RADIUS, type DestructibleState } from './destructible';
import { createDynamicCovers, type DynamicCoverManager } from './dynamicCover';
import {
  createStreakState,
  streakAvailableMask,
  streakCanUse,
  streakConsume,
  streakOnKill,
  type StreakState,
} from './killstreak';

export interface ServerPlayer {
  id: number;
  name: string;
  isBot: boolean;
  st: MoveState;
  alive: boolean;
  health: number;
  respawnAtTick: number;
  loadout: Loadout;
  weapon: WeaponId;
  mags: Record<WeaponId, number>;
  reserve: Record<WeaponId, number>;
  recoil: RecoilState;
  pendingWeapon: WeaponId | null;
  switchEndsTick: number;
  reloadEndsTick: number;
  fireCooldown: number;
  prevFireBtn: boolean;
  kills: number;
  deaths: number;
  streak: number;
  bestStreak: number;
  shotsFired: number;
  shotsHit: number;
  lastDamagedTick: number;
  spawnProtUntil: number;
  lethal: number;
  tactical: number;
  cookingKind: 'frag' | null;
  cookingSinceTick: number;
  meleeEndsTick: number;
  meleeHitTick: number;
  meleeCooldownUntil: number;
  prevMeleeBtn: boolean;
  prevLethalBtn: boolean;
  prevTacticalBtn: boolean;
  streaks: StreakState;
  ackSeq: number;
  lastRecvSeq: number;
  inputQueue: InputMsg[];
  lastInput: InputMsg;
  bot: BotBrain | null;
}

export interface RoomOptions {
  bots?: number;
  seed?: number;
  botDifficulty?: BotDifficulty | 'mixed';
  killLimit?: number;
  durationSec?: number;
  /** [M14] 弹匣容量配置（清洗后生效，人机同规则） */
  magConfig?: MagConfig;
}

function zeroInput(): InputMsg {
  return { seq: 0, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons: 0, slot: 0, streak: 0 };
}

/** [M11] AI 随机 loadout：长枪主武器 + 副手枪/第二长枪 */
function randomBotLoadout(rng: () => number): Loadout {
  const longGuns: WeaponId[] = ['ar', 'smg', 'lmg', 'dmr', 'sg', 'sr'];
  const primary = longGuns[Math.floor(rng() * longGuns.length)];
  const secondary: WeaponId = rng() < 0.5 ? 'pistol' : longGuns[Math.floor(rng() * longGuns.length)];
  return { primary, secondary };
}

export class Room {
  readonly map: MapDef;
  readonly obstacles: AABB[];
  nav: NavGrid;
  readonly players: ServerPlayer[] = [];
  readonly destructibles: DestructibleState[];
  readonly dynamic: DynamicCoverManager;
  tick = 0;
  timeLeft = DEFAULT_MATCH_DURATION;
  killLimit: number;
  durationSec: number;
  /** [M14] 房间级弹匣容量（人机同规则；静态 WEAPONS 表不受影响） */
  magConfig: MagConfig;
  over = false;
  winner: number | null = null;
  botDifficulty: BotDifficulty | 'mixed';
  currentSounds: readonly SoundEvent[] = [];
  private readonly coverBoxes: (AABB | null)[];
  private obstacleCoverIndex: number[] = [];
  private readonly rng: () => number;
  private nextId = 1;
  private botCounter = 0;
  private readonly events: GameEvent[] = [];
  private pendingSounds: SoundEvent[] = [];
  private pendingExplosions: { coverIndex: number; atTick: number; attackerId: number }[] = [];
  private navDirty = false;
  private navRebuildAtTick = 0;
  private readonly rttMs = new Map<number, number>();
  private readonly posHistory: HistoryEntry[] = [];
  private readonly nades: NadeEntity[] = [];
  private nextNadeId = 1;
  private readonly flashBlindUntil = new Map<number, number>();
  private readonly pendingStrikes: {
    attackerId: number;
    cause: 'airstrike' | 'cluster';
    exemptId?: number;
    x: number;
    z: number;
    atTick: number;
    radius: number;
    dmg: number;
  }[] = [];

  constructor(map: MapDef = MAPS.warehouse, opts: RoomOptions = {}) {
    this.map = map;
    this.killLimit = Math.max(1, Math.round(opts.killLimit ?? DEFAULT_KILL_LIMIT));
    this.durationSec = Math.max(15, Math.round(opts.durationSec ?? DEFAULT_MATCH_DURATION));
    this.magConfig = sanitizeMagConfig(opts.magConfig);
    this.timeLeft = this.durationSec;
    this.destructibles = createDestructibles(map.covers);
    this.dynamic = createDynamicCovers(map.covers);
    this.coverBoxes = map.covers.map((c) => coverToAABB(c));
    this.obstacles = [];
    this.rebuildObstacles();
    this.nav = this.bakeNav();
    this.rng = mulberry32(opts.seed ?? ((Math.random() * 0xffffffff) >>> 0));
    this.botDifficulty = opts.botDifficulty ?? 'mixed';
    for (let i = 0; i < (opts.bots ?? 0); i++) {
      const d = this.botDifficulty === 'mixed' ? mixedDifficulty(this.botCounter++) : this.botDifficulty;
      this.addPlayer(`BOT-${i + 1}`, true, d);
    }
  }

  /** [M10] 房间管理：实时调整 AI 数量（守恒席位、真人优先），返回实际数量 */
  setBotCount(target: number): number {
    const humans = this.players.filter((p) => !p.isBot).length;
    const want = Math.max(0, Math.min(MAX_PLAYERS - humans, Math.round(target)));
    let cur = this.players.filter((p) => p.isBot).length;
    while (cur > want) {
      const bot = [...this.players].reverse().find((p) => p.isBot);
      if (!bot) break;
      this.removePlayer(bot.id);
      cur--;
    }
    while (cur < want) {
      const d = this.botDifficulty === 'mixed' ? mixedDifficulty(this.botCounter++) : this.botDifficulty;
      this.addPlayer(`BOT-${++this.botCounter}`, true, d);
      cur++;
    }
    return cur;
  }

  /** [M10] 房间管理：即刻切换 AI 难度（存量 + 后续新增） */
  setBotDifficulty(d: BotDifficulty | 'mixed'): void {
    this.botDifficulty = d;
    for (const p of this.players) {
      if (!p.bot) continue;
      p.bot.difficulty = d === 'mixed' ? mixedDifficulty(this.botCounter++) : d;
    }
  }

  /** [M10] 房间管理：规则调整（killLimit / 时长分钟），返回是否生效 */
  setRules(killLimit: number, matchMinutes: number): void {
    this.killLimit = Math.max(1, Math.min(100, Math.round(killLimit)));
    this.durationSec = Math.max(60, Math.round(matchMinutes * 60));
    this.timeLeft = Math.min(this.timeLeft, this.durationSec);
  }

  /** [M14] 房间生效弹匣容量（人机同规则） */
  effectiveMag(w: WeaponId): number {
    return this.magConfig[w] ?? WEAPONS[w].magSize;
  }

  /** [M14] 即刻调整弹匣配置（单机语义：当前膛内不变，换弹上限与下次出生用新值） */
  setMagConfig(mags: MagConfig): void {
    this.magConfig = sanitizeMagConfig(mags);
  }

  private freshMags(): Record<WeaponId, number> {
    const o = {} as Record<WeaponId, number>;
    for (const id of WEAPON_LIST) o[id] = this.effectiveMag(id);
    return o;
  }

  private freshReserve(): Record<WeaponId, number> {
    const o = {} as Record<WeaponId, number>;
    for (const id of WEAPON_LIST) o[id] = effectiveReserve(id, this.effectiveMag(id));
    return o;
  }

  /** [M11] 死亡时修改 loadout，下一命生效；返回实际生效值 */
  setLoadout(id: number, loadout: Loadout): Loadout {
    const p = this.players.find((q) => q.id === id);
    if (!p) return { ...DEFAULT_LOADOUT };
    if (!p.alive && WEAPON_LIST.includes(loadout.primary) && WEAPON_LIST.includes(loadout.secondary)) {
      p.loadout = { primary: loadout.primary, secondary: loadout.secondary };
    }
    return { ...p.loadout };
  }

  botCount(): number {
    return this.players.filter((p) => p.isBot).length;
  }

  /** [M10] 组装 lobbyState 消息（pending 为联机「下局生效」的规则） */
  lobbyState(
    hostId: number | null,
    pending?: { killLimit?: number; matchMinutes?: number; mags?: MagConfig },
  ): Extract<S2CMessage, { kind: 'lobbyState' }> {
    return {
      kind: 'lobbyState',
      hostId,
      bots: this.botCount(),
      difficulty: this.botDifficulty,
      killLimit: this.killLimit,
      matchMinutes: Math.round(this.durationSec / 60),
      pendingKillLimit: pending?.killLimit,
      pendingMatchMinutes: pending?.matchMinutes,
      mags: this.magConfig,
      pendingMags: pending?.mags,
    };
  }

  private bakeNav(): NavGrid {
    const boxes: AABB[] = [];
    const destroyed = new Set(this.destructibles.filter((d) => d.destroyed).map((d) => d.coverIndex));
    for (let i = 0; i < this.map.covers.length; i++) {
      if (this.map.covers[i].dynamic || destroyed.has(i)) continue;
      boxes.push(coverToAABB(this.map.covers[i]));
    }
    return bakeNavGrid(this.map.size, boxes);
  }

  private rebuildObstacles(): void {
    this.obstacles.length = 0;
    this.obstacleCoverIndex = [];
    for (const s of this.dynamic.states) {
      const box = s.kind === 'door' && s.open >= 0.7 ? null : this.dynamic.boxOf(s);
      this.coverBoxes[s.coverIndex] = box;
    }
    for (let i = 0; i < this.coverBoxes.length; i++) {
      const b = this.coverBoxes[i];
      if (!b) continue;
      this.obstacles.push(b);
      this.obstacleCoverIndex.push(i);
    }
  }

  addPlayer(name: string, isBot: boolean, difficulty: BotDifficulty = 'normal', loadout: Loadout = DEFAULT_LOADOUT): ServerPlayer {
    const p: ServerPlayer = {
      id: this.nextId++,
      name,
      isBot,
      st: createMoveState(0, 0, 0),
      alive: false,
      health: 0,
      respawnAtTick: 0,
      loadout: { ...loadout },
      weapon: loadout.primary,
      mags: this.freshMags(),
      reserve: this.freshReserve(),
      recoil: createRecoilState(),
      pendingWeapon: null,
      switchEndsTick: 0,
      reloadEndsTick: -1,
      fireCooldown: 0,
      prevFireBtn: false,
      kills: 0,
      deaths: 0,
      streak: 0,
      bestStreak: 0,
      shotsFired: 0,
      shotsHit: 0,
      lastDamagedTick: -9999,
      spawnProtUntil: 0,
      lethal: 1,
      tactical: 1,
      cookingKind: null,
      cookingSinceTick: 0,
      meleeEndsTick: 0,
      meleeHitTick: 0,
      meleeCooldownUntil: 0,
      prevMeleeBtn: false,
      prevLethalBtn: false,
      prevTacticalBtn: false,
      streaks: createStreakState(),
      ackSeq: 0,
      lastRecvSeq: 0,
      inputQueue: [],
      lastInput: zeroInput(),
      bot: null,
    };
    this.players.push(p);
    if (isBot) {
      p.bot = createBrain(Math.floor(this.rng() * 0x7fffffff), difficulty, p.st.yaw);
    }
    this.spawn(p);
    return p;
  }

  spawn(p: ServerPlayer): void {
    const spawns = this.map.spawns;
    const ranked = spawns
      .map((s) => ({
        pos: s,
        score: Math.min(
          ...this.players.filter((q) => q !== p && q.alive).map((q) => Math.hypot(q.st.x - s[0], q.st.z - s[2])),
          Infinity,
        ),
      }))
      .sort((a, b) => b.score - a.score);
    const pick = ranked[Math.floor(this.rng() * Math.min(3, ranked.length))] ?? { pos: spawns[0] };
    Object.assign(p.st, createMoveState(pick.pos[0], pick.pos[1], pick.pos[2], this.rng() * Math.PI * 2));
    p.health = MAX_HEALTH;
    p.alive = true;
    if (p.isBot) p.loadout = randomBotLoadout(this.rng);
    p.weapon = p.loadout.primary;
    p.mags = this.freshMags();
    p.reserve = this.freshReserve();
    p.recoil = createRecoilState();
    p.pendingWeapon = null;
    p.reloadEndsTick = -1;
    p.fireCooldown = 0;
    p.prevFireBtn = false;
    p.lastDamagedTick = -9999;
    p.spawnProtUntil = this.tick + SPAWN_PROTECT_TICKS;
    p.lethal = 1;
    p.tactical = 1;
    p.cookingKind = null;
    p.meleeEndsTick = 0;
    p.meleeHitTick = 0;
    p.meleeCooldownUntil = 0;
    p.prevMeleeBtn = false;
    p.prevLethalBtn = false;
    p.prevTacticalBtn = false;
    // [M14] 重生不作废连杀奖励（死亡时仅计数器清零）
    p.inputQueue.length = 0;
    p.lastInput = zeroInput();
    if (p.bot) {
      p.bot.aimYaw = p.st.yaw;
      p.bot.moveGoal = null;
      p.bot.path = null;
      p.bot.targetId = null;
      p.bot.coverSpot = null;
      p.bot.peekSpot = null;
    }
    this.events.push({
      type: 'spawn',
      tick: this.tick,
      playerId: p.id,
      pos: { x: p.st.x, y: p.st.y, z: p.st.z },
    });
  }

  enqueueInput(id: number, input: InputMsg): void {
    const p = this.players.find((q) => q.id === id);
    if (!p || !input || typeof input.seq !== 'number') return;
    if (input.seq <= p.lastRecvSeq) return;
    const yawOk = Number.isFinite(input.yaw);
    const pitchOk = Number.isFinite(input.pitch);
    if (!yawOk || !pitchOk || !Number.isFinite(input.moveX) || !Number.isFinite(input.moveZ)) return;
    const sanitized: InputMsg = {
      seq: input.seq,
      moveX: Math.max(-1, Math.min(1, input.moveX)),
      moveZ: Math.max(-1, Math.min(1, input.moveZ)),
      yaw: input.yaw,
      pitch: Math.max(-1.55, Math.min(1.55, input.pitch)),
      buttons: Number.isFinite(input.buttons) ? input.buttons & 511 : 0,
      slot: Number.isInteger(input.slot) ? Math.max(0, Math.min(2, input.slot)) : 0,
      streak: typeof input.streak === 'number' && Number.isInteger(input.streak) ? Math.max(0, Math.min(3, input.streak)) : 0,
      streakTarget:
        input.streakTarget && Number.isFinite(input.streakTarget.x) && Number.isFinite(input.streakTarget.z)
          ? { x: input.streakTarget.x, z: input.streakTarget.z }
          : undefined,
      streakYaw: typeof input.streakYaw === 'number' && Number.isFinite(input.streakYaw) ? input.streakYaw : undefined,
    };
    p.lastRecvSeq = input.seq;
    p.inputQueue.push(sanitized);
    while (p.inputQueue.length > 8) p.inputQueue.shift();
  }

  addHuman(name: string, loadout: Loadout = DEFAULT_LOADOUT): ServerPlayer {
    if (this.players.length >= MAX_PLAYERS) {
      const bot = this.players.find((q) => q.isBot);
      if (bot) this.removePlayer(bot.id);
    }
    return this.addPlayer(name, false, 'normal', loadout);
  }

  playerLeft(id: number): void {
    if (!this.players.some((p) => p.id === id)) return;
    this.removePlayer(id);
    if (this.players.length < MAX_PLAYERS) {
      this.addPlayer(`BOT-${++this.botCounter}`, true, mixedDifficulty(this.botCounter));
    }
  }

  removePlayer(id: number): void {
    const idx = this.players.findIndex((p) => p.id === id);
    if (idx >= 0) this.players.splice(idx, 1);
  }

  isNavWalkable(x: number, z: number): boolean {
    const c = worldToCell(this.nav, x, z);
    return isWalkable(this.nav, c.cx, c.cz);
  }

  setRtt(id: number, ms: number): void {
    if (!Number.isFinite(ms)) return;
    this.rttMs.set(id, Math.max(0, Math.min(LAG_COMP_MAX_RTT_MS, Math.round(ms))));
  }

  private rewindTicksFor(id: number): number {
    const rtt = this.rttMs.get(id) ?? 0;
    return Math.min(LAG_COMP_MAX_TICKS, Math.round(((rtt / 2) / 1000) * TICK_RATE));
  }

  blocked(x: number, y: number, z: number, dx: number, dz: number, dist: number): boolean {
    return raycastBoxes(x, y, z, dx, 0, dz, dist, this.obstacles) !== null;
  }

  private takeInput(p: ServerPlayer): InputMsg {
    // [M15] 裁剪会整帧丢弃:一次性连杀意图必须合并进幸存帧,否则堆积时激活静默丢失
    while (p.inputQueue.length > 3) {
      const dropped = p.inputQueue.shift()!;
      if (dropped.streak !== 0) {
        const head = p.inputQueue[0];
        if (head.streak === 0) {
          head.streak = dropped.streak;
          head.streakTarget = dropped.streakTarget;
          head.streakYaw = dropped.streakYaw;
        }
      }
    }
    if (p.inputQueue.length > 0) {
      p.lastInput = p.inputQueue.shift()!;
      p.ackSeq = p.lastInput.seq;
    } else if (p.lastInput.streak !== 0) {
      // [M14] 空队列重放上一输入：连杀激活是一次性意图，不随重放重复触发
      p.lastInput = { ...p.lastInput, streak: 0, streakTarget: undefined, streakYaw: undefined };
    }
    return p.lastInput;
  }

  step(): void {
    if (this.over) return;
    this.tick++;
    this.timeLeft -= TICK_DT;
    this.dynamic.update(TICK_DT, this.tick, this.players.filter((p) => p.alive).map((p) => ({ x: p.st.x, z: p.st.z })));
    this.rebuildObstacles();
    this.collectSounds();
    this.currentSounds = this.pendingSounds;
    this.pendingSounds = [];
    for (const p of this.players) {
      if (!p.alive) {
        if (this.tick >= p.respawnAtTick) this.spawn(p);
        continue;
      }
      const input = p.isBot ? updateBot(this, p) : this.takeInput(p);
      stepMovement(p.st, input, this.obstacles, TICK_DT, p.pendingWeapon ?? p.weapon);
      this.combat(p, input);
      if (p.health < MAX_HEALTH && this.tick - p.lastDamagedTick > REGEN_DELAY_TICKS) {
        p.health = Math.min(MAX_HEALTH, p.health + REGEN_PER_TICK);
      }
    }
    this.posHistory.push({
      tick: this.tick,
      pos: new Map(this.players.map((p) => [p.id, { x: p.st.x, y: p.st.y, z: p.st.z, h: p.st.height }])),
    });
    if (this.posHistory.length > LAG_COMP_MAX_TICKS + 1) this.posHistory.shift();
    this.processExplosions();
    this.updateNades();
    this.processStrikes();
    if (this.navDirty && this.tick - this.navRebuildAtTick >= 30) {
      this.nav = this.bakeNav();
      this.navRebuildAtTick = this.tick;
      this.navDirty = false;
      for (const p of this.players) {
        if (p.bot) {
          p.bot.path = null;
          p.bot.repathAtTick = 0;
        }
      }
    }
    this.currentSounds = [];
    this.checkMatchEnd();
  }

  private processExplosions(): void {
    for (let i = this.pendingExplosions.length - 1; i >= 0; i--) {
      const e = this.pendingExplosions[i];
      if (this.tick < e.atTick) continue;
      this.pendingExplosions.splice(i, 1);
      this.explodeBarrel(e.attackerId, e.coverIndex);
    }
  }

  private explodeBarrel(attackerId: number, coverIndex: number): void {
    const d = this.destructibles.find((x) => x.coverIndex === coverIndex);
    if (!d || d.destroyed) return;
    const attacker = this.players.find((p) => p.id === attackerId) ?? null;
    d.destroyed = true;
    this.coverBoxes[coverIndex] = null;
    this.rebuildObstacles();
    this.navDirty = true;
    this.events.push({ type: 'explode', tick: this.tick, coverIndex, pos: { ...d.center }, attackerId });
    for (const q of this.players) {
      if (!q.alive) continue;
      const cx = d.center.x;
      const cy = d.center.y;
      const cz = d.center.z;
      const tx = q.st.x;
      const ty = q.st.y + q.st.height * 0.6;
      const tz = q.st.z;
      const dist = Math.hypot(tx - cx, ty - cy, tz - cz);
      const dmg = barrelDamage(dist);
      if (dmg <= 0) continue;
      const blockedHit = raycastBoxes(cx, cy, cz, (tx - cx) / dist, (ty - cy) / dist, (tz - cz) / dist, dist, this.obstacles);
      if (blockedHit && blockedHit.t < dist - 0.2) continue;
      if (attacker) this.applyDamage(attacker, q, 'body', dmg, 1, 'barrel');
    }
    for (const other of this.destructibles) {
      if (other.destroyed || other.kind !== 'barrel' || other.coverIndex === coverIndex) continue;
      const od = Math.hypot(other.center.x - d.center.x, other.center.z - d.center.z);
      if (od < BARREL_BLAST_RADIUS) {
        this.pendingExplosions.push({ coverIndex: other.coverIndex, atTick: this.tick + CHAIN_DELAY_TICKS, attackerId });
      }
    }
  }

  private damageCover(attacker: ServerPlayer | null, coverIndex: number, dmg: number): void {
    const d = this.destructibles.find((x) => x.coverIndex === coverIndex);
    if (!d || d.destroyed) return;
    d.hp -= dmg;
    if (d.hp > 0) return;
    if (d.kind === 'crate') {
      d.destroyed = true;
      this.coverBoxes[coverIndex] = null;
      this.rebuildObstacles();
      this.navDirty = true;
      this.events.push({ type: 'coverBreak', tick: this.tick, coverIndex, pos: { ...d.center } });
    } else {
      this.pendingExplosions.push({ coverIndex, atTick: this.tick, attackerId: attacker ? attacker.id : -1 });
    }
  }

  private collectSounds(): void {
    if (this.tick % 6 !== 0) return;
    for (const p of this.players) {
      if (!p.alive || !p.st.onGround) continue;
      const speed = Math.hypot(p.st.vx, p.st.vz);
      if (speed > 5) {
        this.pendingSounds.push({ x: p.st.x, z: p.st.z, sourceId: p.id, kind: 'step' });
      }
    }
  }

  private checkMatchEnd(): void {
    const leader = this.players.find((p) => p.kills >= this.killLimit);
    if (leader) {
      this.finish(leader.id);
    } else if (this.timeLeft <= 0) {
      let best: ServerPlayer | null = null;
      for (const p of this.players) {
        if (!best || p.kills > best.kills || (p.kills === best.kills && p.deaths < best.deaths)) best = p;
      }
      this.finish(best && best.kills > 0 ? best.id : null);
    }
  }

  private finish(winnerId: number | null): void {
    this.over = true;
    this.winner = winnerId;
    const standings: Standing[] = this.players
      .map((p) => ({ id: p.id, name: p.name, k: p.kills, d: p.deaths, sf: p.shotsFired, sh: p.shotsHit, bs: p.bestStreak }))
      .sort((a, b) => b.k - a.k || a.d - b.d);
    this.events.push({ type: 'gameOver', tick: this.tick, winnerId, standings });
  }

  private combat(p: ServerPlayer, input: InputMsg): void {
    const w = WEAPONS[p.weapon];
    const btn = input.buttons;
    const slotIdx = input.slot - 1;
    if (slotIdx >= 0 && slotIdx < 2 && this.tick >= p.meleeEndsTick) {
      const want = slotIdx === 0 ? p.loadout.primary : p.loadout.secondary;
      if (want !== p.weapon && p.pendingWeapon !== want) {
        p.pendingWeapon = want;
        p.switchEndsTick = this.tick + Math.round(WEAPONS[want].switchTime * TICK_RATE);
        p.reloadEndsTick = -1;
      }
    }
    if (p.pendingWeapon !== null && this.tick >= p.switchEndsTick) {
      p.weapon = p.pendingWeapon;
      p.pendingWeapon = null;
    }
    if (p.fireCooldown > 0) p.fireCooldown -= TICK_DT;
    if (p.reloadEndsTick >= 0 && this.tick >= p.reloadEndsTick) {
      const take = Math.min(this.effectiveMag(p.weapon) - p.mags[p.weapon], p.reserve[p.weapon]);
      p.mags[p.weapon] += take;
      p.reserve[p.weapon] -= take;
      p.reloadEndsTick = -1;
    }
    updateRecoil(p.recoil, this.tick * TICK_DT, TICK_DT);

    // 近战（V）
    const meleeBtn = (btn & BTN.MELEE) !== 0;
    const meleeFree = this.tick >= p.meleeEndsTick && this.tick >= p.meleeCooldownUntil && p.pendingWeapon === null;
    if (meleeBtn && !p.prevMeleeBtn && meleeFree) {
      p.meleeEndsTick = this.tick + MELEE_LOCK_TICKS;
      p.meleeHitTick = this.tick + MELEE_HIT_TICK;
      p.meleeCooldownUntil = this.tick + MELEE_COOLDOWN_TICKS;
      p.reloadEndsTick = -1;
      p.spawnProtUntil = 0;
    }
    if (p.meleeHitTick > 0 && this.tick >= p.meleeHitTick) {
      p.meleeHitTick = 0;
      this.resolveMelee(p, input);
    }
    p.prevMeleeBtn = meleeBtn;

    // 致命装备：手雷烹煮（G 按住拉栓、松手投出）
    const lethalHeld = (btn & BTN.LETHAL) !== 0;
    if (lethalHeld && !p.prevLethalBtn && p.lethal > 0 && p.cookingKind === null) {
      p.cookingKind = 'frag';
      p.cookingSinceTick = this.tick;
      p.lethal--;
      p.spawnProtUntil = 0;
    }
    if (!lethalHeld && p.cookingKind === 'frag') {
      this.throwNade(p, input, 'frag', p.cookingSinceTick);
      p.cookingKind = null;
    } else if (p.cookingKind === 'frag' && this.tick - p.cookingSinceTick >= FRAG_FUSE_TICKS) {
      p.cookingKind = null;
      this.explodeAt(p.id, p.st.x, p.st.y + p.st.height * 0.6, p.st.z, FRAG_RADIUS, FRAG_MAX_DMG, 'grenade');
    }
    p.prevLethalBtn = lethalHeld;

    // 战术装备：闪光（E 按下即投）
    const tacticalHeld = (btn & BTN.TACTICAL) !== 0;
    if (tacticalHeld && !p.prevTacticalBtn && p.tactical > 0) {
      p.tactical--;
      this.throwNade(p, input, 'flash', this.tick);
    }
    p.prevTacticalBtn = tacticalHeld;

    // 连杀奖励激活（4/5/6）
    const tier = typeof input.streak === 'number' ? input.streak : 0;
    if (tier >= 1 && tier <= 3) this.activateStreak(p, tier, input);

    const fireHeld = (btn & BTN.FIRE) !== 0;
    const fireEdge = fireHeld && !p.prevFireBtn;
    const canTrigger =
      fireHeld && (w.auto || fireEdge) && p.st.sprintLockT <= 0 && p.pendingWeapon === null && this.tick >= p.meleeEndsTick;
    if (canTrigger) {
      if (p.mags[p.weapon] <= 0) {
        if (p.reserve[p.weapon] > 0) this.startReload(p);
      } else if (p.fireCooldown <= 0 && p.reloadEndsTick < 0) {
        this.fire(p, input);
      }
    }
    if ((btn & BTN.RELOAD) !== 0 && p.reloadEndsTick < 0 && p.mags[p.weapon] < this.effectiveMag(p.weapon) && p.reserve[p.weapon] > 0) {
      this.startReload(p);
    }
    p.prevFireBtn = fireHeld;
  }

  private resolveMelee(p: ServerPlayer, input: InputMsg): void {
    const dir = viewDir(input.yaw, input.pitch);
    const ox = p.st.x;
    const oy = eyeY(p.st);
    const oz = p.st.z;
    const hit = this.castShot(p.id, ox, oy, oz, dir, this.rewindTicksFor(p.id));
    const victim = hit && hit.player && hit.t <= MELEE_RANGE ? hit.player : null;
    if (victim) this.applyDamage(p, victim, 'body', MELEE_DAMAGE, 1, 'melee');
    this.events.push({ type: 'melee', tick: this.tick, attackerId: p.id, victimId: victim ? victim.id : null });
    this.pendingSounds.push({ x: ox, z: oz, sourceId: p.id, kind: 'step' });
  }

  private throwNade(p: ServerPlayer, input: InputMsg, kind: 'frag' | 'flash', sinceTick: number): void {
    const dir = viewDir(input.yaw, input.pitch);
    const ox = p.st.x + dir.x * 0.3;
    const oy = eyeY(p.st) - 0.1;
    const oz = p.st.z + dir.z * 0.3;
    const vx = dir.x * GRENADE_THROW_SPEED;
    const vy = dir.y * GRENADE_THROW_SPEED + 3.5;
    const vz = dir.z * GRENADE_THROW_SPEED;
    const explodeAtTick = sinceTick + (kind === 'frag' ? FRAG_FUSE_TICKS : FLASH_FUSE_TICKS);
    const nadeId = this.nextNadeId++;
    this.nades.push({ id: nadeId, kind, ownerId: p.id, explodeAtTick, x: ox, y: oy, z: oz, vx, vy, vz });
    this.events.push({
      type: 'grenadeThrow',
      tick: this.tick,
      ownerId: p.id,
      nadeId,
      kind,
      pos: { x: ox, y: oy, z: oz },
      vel: { x: vx, y: vy, z: vz },
    });
  }

  private updateNades(): void {
    for (let i = this.nades.length - 1; i >= 0; i--) {
      const n = this.nades[i];
      stepProjectile(n, TICK_DT, this.obstacles, GRENADE_BOUNCE, GRENADE_RADIUS);
      if (this.tick < n.explodeAtTick) continue;
      this.nades.splice(i, 1);
      if (n.kind === 'frag') {
        this.explodeAt(n.ownerId, n.x, n.y, n.z, FRAG_RADIUS, FRAG_MAX_DMG, 'grenade');
      } else {
        this.detonateFlash(n);
      }
    }
  }

  private explodeAt(
    attackerId: number | null,
    cx: number,
    cy: number,
    cz: number,
    radius: number,
    maxDmg: number,
    cause: KillCause,
    exemptId?: number,
  ): void {
    const attacker = attackerId !== null ? this.players.find((p) => p.id === attackerId) ?? null : null;
    this.events.push({ type: 'blast', tick: this.tick, pos: { x: cx, y: cy, z: cz }, attackerId, cause });
    for (const q of this.players) {
      if (!q.alive) continue;
      if (exemptId !== undefined && q.id === exemptId) continue;
      const tx = q.st.x;
      const ty = q.st.y + q.st.height * 0.6;
      const tz = q.st.z;
      const dist = Math.hypot(tx - cx, ty - cy, tz - cz);
      if (dist > radius) continue;
      const dmg = dist < 1e-6 ? maxDmg : maxDmg * (1 - dist / radius);
      if (dmg <= 0) continue;
      if (dist > 0.5) {
        const blockedHit = raycastBoxes(cx, cy, cz, (tx - cx) / dist, (ty - cy) / dist, (tz - cz) / dist, dist, this.obstacles);
        if (blockedHit && blockedHit.t < dist - 0.2) continue;
      }
      if (attacker) this.applyDamage(attacker, q, 'body', dmg, 1, cause);
    }
    for (const d of this.destructibles) {
      if (d.destroyed) continue;
      const od = Math.hypot(d.center.x - cx, d.center.y - cy, d.center.z - cz);
      if (od < radius) this.damageCover(attacker, d.coverIndex, maxDmg * (1 - od / radius));
    }
  }

  private detonateFlash(n: NadeEntity): void {
    this.events.push({ type: 'flashPop', tick: this.tick, ownerId: n.ownerId, pos: { x: n.x, y: n.y, z: n.z } });
    for (const q of this.players) {
      if (!q.alive) continue;
      const dx = q.st.x - n.x;
      const dy = eyeY(q.st) - n.y;
      const dz = q.st.z - n.z;
      const dist = Math.hypot(dx, dy, dz);
      if (dist > FLASH_RADIUS) continue;
      if (dist > 0.5 && raycastBoxes(n.x, n.y, n.z, dx / dist, dy / dist, dz / dist, dist, this.obstacles)) continue;
      const vd = viewDir(q.st.yaw, q.st.pitch);
      const dot = dist < 1e-6 ? 1 : (vd.x * -dx + vd.y * -dy + vd.z * -dz) / dist;
      const facing = Math.max(0, dot);
      const distF = Math.max(0.25, 1 - dist / FLASH_RADIUS);
      const blindTicks = Math.round((FLASH_MAX_BLIND_MS / 1000) * TICK_RATE * (0.35 + 0.65 * facing) * distF);
      if (blindTicks > (this.flashBlindUntil.get(q.id) ?? 0)) this.flashBlindUntil.set(q.id, this.tick + blindTicks);
    }
  }

  isBlinded(id: number): boolean {
    return (this.flashBlindUntil.get(id) ?? 0) > this.tick;
  }

  private activateStreak(p: ServerPlayer, tier: number, input: InputMsg): void {
    if (!streakCanUse(p.streaks, tier)) return;
    streakConsume(p.streaks, tier);
    if (tier === 1) {
      this.events.push({ type: 'streakUse', tick: this.tick, playerId: p.id, tier: 1 });
      return;
    }
    // [M15] CoD 式放置：落点与航线来自客户端俯图，服务端只做边界 clamp
    const lim = this.map.size / 2 - 1;
    const clamp = (v: number): number => Math.max(-lim, Math.min(lim, v));
    const t = input.streakTarget ? { x: clamp(input.streakTarget.x), z: clamp(input.streakTarget.z) } : { x: p.st.x, z: p.st.z };
    const heading =
      typeof input.streakYaw === 'number' && Number.isFinite(input.streakYaw) ? input.streakYaw : input.yaw;
    this.events.push({
      type: 'streakUse',
      tick: this.tick,
      playerId: p.id,
      tier: tier as 2 | 3,
      target: { x: t.x, y: 0.5, z: t.z },
      yaw: heading,
    });
    if (tier === 2) {
      const dx = -Math.sin(heading);
      const dz = -Math.cos(heading);
      for (let i = 0; i < AIRSTRIKE_COUNT; i++) {
        const off = (i - (AIRSTRIKE_COUNT - 1) / 2) * AIRSTRIKE_SPACING;
        this.pendingStrikes.push({
          attackerId: p.id,
          cause: 'airstrike',
          exemptId: p.id,
          x: clamp(t.x + dx * off),
          z: clamp(t.z + dz * off),
          atTick: this.tick + AIRSTRIKE_DELAY_TICKS + i * 3,
          radius: AIRSTRIKE_RADIUS,
          dmg: AIRSTRIKE_DMG,
        });
      }
    } else {
      for (let i = 0; i < CLUSTER_COUNT; i++) {
        const ang = this.rng() * Math.PI * 2;
        const r = Math.sqrt(this.rng()) * CLUSTER_SCATTER;
        this.pendingStrikes.push({
          attackerId: p.id,
          cause: 'cluster',
          exemptId: p.id,
          x: clamp(t.x + Math.cos(ang) * r),
          z: clamp(t.z + Math.sin(ang) * r),
          atTick: this.tick + AIRSTRIKE_DELAY_TICKS + i * 2,
          radius: CLUSTER_RADIUS,
          dmg: CLUSTER_DMG,
        });
      }
    }
  }

  private processStrikes(): void {
    for (let i = this.pendingStrikes.length - 1; i >= 0; i--) {
      const s = this.pendingStrikes[i];
      if (this.tick < s.atTick) continue;
      this.pendingStrikes.splice(i, 1);
      this.explodeAt(s.attackerId, s.x, 0.6, s.z, s.radius, s.dmg, s.cause, s.exemptId);
    }
  }

  private startReload(p: ServerPlayer): void {
    const w = WEAPONS[p.weapon];
    const time = p.mags[p.weapon] === 0 ? w.reloadTime + EMPTY_RELOAD_EXTRA : w.reloadTime;
    p.reloadEndsTick = this.tick + Math.round(time * TICK_RATE);
  }

  private fire(p: ServerPlayer, input: InputMsg): void {
    const w = WEAPONS[p.weapon];
    p.spawnProtUntil = 0;
    p.fireCooldown = 60 / w.rpm;
    p.mags[p.weapon]--;
    p.shotsFired++;
    const ox = p.st.x;
    const oy = eyeY(p.st);
    const oz = p.st.z;
    const spread = ((input.buttons & BTN.ADS) !== 0 ? w.spreadAds : w.spreadHip) * spreadMulFor(p.st, input);
    const base = viewDir(input.yaw + p.recoil.yaw * DEG2RAD, input.pitch + p.recoil.pitch * DEG2RAD);
    const rewind = this.rewindTicksFor(p.id);
    let hitAny = false;
    for (let i = 0; i < w.pellets; i++) {
      const dir = jitterDir(base, spread, this.rng);
      const hit = this.castShot(p.id, ox, oy, oz, dir, rewind);
      const dist = hit ? hit.t : SHOT_MAX_DISTANCE;
      this.events.push({
        type: 'shot',
        tick: this.tick,
        shooterId: p.id,
        origin: { x: ox, y: oy, z: oz },
        end: { x: ox + dir.x * dist, y: oy + dir.y * dist, z: oz + dir.z * dist },
        weapon: p.weapon,
      });
      if (hit && hit.player) {
        this.applyDamage(p, hit.player, hit.part, w.damage * falloffMul(w, hit.t), w.headMul, p.weapon);
        hitAny = true;
      } else if (hit && hit.coverIndex >= 0) {
        this.damageCover(p, hit.coverIndex, w.damage);
      }
    }
    if (hitAny) p.shotsHit++;
    addRecoilShot(p.recoil, w, this.tick * TICK_DT, this.rng);
    this.pendingSounds.push({ x: ox, z: oz, sourceId: p.id, kind: 'shot' });
  }

  hasLineOfSight(a: ServerPlayer, b: ServerPlayer): boolean {
    const ox = a.st.x;
    const oy = eyeY(a.st);
    const oz = a.st.z;
    const tx = b.st.x;
    const ty = b.st.y + b.st.height * 0.7;
    const tz = b.st.z;
    const dx = tx - ox;
    const dy = ty - oy;
    const dz = tz - oz;
    const dist = Math.hypot(dx, dy, dz);
    if (dist < 1e-6) return true;
    return raycastBoxes(ox, oy, oz, dx / dist, dy / dist, dz / dist, dist, this.obstacles) === null;
  }

  private castShot(
    excludeId: number,
    ox: number,
    oy: number,
    oz: number,
    dir: { x: number; y: number; z: number },
    rewindTicks = 0,
  ): { t: number; player: ServerPlayer | null; part: 'head' | 'body'; coverIndex: number } | null {
    let best: { t: number; player: ServerPlayer | null; part: 'head' | 'body'; coverIndex: number } | null = null;
    for (let i = 0; i < this.obstacles.length; i++) {
      const t = rayBox(ox, oy, oz, dir.x, dir.y, dir.z, this.obstacles[i], SHOT_MAX_DISTANCE);
      if (t !== null && (best === null || t < best.t)) {
        best = { t, player: null, part: 'body', coverIndex: this.obstacleCoverIndex[i] };
      }
    }
    let rewound: Map<number, { x: number; y: number; z: number; h: number }> | null = null;
    if (rewindTicks > 0) {
      const targetTick = this.tick - rewindTicks;
      for (let i = this.posHistory.length - 1; i >= 0; i--) {
        if (this.posHistory[i].tick <= targetTick) {
          rewound = this.posHistory[i].pos;
          break;
        }
      }
    }
    for (const q of this.players) {
      if (q.id === excludeId || !q.alive) continue;
      let bx = q.st.x;
      let by = q.st.y;
      let bz = q.st.z;
      let bh = q.st.height;
      const r = rewound?.get(q.id);
      if (r) {
        bx = r.x;
        by = r.y;
        bz = r.z;
        bh = r.h;
      }
      const body = boxCenter(bx, by + (bh - 0.35) / 2, bz, 0.8, bh - 0.35, 0.8);
      const head = boxCenter(bx, by + bh - 0.175, bz, 0.36, 0.35, 0.36);
      const tBody = rayBox(ox, oy, oz, dir.x, dir.y, dir.z, body, SHOT_MAX_DISTANCE);
      const tHead = rayBox(ox, oy, oz, dir.x, dir.y, dir.z, head, SHOT_MAX_DISTANCE);
      for (const [t, part] of [
        [tBody, 'body'],
        [tHead, 'head'],
      ] as const) {
        if (t !== null && (best === null || t < best.t)) best = { t, player: q, part, coverIndex: -1 };
      }
    }
    return best;
  }

  private applyDamage(
    attacker: ServerPlayer,
    victim: ServerPlayer,
    part: 'head' | 'body',
    damage: number,
    headMul: number,
    cause: KillCause = attacker.weapon,
  ): void {
    if (this.tick < victim.spawnProtUntil) return;
    const dmg = part === 'head' ? damage * headMul : damage;
    victim.health -= dmg;
    victim.lastDamagedTick = this.tick;
    this.events.push({
      type: 'hit',
      tick: this.tick,
      attackerId: attacker.id,
      victimId: victim.id,
      part,
      damage: dmg,
      attackerPos: { x: attacker.st.x, y: attacker.st.y, z: attacker.st.z },
    });
    if (victim.health <= 0) this.killPlayer(attacker, victim, cause, part === 'head');
  }

  private killPlayer(killer: ServerPlayer, victim: ServerPlayer, cause: KillCause, hs: boolean): void {
    victim.alive = false;
    victim.deaths++;
    victim.streak = 0;
    victim.spawnProtUntil = 0;
    victim.cookingKind = null;
    victim.respawnAtTick = this.tick + RESPAWN_DELAY_TICKS;
    victim.st.vx = 0;
    victim.st.vy = 0;
    victim.st.vz = 0;
    const suicide = killer === victim || cause === 'suicide';
    if (!suicide) {
      killer.kills++;
      killer.streak++;
      killer.bestStreak = Math.max(killer.bestStreak, killer.streak);
      for (const tier of streakOnKill(killer.streaks, killer.streak)) {
        this.events.push({ type: 'streakEarned', tick: this.tick, playerId: killer.id, tier: (tier + 1) as 1 | 2 | 3 });
      }
    }
    // [M14] 死亡不作废连杀奖励：仅计数器清零（上文 victim.streak = 0），已获未用奖励保留
    this.events.push({
      type: 'kill',
      tick: this.tick,
      killerId: killer.id,
      victimId: victim.id,
      weapon: killer.weapon,
      streak: suicide ? 0 : killer.streak,
      hs: hs && !suicide,
      cause: suicide ? 'suicide' : cause,
    });
  }

  snapshot(): Extract<S2CMessage, { kind: 'snapshot' }> {
    const players: PlayerSnap[] = this.players.map((p) => ({
      id: p.id,
      name: p.name,
      x: round3(p.st.x),
      y: round3(p.st.y),
      z: round3(p.st.z),
      vx: round3(p.st.vx),
      vy: round3(p.st.vy),
      vz: round3(p.st.vz),
      yaw: round3(p.st.yaw),
      pitch: round3(p.st.pitch),
      h: p.st.height,
      sl: p.st.sliding,
      hp: Math.max(0, Math.round(p.health)),
      a: p.alive,
      w: p.weapon,
      m: p.mags[p.weapon],
      rs: p.reserve[p.weapon],
      rl: p.reloadEndsTick >= 0 ? Math.round((p.reloadEndsTick - this.tick) * TICK_DT * 10) / 10 : 0,
      k: p.kills,
      d: p.deaths,
      sf: p.shotsFired,
      sh: p.shotsHit,
      bs: p.bestStreak,
      sp: p.alive ? Math.max(0, Math.round((p.spawnProtUntil - this.tick) * TICK_DT * 10) / 10) : 0,
      st: p.streak,
      sv: streakAvailableMask(p.streaks),
      le: p.lethal,
      ta: p.tactical,
      ps: p.st.sliding ? 2 : !p.st.onGround ? 3 : p.st.crouching ? 1 : 0,
    }));
    const acks: Record<number, number> = {};
    for (const p of this.players) acks[p.id] = p.ackSeq;
    const destroyed = this.destructibles.filter((d) => d.destroyed).map((d) => d.coverIndex);
    const nades: NadeSnap[] = this.nades.map((n) => ({
      i: n.id,
      x: round3(n.x),
      y: round3(n.y),
      z: round3(n.z),
      k: n.kind,
    }));
    return {
      kind: 'snapshot',
      tick: this.tick,
      timeLeft: this.timeLeft,
      acks,
      players,
      destroyed,
      dyn: this.dynamic.values(),
      nades,
    };
  }

  drainEvents(): GameEvent[] {
    const evs = this.events.slice();
    this.events.length = 0;
    return evs;
  }
}

function round3(v: number): number {
  return Math.round(v * 1000) / 1000;
}

interface HistoryEntry {
  tick: number;
  pos: Map<number, { x: number; y: number; z: number; h: number }>;
}

interface NadeEntity extends ProjectileState {
  id: number;
  kind: 'frag' | 'flash';
  ownerId: number;
  explodeAtTick: number;
}
