import {
  BTN,
  type GameEvent,
  type InputMsg,
  MAPS,
  type MapDef,
  MAX_HEALTH,
  MAX_PLAYERS,
  MATCH_KILL_LIMIT,
  type PlayerSnap,
  REGEN_DELAY_TICKS,
  REGEN_PER_TICK,
  RESPAWN_DELAY_TICKS,
  SHOT_MAX_DISTANCE,
  type Standing,
  TICK_RATE,
  TICK_DT,
  WEAPONS,
  WEAPON_SLOTS,
  type WeaponId,
  bakeNavGrid,
  boxCenter,
  coverToAABB,
  createMoveState,
  eyeY,
  type MoveState,
  type NavGrid,
  rayBox,
  raycastBoxes,
  type AABB,
  stepMovement,
  viewDir,
  isWalkable,
  worldToCell,
  type S2CMessage,
} from 'shared';
import { mulberry32 } from './rng';
import { createBrain, mixedDifficulty, updateBot, type BotBrain, type BotDifficulty } from '../ai/controller';
import type { SoundEvent } from '../ai/perception';
import { createDestructibles, barrelDamage, CHAIN_DELAY_TICKS, BARREL_BLAST_RADIUS, type DestructibleState } from './destructible';
import { createDynamicCovers, type DynamicCoverManager } from './dynamicCover';

export interface ServerPlayer {
  id: number;
  name: string;
  isBot: boolean;
  st: MoveState;
  alive: boolean;
  health: number;
  respawnAtTick: number;
  weapon: WeaponId;
  mags: Record<WeaponId, number>;
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
}

function zeroInput(): InputMsg {
  return { seq: 0, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons: 0, slot: 0 };
}

function freshMags(): Record<WeaponId, number> {
  return { ar: WEAPONS.ar.magSize, sg: WEAPONS.sg.magSize, sr: WEAPONS.sr.magSize };
}

export class Room {
  readonly map: MapDef;
  readonly obstacles: AABB[];
  nav: NavGrid;
  readonly players: ServerPlayer[] = [];
  readonly destructibles: DestructibleState[];
  readonly dynamic: DynamicCoverManager;
  tick = 0;
  timeLeft = 600;
  over = false;
  winner: number | null = null;
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

