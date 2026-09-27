import './style.css';
import * as THREE from 'three';
import {
  BTN,
  DEG2RAD,
  FLASH_MAX_BLIND_MS,
  FLASH_RADIUS,
  MAPS,
  PLAYER_EYE_RATIO,
  SHOT_MAX_DISTANCE,
  TICK_DT,
  WEAPONS,
  WEAPON_LIST,
  type Loadout,
  type MagConfig,
  addRecoilShot,
  createRecoilState,
  defaultMagConfig,
  eyeY,
  type GameEvent,
  jitterDir,
  type PlayerSnap,
  type S2CMessage,
  sanitizeMagConfig,
  spreadMulFor,
  updateRecoil,
  viewDir,
  type WeaponId,
} from 'shared';
import { InputSystem, defaultSettings, type Settings } from './input';
import { loadSettings, saveSettings } from './persist';
import { Predictor } from './predict';
import { Hud } from './hud';
import { createScene } from './render/scene';
import { MapView } from './render/mapView';
import { RemoteViews, ENEMY_COLOR_RGB } from './render/actors';
import { Effects, impactKindAt } from './render/effects';
import { ViewModel } from './render/viewmodel';
import { Minimap, type EnemyBlip } from './render/minimap';
import { Placement } from './placement';
import { Killcam } from './render/killcam';
import { AudioSys } from './audio';
import { LocalSession, NetSession, sanitizeLoadout, type Session } from './transport';
import { GamepadSys } from './gamepad';
import { ASSIST_LEVELS, applyAimAssist } from './aimassist';

const canvas = document.getElementById('game') as HTMLCanvasElement;
const ctx = createScene(canvas);
let mapView = new MapView(ctx.scene, MAPS.warehouse);
ctx.scene.add(ctx.camera);
const remotes = new RemoteViews(ctx.scene);
const fx = new Effects(ctx.scene);
const vm = new ViewModel();
vm.resize(window.innerWidth / window.innerHeight);
window.addEventListener('resize', () => vm.resize(window.innerWidth / window.innerHeight));
const hud = new Hud();
const audio = new AudioSys();
const minimap = new Minimap(MAPS.warehouse);
const placement = new Placement(MAPS.warehouse, document.getElementById('tacmap') as HTMLCanvasElement);
const settings: Settings = { ...defaultSettings, ...loadSettings() };
// [M14] 本地存储的弹匣配置可能损坏/越界，统一清洗
settings.mags = sanitizeMagConfig(settings.mags);

const overlay = document.getElementById('startoverlay') as HTMLDivElement;
const endoverlay = document.getElementById('endoverlay') as HTMLDivElement;
const pauseoverlay = document.getElementById('pauseoverlay') as HTMLDivElement;
const pausetitle = document.getElementById('pausetitle') as HTMLHeadingElement;
let gameState: 'menu' | 'playing' | 'ended' = 'menu';
let sessionIsLocal = true;

const input = new InputSystem(document.body, () => {}, settings);
const gamepad = new GamepadSys();
let prevPadPause = false;
let prevPadSwitch = false;
let assistTouched = false;
let assistPrevYaw = 0;
let assistPrevPitch = 0;
const inputdev = document.getElementById('inputdev') as HTMLDivElement | null;
let sessionPaused = false;
document.addEventListener('pointerlockchange', () => {
  if (!document.pointerLockElement && gameState === 'playing') showPause();
});

let session: Session | null = null;
let predictor = new Predictor();
let selfId = -1;
let selfSnap: PlayerSnap | undefined;
let lastPlayers: PlayerSnap[] = [];
let sessionTimeLeft = 600;
let matchKillLimit = 30;
const nameById = new Map<number, string>();
const blips = new Map<number, EnemyBlip>();
const remoteStepAt = new Map<number, number>();
let ownStepAt = 0;
let fov = settings.fov;
const recoil = createRecoilState();
let camRoll = 0;
let shake = 0;
let desiredWeapon: WeaponId = 'ar';
let prevRl = 0;
let prevFire = false;
let localSwitchBusyUntil = 0;
let localMag = 0;
let localFireCd = 0;
let cookStart = 0;
let prevLethalHeld = false;
let uavUntilLocal = 0;
let breath = 1;
let breathPhase = 0;
let swayYaw = 0;
let swayPitch = 0;
let prevYawForSway = 0;
let prevPitchForSway = 0;
let reloadTotalMax = 0;
let announcedStart = false;
let myLoadout: Loadout = sanitizeLoadout(settings.loadout);
const killcamRec = new Killcam();
let kcReplay: { killerId: number; deathAt: number; startAt: number } | null = null;
let killcamViews: RemoteViews | null = null;

function showPause(): void {
  if (input.placementActive) {
    placement.close();
    input.placementActive = false;
  }
  if (sessionIsLocal && session instanceof LocalSession && !sessionPaused) {
    session.pause();
    sessionPaused = true;
  }
  pausetitle.textContent = sessionIsLocal ? '已暂停' : '菜单';
  pauseoverlay.classList.add('show');
}

function hidePause(): void {
  pauseoverlay.classList.remove('show');
}

function resumeFromPause(): void {
  hidePause();
  if (sessionIsLocal && session instanceof LocalSession && sessionPaused) {
    session.resume();
    sessionPaused = false;
  }
  audio.resume();
  input.lock();
}

document.getElementById('resumebtn')!.addEventListener('click', resumeFromPause);
document.getElementById('pauserestartbtn')!.addEventListener('click', () => {
  hidePause();
  sessionPaused = false;
  startSession();
  input.lock();
});
document.getElementById('pausemenubtn')!.addEventListener('click', () => {
  hidePause();
  sessionPaused = false;
  session?.dispose();
  session = null;
  gameState = 'menu';
  overlay.style.display = 'flex';
});

// [M10] 联机房主房间设置面板（单机复用，立即生效）
type LobbyStateMsg = Extract<S2CMessage, { kind: 'lobbyState' }>;
let lobby: LobbyStateMsg | null = null;
let lobbyEditing = false;
const pbotsrange = document.getElementById('pbotsrange') as HTMLInputElement;
const pdiffsel = document.getElementById('pdiffsel') as HTMLSelectElement;
const pkillrange = document.getElementById('pkillrange') as HTMLInputElement;
const pdurrange = document.getElementById('pdurrange') as HTMLInputElement;
const pbotsval = document.getElementById('pbotsval') as HTMLSpanElement;
const pkillval = document.getElementById('pkillval') as HTMLSpanElement;
const pdurval = document.getElementById('pdurval') as HTMLSpanElement;
const plobbyhint = document.getElementById('plobbyhint') as HTMLDivElement;
/** [M14] 弹匣配置输入（mag_=主菜单 / pmag_=暂停面板），顺序同 WEAPON_LIST */
const magInputs = WEAPON_LIST.map((id) => document.getElementById(`mag_${id}`) as HTMLInputElement);
const pmagInputs = WEAPON_LIST.map((id) => document.getElementById(`pmag_${id}`) as HTMLInputElement);
const pmagreset = document.getElementById('pmagreset') as HTMLButtonElement;

function readMagInputs(inputs: HTMLInputElement[]): MagConfig {
  const o = {} as MagConfig;
  WEAPON_LIST.forEach((id, i) => (o[id] = Number(inputs[i].value)));
  return sanitizeMagConfig(o);
}

function writeMagInputs(inputs: HTMLInputElement[], mags: MagConfig): void {
  WEAPON_LIST.forEach((id, i) => (inputs[i].value = String(mags[id])));
}

function lobbyEditable(): boolean {
  return sessionIsLocal || (lobby !== null && lobby.hostId === selfId);
}

function syncLobbyUI(): void {
  const editable = lobbyEditable();
  for (const c of [pbotsrange, pdiffsel, pkillrange, pdurrange, ...pmagInputs, pmagreset]) c.disabled = !editable;
  if (sessionIsLocal) plobbyhint.textContent = '单机模式：修改立即生效';
  else if (lobby === null) plobbyhint.textContent = '';
  else if (lobby.hostId === selfId) {
    const pend =
      lobby.pendingKillLimit != null || lobby.pendingMatchMinutes != null
        ? `（待下局生效：${lobby.pendingKillLimit ?? lobby.killLimit} 杀 / ${lobby.pendingMatchMinutes ?? lobby.matchMinutes} 分钟）`
        : '';
    const magPend = lobby.pendingMags != null ? '，弹匣改动待下局' : '';
    plobbyhint.textContent = `你是房主：AI 设置立即生效，规则改动下局生效${magPend}${pend}`;
  } else {
    plobbyhint.textContent = '仅房主可修改';
  }
  if (lobby && !lobbyEditing) {
    pbotsrange.value = String(lobby.bots);
    pbotsval.textContent = String(lobby.bots);
    pdiffsel.value = lobby.difficulty;
    pkillrange.value = String(lobby.pendingKillLimit ?? lobby.killLimit);
    pkillval.textContent = String(lobby.pendingKillLimit ?? lobby.killLimit);
    pdurrange.value = String(lobby.pendingMatchMinutes ?? lobby.matchMinutes);
    pdurval.textContent = `${lobby.pendingMatchMinutes ?? lobby.matchMinutes} 分钟`;
    if (lobby.pendingMags ?? lobby.mags) writeMagInputs(pmagInputs, (lobby.pendingMags ?? lobby.mags)!);
  }
}

function sendLobby(): void {
  if (!lobbyEditable() || !session) return;
  session.send({
    kind: 'lobby',
    bots: Number(pbotsrange.value),
    difficulty: pdiffsel.value as 'mixed' | 'easy' | 'normal' | 'hard',
    killLimit: Number(pkillrange.value),
    matchMinutes: Number(pdurrange.value),
    mags: readMagInputs(pmagInputs),
  });
}

for (const c of [pbotsrange, pkillrange, pdurrange]) {
  c.addEventListener('pointerdown', () => (lobbyEditing = true));
  c.addEventListener('input', () => {
    pbotsval.textContent = pbotsrange.value;
    pkillval.textContent = pkillrange.value;
    pdurval.textContent = `${pdurrange.value} 分钟`;
    sendLobby();
  });
  c.addEventListener('change', () => {
    lobbyEditing = false;
    sendLobby();
  });
}
pdiffsel.addEventListener('change', () => sendLobby());
// [M14] 弹匣输入：编辑期间置 lobbyEditing 防回显覆盖，input/change 同现有滑条模式
for (const c of pmagInputs) {
  c.addEventListener('pointerdown', () => (lobbyEditing = true));
  c.addEventListener('input', () => sendLobby());
  c.addEventListener('change', () => {
    lobbyEditing = false;
    sendLobby();
  });
}
pmagreset.addEventListener('click', () => {
  writeMagInputs(pmagInputs, defaultMagConfig());
  sendLobby();
});

function startSession(): void {
  session?.dispose();
  session = null;
  predictor = new Predictor();
  remotes.clear();
  blips.clear();
  remoteStepAt.clear();
  hud.reset();
  mapView.dispose();
  mapView = new MapView(ctx.scene, MAPS.warehouse);
  nameById.clear();
  selfId = -1;
  selfSnap = undefined;
  sessionTimeLeft = 600;
  matchKillLimit = 30;
  endKillcamReplay();
  seq = 0;
  acc = 0;
  recoil.pitch = 0;
  recoil.yaw = 0;
  recoil.lastShotAt = -1e9;
  prevRl = 0;
  prevFire = false;
  localSwitchBusyUntil = 0;
  localMag = 0;
  localFireCd = 0;
  desiredWeapon = myLoadout.primary;
  vm.setWeapon(myLoadout.primary);
  fov = settings.fov;
  breath = 1;
  breathPhase = 0;
  swayYaw = 0;
  swayPitch = 0;
  announcedStart = false;
  endKillcamReplay();
  gameState = 'playing';
  sessionPaused = false;
  lobby = null;
  syncLobbyUI();
  overlay.style.display = 'none';
  hidePause();
  const mode = (document.getElementById('modesel') as HTMLSelectElement).value;
  const name = (document.getElementById('nameinput') as HTMLInputElement).value.trim() || '玩家';
  settings.name = name;
  sessionIsLocal = mode !== 'net';
  if (mode === 'net') {
    const raw = (document.getElementById('serverinput') as HTMLInputElement).value.trim();
    const url = raw || `ws://${location.host}`;
    session = new NetSession(url, name, myLoadout);
  } else {
    session = new LocalSession({
      name,
      loadout: myLoadout,
      bots: settings.bots,
      botDifficulty: settings.difficulty,
      killLimit: settings.killLimit,
      durationSec: settings.matchMinutes * 60,
      magConfig: settings.mags,
    });
  }
  session.onMessage(onMessage);
  session.onDisconnect(() => {
    if (gameState !== 'playing' && gameState !== 'ended') return;
    session = null;
    gameState = 'menu';
    hidePause();
    overlay.style.display = 'flex';
    hud.showKill('⚠ 与服务器断开连接');
  });
}