  constructor(map: MapDef = MAPS.warehouse, opts: RoomOptions = {}) {
    this.map = map;
    this.destructibles = createDestructibles(map.covers);
    this.dynamic = createDynamicCovers(map.covers);
    this.coverBoxes = map.covers.map((c) => coverToAABB(c));
    this.obstacles = [];
    this.rebuildObstacles();
    this.nav = this.bakeNav();
    this.rng = mulberry32(opts.seed ?? ((Math.random() * 0xffffffff) >>> 0));
    const difficulty = opts.botDifficulty ?? 'mixed';
    for (let i = 0; i < (opts.bots ?? 0); i++) {
      const d = difficulty === 'mixed' ? mixedDifficulty(this.botCounter++) : difficulty;
      this.addPlayer(`BOT-${i + 1}`, true, d);
    }
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

  addPlayer(name: string, isBot: boolean, difficulty: BotDifficulty = 'normal'): ServerPlayer {
    const p: ServerPlayer = {
      id: this.nextId++,
      name,
      isBot,
      st: createMoveState(0, 0, 0),
      alive: false,
      health: 0,
      respawnAtTick: 0,
      weapon: 'ar',
      mags: freshMags(),
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
    p.mags = freshMags();
    p.pendingWeapon = null;
    p.reloadEndsTick = -1;
    p.fireCooldown = 0;
    p.prevFireBtn = false;
    p.lastDamagedTick = -9999;
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
      buttons: Number.isFinite(input.buttons) ? input.buttons & 63 : 0,
      slot: Number.isInteger(input.slot) ? Math.max(0, Math.min(3, input.slot)) : 0,
    };
    p.lastRecvSeq = input.seq;
    p.inputQueue.push(sanitized);
    while (p.inputQueue.length > 8) p.inputQueue.shift();
  }

  addHuman(name: string): ServerPlayer {
    if (this.players.length >= MAX_PLAYERS) {
      const bot = this.players.find((q) => q.isBot);
      if (bot) this.removePlayer(bot.id);
    }
    return this.addPlayer(name, false);
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

  blocked(x: number, y: number, z: number, dx: number, dz: number, dist: number): boolean {
    return raycastBoxes(x, y, z, dx, 0, dz, dist, this.obstacles) !== null;
  }

  private takeInput(p: ServerPlayer): InputMsg {
    while (p.inputQueue.length > 3) p.inputQueue.shift();
    if (p.inputQueue.length > 0) {
      p.lastInput = p.inputQueue.shift()!;
      p.ackSeq = p.lastInput.seq;
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
      stepMovement(p.st, input, this.obstacles);
      this.combat(p, input);
      if (p.health < MAX_HEALTH && this.tick - p.lastDamagedTick > REGEN_DELAY_TICKS) {
        p.health = Math.min(MAX_HEALTH, p.health + REGEN_PER_TICK);
      }
    }
    this.processExplosions();
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
      if (attacker) this.applyDamage(attacker, q, 'body', dmg, 1);
    }
    for (const other of this.destructibles) {
      if (other.destroyed || other.kind !== 'barrel' || other.coverIndex === coverIndex) continue;
      const od = Math.hypot(other.center.x - d.center.x, other.center.z - d.center.z);
      if (od < BARREL_BLAST_RADIUS) {
        this.pendingExplosions.push({ coverIndex: other.coverIndex, atTick: this.tick + CHAIN_DELAY_TICKS, attackerId });
      }
    }
  }

  private damageCover(attacker: ServerPlayer, coverIndex: number, dmg: number): void {
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
      this.pendingExplosions.push({ coverIndex, atTick: this.tick, attackerId: attacker.id });
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
    const leader = this.players.find((p) => p.kills >= MATCH_KILL_LIMIT);
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
    const slotIdx = input.slot - 1;
    if (slotIdx >= 0 && slotIdx < WEAPON_SLOTS.length) {
      const want = WEAPON_SLOTS[slotIdx];
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
      p.mags[p.weapon] = WEAPONS[p.weapon].magSize;
      p.reloadEndsTick = -1;
    }
    const btn = input.buttons;
    const moving = input.moveX !== 0 || input.moveZ !== 0;
    const sprinting = (btn & BTN.SPRINT) !== 0 && (btn & BTN.CROUCH) === 0 && moving;
    const fireHeld = (btn & BTN.FIRE) !== 0;
    const fireEdge = fireHeld && !p.prevFireBtn;
    const canTrigger =
      fireHeld && (w.auto || fireEdge) && !sprinting && !p.st.sliding && p.pendingWeapon === null;
    if (canTrigger) {
      if (p.mags[p.weapon] <= 0) {
        this.startReload(p);
      } else if (p.fireCooldown <= 0 && p.reloadEndsTick < 0) {
        this.fire(p, input);
      }
    }
    if ((btn & BTN.RELOAD) !== 0 && p.reloadEndsTick < 0 && p.mags[p.weapon] < WEAPONS[p.weapon].magSize) {
      this.startReload(p);
    }
    p.prevFireBtn = fireHeld;
  }

  private startReload(p: ServerPlayer): void {
    p.reloadEndsTick = this.tick + Math.round(WEAPONS[p.weapon].reloadTime * TICK_RATE);
  }

  private fire(p: ServerPlayer, input: InputMsg): void {
    const w = WEAPONS[p.weapon];
    p.fireCooldown = 60 / w.rpm;
    p.mags[p.weapon]--;
    p.shotsFired++;
    const ox = p.st.x;
    const oy = eyeY(p.st);
    const oz = p.st.z;
    const spread = (input.buttons & BTN.ADS) !== 0 ? w.spreadAds : w.spreadHip;
    const base = viewDir(input.yaw, input.pitch);
    let hitAny = false;
    for (let i = 0; i < w.pellets; i++) {
      const dir = jitterDir(base, spread, this.rng);
      const hit = this.castShot(p.id, ox, oy, oz, dir);
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
        this.applyDamage(p, hit.player, hit.part, w.damage, w.headMul);
        hitAny = true;
      } else if (hit && hit.coverIndex >= 0) {
        this.damageCover(p, hit.coverIndex, w.damage);
      }
    }
    if (hitAny) p.shotsHit++;
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
  ): { t: number; player: ServerPlayer | null; part: 'head' | 'body'; coverIndex: number } | null {
    let best: { t: number; player: ServerPlayer | null; part: 'head' | 'body'; coverIndex: number } | null = null;
    for (let i = 0; i < this.obstacles.length; i++) {
      const t = rayBox(ox, oy, oz, dir.x, dir.y, dir.z, this.obstacles[i], SHOT_MAX_DISTANCE);
      if (t !== null && (best === null || t < best.t)) {
        best = { t, player: null, part: 'body', coverIndex: this.obstacleCoverIndex[i] };
      }
    }
    for (const q of this.players) {
      if (q.id === excludeId || !q.alive) continue;
      const body = boxCenter(q.st.x, q.st.y + (q.st.height - 0.35) / 2, q.st.z, 0.8, q.st.height - 0.35, 0.8);
      const head = boxCenter(q.st.x, q.st.y + q.st.height - 0.175, q.st.z, 0.36, 0.35, 0.36);
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
  ): void {
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
    if (victim.health <= 0) this.killPlayer(attacker, victim);
  }

  private killPlayer(killer: ServerPlayer, victim: ServerPlayer): void {
    victim.alive = false;
    victim.deaths++;
    victim.streak = 0;
    victim.respawnAtTick = this.tick + RESPAWN_DELAY_TICKS;
    victim.st.vx = 0;
    victim.st.vy = 0;
    victim.st.vz = 0;
    killer.kills++;
    killer.streak++;
    killer.bestStreak = Math.max(killer.bestStreak, killer.streak);
    this.events.push({
      type: 'kill',
      tick: this.tick,
      killerId: killer.id,
      victimId: victim.id,
      weapon: killer.weapon,
      streak: killer.streak,
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
      k: p.kills,
      d: p.deaths,
      sf: p.shotsFired,
      sh: p.shotsHit,
      bs: p.bestStreak,
    }));
    const acks: Record<number, number> = {};
    for (const p of this.players) acks[p.id] = p.ackSeq;
    const destroyed = this.destructibles.filter((d) => d.destroyed).map((d) => d.coverIndex);
    return { kind: 'snapshot', tick: this.tick, timeLeft: this.timeLeft, acks, players, destroyed, dyn: this.dynamic.values() };
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

export function jitterDir(
  base: { x: number; y: number; z: number },
  spread: number,
  rng: () => number,
): { x: number; y: number; z: number } {
  if (spread <= 0) return base;
  const upX = Math.abs(base.y) < 0.99 ? 0 : 1;
  const upY = Math.abs(base.y) < 0.99 ? 1 : 0;
  let rx = -base.z * upY;
  let ry = base.z * upX;
  let rz = base.x * upY - base.y * upX;
  const rLen = Math.hypot(rx, ry, rz) || 1;
  rx /= rLen;
  ry /= rLen;
  rz /= rLen;
  const ux = ry * base.z - rz * base.y;
  const uy = rz * base.x - rx * base.z;
  const uz = rx * base.y - ry * base.x;
  const a = rng() * Math.PI * 2;
  const r = Math.sqrt(rng()) * spread;
  const ca = Math.cos(a) * r;
  const sa = Math.sin(a) * r;
  const dx = base.x + rx * ca + ux * sa;
  const dy = base.y + ry * ca + uy * sa;
  const dz = base.z + rz * ca + uz * sa;
  const len = Math.hypot(dx, dy, dz);
  return { x: dx / len, y: dy / len, z: dz / len };
}