hud.onRestart = () => {
  endoverlay.classList.remove('show');
  startSession();
  input.lock();
};
hud.onMenu = () => {
  endoverlay.classList.remove('show');
  session?.dispose();
  session = null;
  gameState = 'menu';
  overlay.style.display = 'flex';
};

function onMessage(raw: S2CMessage): void {
  if (raw.kind === 'welcome') {
    selfId = raw.playerId;
    if (raw.cfg) matchKillLimit = raw.cfg.killLimit;
    if (raw.loadout) {
      myLoadout = sanitizeLoadout(raw.loadout);
      syncLoadoutUI();
    }
  } else if (raw.kind === 'loadoutAck') {
    myLoadout = sanitizeLoadout(raw.loadout);
    settings.loadout = myLoadout;
    saveSettings(settings);
    syncLoadoutUI();
  } else if (raw.kind === 'snapshot') {
    selfSnap = raw.players.find((p) => p.id === selfId);
    lastPlayers = raw.players;
    sessionTimeLeft = raw.timeLeft;
    killcamRec.pushSnapshot(raw.players, performance.now() / 1000);
    mapView.applySnapshot(raw.destroyed, raw.dyn);
    fx.nadesSync(raw.nades ?? []);
    if (selfSnap) {
      predictor.reconcile(selfSnap, raw.acks[selfId] ?? 0);
      localMag = selfSnap.m;
      if (selfSnap.rl > 0 && prevRl <= 0) audio.playLocal('reload');
      if (selfSnap.rl > 0) reloadTotalMax = Math.max(reloadTotalMax, selfSnap.rl);
      else reloadTotalMax = 0;
      prevRl = selfSnap.rl;
      if (selfSnap.w !== vm.activeWeapon && selfSnap.w !== desiredWeapon) {
        desiredWeapon = selfSnap.w;
        vm.setWeapon(selfSnap.w);
      }
    }
    for (const p of raw.players) nameById.set(p.id, p.name);
    remotes.pushAll(raw.players, selfId, performance.now() / 1000);
  } else if (raw.kind === 'events') {
    for (const e of raw.events) onEvent(e);
  } else if (raw.kind === 'lobbyState') {
    lobby = raw;
    syncLobbyUI();
  }
}

/** [M11] 同步 loadout 选择 UI（主菜单 + 死亡等待界面） */
function syncLoadoutUI(): void {
  const ps = document.getElementById('primarysel') as HTMLSelectElement | null;
  const ss = document.getElementById('secondarysel') as HTMLSelectElement | null;
  const dps = document.getElementById('dprimarysel') as HTMLSelectElement | null;
  const dss = document.getElementById('dsecondarysel') as HTMLSelectElement | null;
  if (ps) ps.value = myLoadout.primary;
  if (ss) ss.value = myLoadout.secondary;
  if (dps) dps.value = myLoadout.primary;
  if (dss) dss.value = myLoadout.secondary;
}

/** [M11] loadout 变更：存活时仅本地记录（菜单选择），死亡时发送服务器下一命生效 */
function applyLoadoutChange(source: 'menu' | 'death'): void {
  const ps = document.getElementById('primarysel') as HTMLSelectElement;
  const ss = document.getElementById('secondarysel') as HTMLSelectElement;
  myLoadout = sanitizeLoadout({
    primary: ps.value as WeaponId,
    secondary: ss.value as WeaponId,
  });
  settings.loadout = myLoadout;
  saveSettings(settings);
  syncLoadoutUI();
  if (source === 'death' && session && selfSnap && selfSnap.a === false) {
    session.send({ kind: 'loadout', loadout: myLoadout });
  }
}

function normAngle(a: number): number {
  let d = a % (Math.PI * 2);
  if (d > Math.PI) d -= Math.PI * 2;
  if (d < -Math.PI) d += Math.PI * 2;
  return d;
}

function onEvent(e: GameEvent): void {
  const now = performance.now();
  killcamRec.pushEvent(e, now / 1000);
  if (e.type === 'shot') {
    if (e.shooterId === selfId) return;
    const o3 = new THREE.Vector3(e.origin.x, e.origin.y, e.origin.z);
    const e3 = new THREE.Vector3(e.end.x, e.end.y, e.end.z);
    const dirv = e3.clone().sub(o3).normalize();
    fx.tracer(o3, e3);
    fx.muzzle(o3, dirv);
    fx.impact(e3, dirv, impactKindAt(e3.x, e3.y, e3.z));
    audio.playAt(`${e.weapon}_shot` as 'ar_shot', e.origin.x, e.origin.y, e.origin.z);
    blips.set(e.shooterId, { x: e.origin.x, z: e.origin.z, until: now + 2000 });
  } else if (e.type === 'hit') {
    if (e.victimId === selfId) {
      hud.damageFlash();
      const s = predictor.state;
      const yawTo = Math.atan2(-(e.attackerPos.x - s.x), -(e.attackerPos.z - s.z));
      hud.damageDir(normAngle(yawTo - input.yaw));
      if (settings.padRumble) gamepad.rumble(now, 60, 0.4, 0.6);
    } else {
      const pose = remotes.getPose(e.victimId);
      if (pose) fx.blood(new THREE.Vector3(pose.x, pose.y + pose.h * 0.65, pose.z));
    }
    if (e.attackerId === selfId) {
      hud.hitmarkerShow(e.part === 'head');
      audio.playLocal(e.part === 'head' ? 'headshot' : 'hit');
      shake = Math.max(shake, 0.06);
    }
  } else if (e.type === 'kill') {
    const killer = nameById.get(e.killerId) ?? '?';
    const victim = nameById.get(e.victimId) ?? '?';
    hud.addKill(killer, victim, e.hs ?? false, e.cause ?? e.weapon);
    if (e.victimId !== selfId) {
      const vp = remotes.getPose(e.victimId);
      const ap = e.killerId === selfId ? predictor.state : remotes.getPose(e.killerId);
      if (vp && ap) {
        remotes.kill(e.victimId, new THREE.Vector3(vp.x - ap.x, 0, vp.z - ap.z));
      }
    }
    if (e.victimId === selfId) {
      if (input.placementActive) {
        placement.close();
        input.placementActive = false;
      }
      hud.showDeath(killer, e.cause ?? e.weapon);
      hud.showKill(`击杀回放：${killer}`);
      if (e.killerId !== selfId) {
        kcReplay = { killerId: e.killerId, deathAt: now / 1000, startAt: now / 1000 };
        killcamViews = new RemoteViews(ctx.scene);
        remotes.setVisible(false);
      }
    }
    if (e.killerId === selfId) {
      if (e.streak >= 8) hud.showKill('⚡ 疯狂杀戮！');
      else if (e.streak === 5) hud.showKill('五连杀！');
      else if (e.streak === 3) hud.showKill('三连杀！');
      else hud.showKill(victim);
      audio.playLocal('kill');
      if (settings.padRumble) gamepad.rumble(now, 50, 0.5, 0.3);
    }
  } else if (e.type === 'coverBreak') {
    mapView.breakCover(e.coverIndex);
    fx.debris(new THREE.Vector3(e.pos.x, e.pos.y, e.pos.z));
    audio.playAt('break', e.pos.x, e.pos.y, e.pos.z);
  } else if (e.type === 'explode') {
    const p = new THREE.Vector3(e.pos.x, e.pos.y, e.pos.z);
    mapView.breakCover(e.coverIndex);
    fx.explosion(p);
    audio.playAt('explode', p.x, p.y, p.z);
    const s = predictor.state;
    const dist = Math.hypot(p.x - s.x, p.y - s.y - 1, p.z - s.z);
    if (dist < 9) {
      shake = Math.max(shake, 0.4 * (1 - dist / 9));
      if (settings.padRumble) gamepad.rumble(now, 120, 0.6, 1);
    }
  } else if (e.type === 'spawn') {
    if (e.playerId === selfId) {
      hud.hideDeath();
      endKillcamReplay();
      if (!announcedStart) {
        announcedStart = true;
        audio.announce(`自由混战开始，先达到${matchKillLimit}杀获胜`);
      }
    }
  } else if (e.type === 'grenadeThrow') {
    fx.nadeThrow(e.nadeId, e.kind, new THREE.Vector3(e.pos.x, e.pos.y, e.pos.z));
    audio.playAt('throw', e.pos.x, e.pos.y, e.pos.z);
  } else if (e.type === 'flashPop') {
    audio.playAt('flash', e.pos.x, e.pos.y, e.pos.z);
    const s0 = predictor.state;
    const ex = s0.x;
    const ey = s0.y + s0.height * 0.9;
    const ez = s0.z;
    const dx = ex - e.pos.x;
    const dy = ey - e.pos.y;
    const dz = ez - e.pos.z;
    const dist = Math.hypot(dx, dy, dz);
    if (dist <= FLASH_RADIUS && selfSnap?.a) {
      const tHit = dist > 0.5 ? predictor.raycastObstacles(e.pos.x, e.pos.y, e.pos.z, dx / dist, dy / dist, dz / dist) : null;
      const blocked = tHit !== null && tHit < dist - 0.2;
      if (!blocked) {
        const vd = viewDir(input.yaw, input.pitch);
        const dot = dist < 1e-6 ? 1 : (vd.x * -dx + vd.y * -dy + vd.z * -dz) / dist;
        const facing = Math.max(0, dot);
        const distF = Math.max(0.25, 1 - dist / FLASH_RADIUS);
        hud.flash(FLASH_MAX_BLIND_MS * (0.35 + 0.65 * facing) * distF);
      }
    }
  } else if (e.type === 'melee') {
    if (e.attackerId === selfId) {
      audio.playLocal('melee');
      vm.kick();
    } else {
      const pose = remotes.getPose(e.attackerId);
      if (pose) audio.playAt('melee', pose.x, pose.y + 1.4, pose.z);
    }
  } else if (e.type === 'blast') {
    const p = new THREE.Vector3(e.pos.x, e.pos.y, e.pos.z);
    fx.explosion(p);
    audio.playAt('explode', p.x, p.y, p.z);
    const s = predictor.state;
    const dist = Math.hypot(p.x - s.x, p.y - s.y - 1, p.z - s.z);
    if (dist < 10) {
      shake = Math.max(shake, 0.45 * (1 - dist / 10));
      if (settings.padRumble) gamepad.rumble(now, 120, 0.6, 1);
    }
  } else if (e.type === 'streakEarned') {
    if (e.playerId === selfId) {
      const names = { 1: 'UAV', 2: '精准空袭', 3: '集束炸弹' } as Record<number, string>;
      hud.showKill(`${names[e.tier]} 就绪！按 ${3 + e.tier} 激活`);
      audio.playLocal('streak');
      audio.announce(`${names[e.tier]}已就绪`);
    }
  } else if (e.type === 'streakUse') {
    if (e.playerId === selfId) {
      if (e.tier === 1) {
        uavUntilLocal = now + 15000;
        hud.showKill('UAV 已启动：敌人位置已标记');
      } else {
        hud.showKill(e.tier === 2 ? '精准空袭已呼叫！' : '集束炸弹已投放！');
      }
      audio.playLocal('uav');
    } else if (e.tier === 1) {
      hud.showKill('⚠ 敌方 UAV 已启动');
      audio.playLocal('uav');
      audio.announce('警告，敌方无人侦察机已升空');
    } else if (e.target) {
      audio.playAt('uav', e.target.x, 2, e.target.z);
    }
  } else if (e.type === 'gameOver') {
    gameState = 'ended';
    document.exitPointerLock();
    hud.showEnd(e.winnerId === selfId, e.standings, selfId);
    audio.announce(e.winnerId === selfId ? '胜利！' : '比赛结束');
  }
}

function endKillcamReplay(): void {
  kcReplay = null;
  if (killcamViews) {
    killcamViews.clear();
    killcamViews = null;
  }
  if (gameState === 'playing') remotes.setVisible(true);
}

function applyEnemyVisuals(): void {
  const shells = settings.enemyOutline && settings.quality !== 'low';
  remotes.setEnemyVisuals(settings.enemyOutline, settings.enemyColor, shells);
  minimap.setEnemyColor(ENEMY_COLOR_RGB[settings.enemyColor]);
  placement.setEnemyColor(ENEMY_COLOR_RGB[settings.enemyColor]);
}

function applyMenuSettings(): void {
  const el = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
  (el<HTMLInputElement>('nameinput')).value = settings.name;
  (el<HTMLInputElement>('botsrange')).value = String(settings.bots);
  (el<HTMLInputElement>('killrange')).value = String(settings.killLimit);
  (el<HTMLInputElement>('durrange')).value = String(settings.matchMinutes);
  (el<HTMLInputElement>('sensrange')).value = String(Math.round(settings.sensitivity * 10000));
  (el<HTMLInputElement>('volrange')).value = String(Math.round(settings.volume * 100));
  (el<HTMLInputElement>('fovrange')).value = String(settings.fov);
  (el<HTMLInputElement>('shadowcheck')).checked = settings.shadow;
  (el<HTMLInputElement>('ttscheck')).checked = settings.tts;
  (el<HTMLInputElement>('brightrange')).value = String(Math.round(settings.brightness * 100));
  (el<HTMLInputElement>('outlinecheck')).checked = settings.enemyOutline;
  (el<HTMLInputElement>('adssensrange')).value = String(Math.round(settings.adsSens * 100));
  (el<HTMLSelectElement>('assistsel')).value = settings.aimAssist;
  (el<HTMLInputElement>('padsensrange')).value = String(Math.round(settings.padSensitivity * 10));
  (el<HTMLInputElement>('paddzrange')).value = String(Math.round(settings.padDeadzone * 100));
  (el<HTMLInputElement>('padrumblecheck')).checked = settings.padRumble;
  (el<HTMLSelectElement>('diffsel')).value = settings.difficulty;
  (el<HTMLSelectElement>('qualitysel')).value = settings.quality;
  (el<HTMLSelectElement>('enemycolorsel')).value = settings.enemyColor;
  // [M14] 主菜单弹匣配置：回显 + 变更保存 + 恢复默认
  writeMagInputs(magInputs, settings.mags);
  for (const input of magInputs) {
    input.addEventListener('change', () => {
      settings.mags = readMagInputs(magInputs);
      saveSettings(settings);
    });
  }
  const magreset = el<HTMLButtonElement>('magreset');
  magreset.addEventListener('click', () => {
    settings.mags = defaultMagConfig();
    writeMagInputs(magInputs, settings.mags);
    saveSettings(settings);
  });

  el<HTMLInputElement>('nameinput').addEventListener('input', () => {
    settings.name = el<HTMLInputElement>('nameinput').value.trim().slice(0, 16) || '玩家';
    saveSettings(settings);
  });
  const bindRange = (
    id: string,
    labelId: string,
    fmt: (v: number) => string,
    cb: (v: number) => void,
  ) => {
    const input = el<HTMLInputElement>(id);
    const label = document.getElementById(labelId)!;
    const apply = () => {
      cb(Number(input.value));
      label.textContent = fmt(Number(input.value));
      saveSettings(settings);
    };
    input.addEventListener('input', apply);
    apply();
  };
  bindRange('botsrange', 'botsval', (v) => String(v), (v) => (settings.bots = v));
  bindRange('killrange', 'killval', (v) => `${v} 杀`, (v) => (settings.killLimit = v));
  bindRange('durrange', 'durval', (v) => `${v} 分钟`, (v) => (settings.matchMinutes = v));
  bindRange('sensrange', 'sensval', (v) => (v / 10).toFixed(1), (v) => (settings.sensitivity = v / 10000));
  bindRange('volrange', 'volval', (v) => `${v}%`, (v) => {
    settings.volume = v / 100;
    audio.setVolume(v / 100);
  });
  bindRange('fovrange', 'fovval', (v) => String(v), (v) => (settings.fov = v));
  bindRange('brightrange', 'brightval', (v) => `${v}%`, (v) => {
    settings.brightness = v / 100;
    ctx.setBrightness(settings.brightness);
  });
  bindRange('adssensrange', 'adssensval', (v) => `${v}%`, (v) => (settings.adsSens = v / 100));
  const assistsel = el<HTMLSelectElement>('assistsel');
  assistsel.addEventListener('change', () => {
    settings.aimAssist = assistsel.value as Settings['aimAssist'];
    assistTouched = true;
    saveSettings(settings);
  });
  bindRange('padsensrange', 'padsensval', (v) => (v / 10).toFixed(1), (v) => (settings.padSensitivity = v / 10));
  bindRange('paddzrange', 'paddzval', (v) => (v / 100).toFixed(2), (v) => (settings.padDeadzone = v / 100));
  const padrumblecheck = el<HTMLInputElement>('padrumblecheck');
  padrumblecheck.addEventListener('change', () => {
    settings.padRumble = padrumblecheck.checked;
    saveSettings(settings);
  });
  const outlinecheck = el<HTMLInputElement>('outlinecheck');
  outlinecheck.addEventListener('change', () => {
    settings.enemyOutline = outlinecheck.checked;
    applyEnemyVisuals();
    saveSettings(settings);
  });
  const enemycolorsel = el<HTMLSelectElement>('enemycolorsel');
  enemycolorsel.addEventListener('change', () => {
    settings.enemyColor = enemycolorsel.value as Settings['enemyColor'];
    applyEnemyVisuals();
    saveSettings(settings);
  });
  const shadowcheck = el<HTMLInputElement>('shadowcheck');
  const applyShadows = () => {
    settings.shadow = shadowcheck.checked;
    ctx.renderer.shadowMap.enabled = shadowcheck.checked;
    ctx.sun.castShadow = shadowcheck.checked;
    ctx.scene.traverse((o) => {
      const mesh = o as THREE.Mesh;
      if (mesh.material) {
        if (Array.isArray(mesh.material)) mesh.material.forEach((m) => (m.needsUpdate = true));
        else (mesh.material as THREE.Material).needsUpdate = true;
      }
    });
    saveSettings(settings);
  };
  shadowcheck.addEventListener('change', applyShadows);
  applyShadows();
  const diffsel = el<HTMLSelectElement>('diffsel');
  diffsel.addEventListener('change', () => {
    settings.difficulty = diffsel.value as Settings['difficulty'];
    saveSettings(settings);
  });
  const qualitysel = el<HTMLSelectElement>('qualitysel');
  qualitysel.addEventListener('change', () => {
    settings.quality = qualitysel.value as Settings['quality'];
    fx.quality = settings.quality;
    ctx.setQuality(settings.quality);
    applyEnemyVisuals();
    saveSettings(settings);
  });
  qualitysel.dispatchEvent(new Event('change'));
  const ttscheck = el<HTMLInputElement>('ttscheck');
  ttscheck.addEventListener('change', () => {
    settings.tts = ttscheck.checked;
    audio.setTts(ttscheck.checked);
    saveSettings(settings);
  });
  ttscheck.dispatchEvent(new Event('change'));
}

applyMenuSettings();

// [M11] loadout 选择 UI：主菜单（7 选 2）+ 死亡等待界面（下一命生效）
{
  const fill = (sel: HTMLSelectElement) => {
    sel.innerHTML = '';
    for (const wid of WEAPON_LIST) {
      const opt = document.createElement('option');
      opt.value = wid;
      opt.textContent = WEAPONS[wid].name;
      sel.appendChild(opt);
    }
  };
  for (const id of ['primarysel', 'secondarysel', 'dprimarysel', 'dsecondarysel']) {
    fill(document.getElementById(id) as HTMLSelectElement);
  }
  syncLoadoutUI();
  document.getElementById('primarysel')!.addEventListener('change', () => applyLoadoutChange('menu'));
  document.getElementById('secondarysel')!.addEventListener('change', () => applyLoadoutChange('menu'));
  document.getElementById('dprimarysel')!.addEventListener('change', () => applyLoadoutChange('death'));
  document.getElementById('dsecondarysel')!.addEventListener('change', () => applyLoadoutChange('death'));
}
const modesel = document.getElementById('modesel') as HTMLSelectElement;
modesel.addEventListener('change', () => {
  const net = modesel.value === 'net';
  document.querySelectorAll<HTMLElement>('.netonly').forEach((el) => (el.style.display = net ? 'flex' : 'none'));
  document.querySelectorAll<HTMLElement>('.localonly').forEach((el) => (el.style.display = net ? 'none' : 'flex'));
});
document.getElementById('playbtn')!.addEventListener('click', () => {
  audio.resume();
  audio.playLocal('ui');
  if (gameState === 'playing') {
    input.lock();
    return;
  }
  startSession();
  input.lock();
});

let seq = 0;
let acc = 0;
let last = performance.now();

function frame(nowMs: number): void {
  requestAnimationFrame(frame);
  const dt = Math.min((nowMs - last) / 1000, 0.1);
  last = nowMs;
  const now = nowMs / 1000;
  const nowMs2 = nowMs;

  // [M12] 手柄轮询：动作合并进输入系统，与键鼠自动切换
  const pad = gamepad.poll(dt, settings);
  const padPauseEdge = pad.pause && !prevPadPause;
  const padSwitchEdge = pad.switchWeapon && !prevPadSwitch;
  prevPadPause = pad.pause;
  prevPadSwitch = pad.switchWeapon;
  input.pad = pad;
  if (padSwitchEdge) input.requestSlot(input.currentSlotHint === 1 ? 2 : 1);
  if (pad.streak > 0) {
    if (pad.streak === 1) input.requestStreak(1);
    else input.placementRequest = pad.streak as 2 | 3;
  }
  if (padPauseEdge) {
    if (gameState === 'playing') {
      if (sessionPaused) resumeFromPause();
      else {
        document.exitPointerLock();
        showPause();
      }
    }
  }
  if (gamepad.active && settings.aimAssist === 'off' && !assistTouched) {
    // 手柄检测到时辅助瞄准默认开（中档），仅自动设置一次
    settings.aimAssist = 'medium';
    assistTouched = true;
    saveSettings(settings);
    const assistSel = document.getElementById('assistsel') as HTMLSelectElement | null;
    if (assistSel) assistSel.value = settings.aimAssist;
  }
  if (inputdev) inputdev.textContent = gamepad.active ? '手柄' : '键鼠';
  gamepad.markPrev(pad);

  // [M15] 连杀放置模式：5/6 打开俯图（需对应奖励就绪）
  const reqTier = input.placementRequest;
  input.placementRequest = 0;
  if (reqTier >= 2 && gameState === 'playing' && selfSnap?.a && !kcReplay) {
    const svMask = selfSnap.sv ?? 0;
    if (svMask & (reqTier === 2 ? 2 : 4)) {
      const uavActive = nowMs2 < uavUntilLocal;
      const st0 = predictor.state;
      placement.open(
        reqTier as 2 | 3,
        { x: st0.x, z: st0.z, yaw: input.yaw },
        uavActive ? lastPlayers.filter((p) => p.id !== selfId && p.a).map((p) => ({ x: p.x, z: p.z })) : [],
      );
      input.placementActive = true;
    }
  }
  if (input.placementActive) {
    const d = input.drainPlacementDeltas();
    if (d.wheel !== 0) placement.rotate(d.wheel > 0 ? 1 : -1);
    placement.moveCursor(d.dx, d.dy);
    if (d.cancel) {
      placement.close();
      input.placementActive = false;
    } else if (d.confirm) {
      const r = placement.confirm();
      input.placementActive = false;
      if (r) input.requestStreakTargeted(r.streak, r.streakTarget, r.streakYaw);
    } else {
      const st1 = predictor.state;
      placement.updateSelf({ x: st1.x, z: st1.z, yaw: input.yaw });
    }
    placement.render();
  }

  if (gameState === 'playing' && selfId >= 0 && (input.locked || gamepad.active) && selfSnap?.a !== false) {
    acc = Math.min(acc + dt, 0.2);
    let steps = 0;
    while (acc >= TICK_DT && steps < 5) {
      const inp = input.buildInput(++seq, swayYaw, swayPitch);
      if (inp.slot > 0) {
        const want = inp.slot === 1 ? myLoadout.primary : myLoadout.secondary;
        if (want !== vm.activeWeapon) {
          desiredWeapon = want;
          vm.setWeapon(want);
          localSwitchBusyUntil = nowMs2 + WEAPONS[want].switchTime * 1000;
          localFireCd = Math.max(localFireCd, WEAPONS[want].switchTime);
          audio.playLocal('switch');
        }
      }
      predictor.step(inp, vm.activeWeapon);
      const fireNow = (inp.buttons & BTN.FIRE) !== 0;
      const fireEdgeNow = fireNow && !prevFire;
      if (fireEdgeNow && localMag === 0 && selfSnap && selfSnap.rs === 0) audio.playLocal('empty');
      prevFire = fireNow;
      if (localFireCd > 0) localFireCd -= TICK_DT;
      const w = WEAPONS[vm.activeWeapon];
      const canFireLocal =
        fireNow &&
        (w.auto || fireEdgeNow) &&
        predictor.state.sprintLockT <= 0 &&
        localMag > 0 &&
        selfSnap &&
        selfSnap.a &&
        selfSnap.rl === 0 &&
        nowMs2 >= localSwitchBusyUntil &&
        localFireCd <= 0;
      if (canFireLocal) {
        localFireCd = 60 / w.rpm;
        localMag--;
        const st = predictor.state;
        const ox = st.x;
        const oy = eyeY(st);
        const oz = st.z;
        const spread =
          ((inp.buttons & BTN.ADS) !== 0 ? w.spreadAds : w.spreadHip) * spreadMulFor(st, inp);
        const base = viewDir(input.yaw + swayYaw + recoil.yaw * DEG2RAD, input.pitch + swayPitch + recoil.pitch * DEG2RAD);
        const dir = jitterDir(base, spread, Math.random);
        const dist = predictor.raycastObstacles(ox, oy, oz, dir.x, dir.y, dir.z) ?? SHOT_MAX_DISTANCE;
        const fwd = new THREE.Vector3(dir.x, dir.y, dir.z);
        const rightV = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
        const origin = new THREE.Vector3(ox, oy, oz).addScaledVector(rightV, 0.12).add(new THREE.Vector3(0, -0.07, 0));
        const end = new THREE.Vector3(ox + dir.x * dist, oy + dir.y * dist, oz + dir.z * dist);
        fx.tracer(origin, end);
        fx.impact(end, fwd, impactKindAt(end.x, end.y, end.z));
        fx.shell(
          origin.clone().addScaledVector(fwd, 0.4),
          fwd,
          rightV,
        );
        fx.muzzle(origin.clone().addScaledVector(fwd, 0.75), fwd, 0.5);
        vm.kick();
        addRecoilShot(recoil, w, nowMs2 / 1000, Math.random);
        audio.playLocal(`${vm.activeWeapon}_shot` as 'ar_shot');
        if (settings.padRumble) gamepad.rumble(nowMs2, 10, 0.25, 0.15);
      }
      session!.send({ kind: 'input', input: inp });
      acc -= TICK_DT;
      steps++;
    }
    if (steps === 5) acc = 0;
  } else {
    acc = 0;
  }

  // [M12] 辅助瞄准（纯客户端视角层）：对最近可视敌人做粘滞+轻吸附
  const assistStrength = ASSIST_LEVELS[settings.aimAssist];
  if (
    assistStrength > 0 &&
    gameState === 'playing' &&
    selfId >= 0 &&
    (input.locked || gamepad.active) &&
    selfSnap?.a !== false &&
    !kcReplay
  ) {
    const st = predictor.state;
    const ex = st.x;
    const ey = eyeY(st);
    const ez = st.z;
    let best: { dyaw: number; dpitch: number; dist: number } | null = null;
    for (const p of lastPlayers) {
      if (p.id === selfId || !p.a) continue;
      const dx = p.x - ex;
      const dz = p.z - ez;
      const dist = Math.hypot(dx, dz);
      if (dist > 60 || dist < 0.5) continue;
      const ty = p.y + p.h * 0.7;
      const dy = ty - ey;
      const d3 = Math.hypot(dx, dy, dz);
      const hitT = predictor.raycastObstacles(ex, ey, ez, dx / d3, dy / d3, dz / d3);
      if (hitT !== null && hitT < d3 - 0.2) continue;
      const dyaw = normAngle(Math.atan2(-dx, -dz) - input.yaw);
      const dpitch = Math.atan2(dy, Math.hypot(dx, dz)) - input.pitch;
      const ang = Math.hypot(dyaw, dpitch);
      if (!best || ang < Math.hypot(best.dyaw, best.dpitch)) best = { dyaw, dpitch, dist };
    }
    const r = applyAimAssist({
      userYaw: input.yaw,
      userPitch: input.pitch,
      prevYaw: assistPrevYaw,
      prevPitch: assistPrevPitch,
      targetYaw: input.yaw + (best?.dyaw ?? 0),
      targetPitch: input.pitch + (best?.dpitch ?? 0),
      strength: best ? assistStrength : 0,
      dt,
    });
    input.yaw = r.yaw;
    input.pitch = r.pitch;
    assistPrevYaw = r.yaw;
    assistPrevPitch = r.pitch;
  } else {
    assistPrevYaw = input.yaw;
    assistPrevPitch = input.pitch;
  }

  const s = predictor.state;
  const alpha = Math.max(0, Math.min(1, acc / TICK_DT));
  updateRecoil(recoil, now, dt);
  shake = Math.max(0, shake - shake * 6 * dt);
  const shakeX = (Math.random() - 0.5) * shake * 0.25;
  const shakeY = (Math.random() - 0.5) * shake * 0.25;

  if (kcReplay) {
    const kc = kcReplay;
    const span = 3;
    const elapsed = now - kc.startAt;
    if (elapsed > span || input.fire) {
      endKillcamReplay();
    } else {
      const viewTime = kc.deathAt - span + elapsed;
      const players = killcamRec.sample(kc.deathAt, viewTime, span);
      if (players && killcamViews) {
        for (const ev of killcamRec.drainEvents(viewTime - dt - 0.066, viewTime)) {
          if (ev.type !== 'shot' || ev.shooterId !== kc.killerId) continue;
          const o3 = new THREE.Vector3(ev.origin.x, ev.origin.y, ev.origin.z);
          const e3 = new THREE.Vector3(ev.end.x, ev.end.y, ev.end.z);
          fx.tracer(o3, e3);
          fx.muzzle(o3, e3.clone().sub(o3).normalize());
          audio.playAt(`${ev.weapon}_shot` as 'ar_shot', ev.origin.x, ev.origin.y, ev.origin.z);
        }
        killcamViews.pushAll(players, kc.killerId, viewTime + 0.066);
        const killer = players.find((p) => p.id === kc.killerId);
        if (killer) {
          ctx.camera.position.set(killer.x + shakeX, killer.y + killer.h * 0.9 + shakeY, killer.z);
          ctx.camera.rotation.y = killer.yaw;
          ctx.camera.rotation.x = killer.pitch;
          ctx.camera.rotation.z = 0;
        }
        killcamViews.render(viewTime + 0.066, dt);
        vm.group.visible = false;
        minimap.render({ x: killer?.x ?? 0, z: killer?.z ?? 0, yaw: killer?.yaw ?? 0 }, blips, nowMs2);
      } else {
        endKillcamReplay();
      }
    }
  }
  if (!kcReplay) {
    const ex = THREE.MathUtils.lerp(predictor.prevX, s.x, alpha);
    const ey = THREE.MathUtils.lerp(predictor.prevY, s.y, alpha);
    const ez = THREE.MathUtils.lerp(predictor.prevZ, s.z, alpha);
    const eh = THREE.MathUtils.lerp(predictor.prevH, s.height, alpha) * PLAYER_EYE_RATIO;
    ctx.camera.position.set(ex + shakeX, ey + eh + shakeY, ez);
    ctx.camera.rotation.y = input.yaw + swayYaw + recoil.yaw * DEG2RAD;
    ctx.camera.rotation.x = input.pitch + swayPitch + recoil.pitch * DEG2RAD;
    camRoll += ((s.sliding ? -5 * DEG2RAD : 0) - camRoll) * Math.min(1, 10 * dt);
    ctx.camera.rotation.z = camRoll;
  }

  const w = WEAPONS[vm.activeWeapon];
  const targetFov = input.ads ? w.adsFov : settings.fov + (s.sprinting ? 10 : 0);
  // [M12] 开镜速度按武器 adsTime（时间常数 adsTime/3）；收镜维持原速率
  const fovRate = input.ads ? 1 - Math.exp(-dt / (w.adsTime / 3)) : Math.min(1, 12 * dt);
  fov += (targetFov - fov) * fovRate;
  ctx.camera.fov = fov;
  ctx.camera.updateProjectionMatrix();
  const scoped = vm.activeWeapon === 'sr' && input.ads && fov < 34;
  hud.setScope(scoped);

  // 狙击屏息：呼吸摇摆随视角上传（服务端权威弹道），Shift 稳镜耗气息
  if (scoped && selfSnap?.a !== false) {
    const hold = input.sprintHeld;
    breath = Math.max(0, Math.min(1, breath + (hold ? -dt / 5 : dt / 3.5)));
    breathPhase += dt * (hold ? 0.4 : 1.3);
    const ampDeg = breath <= 0 ? 1.2 : hold ? 0.06 : 0.6;
    swayPitch = Math.sin(breathPhase * 1.1) * ampDeg * DEG2RAD;
    swayYaw = Math.cos(breathPhase * 0.7) * ampDeg * 0.7 * DEG2RAD;
  } else {
    breath = Math.min(1, breath + dt / 3.5);
    swayYaw = 0;
    swayPitch = 0;
  }
  hud.setBreath(scoped ? breath : null);

  const speed = Math.hypot(s.vx, s.vz);
  const lookDX = input.yaw - prevYawForSway;
  const lookDY = input.pitch - prevPitchForSway;
  prevYawForSway = input.yaw;
  prevPitchForSway = input.pitch;
  vm.update({
    dt,
    speed,
    onGround: s.onGround,
    ads: input.ads,
    sprinting: s.sprinting,
    lookDX,
    lookDY,
    reloadT: selfSnap?.rl ?? 0,
    reloadTotal: reloadTotalMax,
    scoped,
  });
  vm.group.visible = !scoped && selfSnap?.a !== false && !kcReplay;

  if (selfSnap && selfSnap.a && s.onGround && speed > 1.5 && nowMs2 >= ownStepAt) {
    audio.playLocal('step');
    ownStepAt = nowMs2 + Math.max(280, Math.min(650, (2.2 / speed) * 1000));
  }
  remotes.forEach((id, pose) => {
    if (pose.speed < 1.5) return;
    const next = remoteStepAt.get(id) ?? 0;
    if (nowMs2 >= next) {
      audio.playAt('step', pose.x, pose.y + 1, pose.z);
      remoteStepAt.set(id, nowMs2 + Math.max(300, Math.min(700, (2.2 / pose.speed) * 1000)));
    }
  });

  audio.setListener(ctx.camera.position.x, ctx.camera.position.y, ctx.camera.position.z, -Math.sin(input.yaw), -Math.cos(input.yaw));

  const spreadPx = Math.round(3 + speed * 1.4 + (input.ads ? 0 : 5) + recoil.pitch * 18 + (s.onGround ? 0 : 6));
  hud.setSpread(spreadPx);
  if (input.lethalHeld && !prevLethalHeld && (selfSnap?.le ?? 0) > 0) cookStart = nowMs2;
  if (!input.lethalHeld) cookStart = 0;
  prevLethalHeld = input.lethalHeld;
  hud.setCook(cookStart > 0 ? ((nowMs2 - cookStart) / 1000).toFixed(1) : null);
  hud.update(selfSnap, sessionTimeLeft, nowMs2, session?.rtt ?? null, matchKillLimit);
  hud.setScoreboard(input.scoreboard && gameState === 'playing', lastPlayers, selfId, sessionIsLocal ? null : (lobby?.hostId ?? null));
  if (gameState === 'playing' && !kcReplay) {
    const uavActive = nowMs2 < uavUntilLocal;
    const uavEnemies = uavActive
      ? lastPlayers.filter((p) => p.id !== selfId && p.a).map((p) => ({ x: p.x, z: p.z }))
      : [];
    minimap.render({ x: s.x, z: s.z, yaw: input.yaw }, blips, nowMs2, uavEnemies);
  }

  if (!kcReplay) remotes.render(now, dt, ctx.camera.position);
  fx.update(dt);
  ctx.renderer.autoClear = true;
  if (ctx.composer) ctx.composer.render();
  else ctx.renderer.render(ctx.scene, ctx.camera);
  if (vm.group.visible) {
    ctx.renderer.autoClear = false;
    ctx.renderer.clearDepth();
    ctx.renderer.render(vm.scene, vm.camera);
    ctx.renderer.autoClear = true;
  }
}

requestAnimationFrame(frame);
