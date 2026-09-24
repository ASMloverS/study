import './style.css';
import * as THREE from 'three';
import {
  BTN,
  DEG2RAD,
  MAPS,
  PLAYER_EYE_RATIO,
  SHOT_MAX_DISTANCE,
  TICK_DT,
  WEAPONS,
  WEAPON_SLOTS,
  addRecoilShot,
  createRecoilState,
  eyeY,
  type GameEvent,
  jitterDir,
  type PlayerSnap,
  type S2CMessage,
  spreadMulFor,
  updateRecoil,
  viewDir,
  type WeaponId,
} from 'shared';
import { InputSystem, defaultSettings, type Settings } from './input';
import { Predictor } from './predict';
import { Hud } from './hud';
import { createScene } from './render/scene';
import { MapView } from './render/mapView';
import { RemoteViews } from './render/actors';
import { Effects, impactKindAt } from './render/effects';
import { ViewModel } from './render/viewmodel';
import { Minimap, type EnemyBlip } from './render/minimap';
import { AudioSys } from './audio';
import { LocalSession, NetSession, type Session } from './transport';

const canvas = document.getElementById('game') as HTMLCanvasElement;
const ctx = createScene(canvas);
let mapView = new MapView(ctx.scene, MAPS.warehouse);
ctx.scene.add(ctx.camera);
const remotes = new RemoteViews(ctx.scene);
const fx = new Effects(ctx.scene);
const vm = new ViewModel(ctx.camera);
const hud = new Hud();
const audio = new AudioSys();
const minimap = new Minimap(MAPS.warehouse);
const settings: Settings = { ...defaultSettings };

const overlay = document.getElementById('startoverlay') as HTMLDivElement;
const endoverlay = document.getElementById('endoverlay') as HTMLDivElement;
let gameState: 'menu' | 'playing' | 'ended' = 'menu';

const input = new InputSystem(document.body, () => {}, settings);
document.addEventListener('pointerlockchange', () => {
  if (!document.pointerLockElement && gameState === 'playing') overlay.style.display = 'flex';
});

let session: Session | null = null;
let predictor = new Predictor();
let selfId = -1;
let selfSnap: PlayerSnap | undefined;
let lastPlayers: PlayerSnap[] = [];
let sessionTimeLeft = 600;
const nameById = new Map<number, string>();
const blips = new Map<number, EnemyBlip>();
const remoteStepAt = new Map<number, number>();
let ownStepAt = 0;
let fov = settings.fov;
const recoil = createRecoilState();
let camRoll = 0;
let shake = 0;
let killcam: { id: number; until: number } | null = null;
let desiredWeapon: WeaponId = 'ar';
let prevRl = 0;
let prevFire = false;
let localSwitchBusyUntil = 0;
let localMag = 0;
let localFireCd = 0;

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
  killcam = null;
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
  desiredWeapon = 'ar';
  vm.setWeapon('ar');
  fov = settings.fov;
  gameState = 'playing';
  overlay.style.display = 'none';
  const mode = (document.getElementById('modesel') as HTMLSelectElement).value;
  if (mode === 'net') {
    const raw = (document.getElementById('serverinput') as HTMLInputElement).value.trim();
    const url = raw || `ws://${location.host}`;
    const name = (document.getElementById('nameinput') as HTMLInputElement).value.trim() || '玩家';
    session = new NetSession(url, name);
  } else {
    session = new LocalSession({ bots: settings.bots, botDifficulty: settings.difficulty });
  }
  session.onMessage(onMessage);
  session.onDisconnect(() => {
    if (gameState !== 'playing' && gameState !== 'ended') return;
    session = null;
    gameState = 'menu';
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
  } else if (raw.kind === 'snapshot') {
    selfSnap = raw.players.find((p) => p.id === selfId);
    lastPlayers = raw.players;
    sessionTimeLeft = raw.timeLeft;
    mapView.applySnapshot(raw.destroyed, raw.dyn);
    if (selfSnap) {
      predictor.reconcile(selfSnap, raw.acks[selfId] ?? 0);
      localMag = selfSnap.m;
      if (selfSnap.rl > 0 && prevRl <= 0) audio.playLocal('reload');
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
    hud.addKill(killer, victim);
    if (e.victimId === selfId) {
      hud.showDeath(killer);
      hud.showKill(`视角：${killer}`);
      killcam = { id: e.killerId, until: now + 3000 };
    }
    if (e.killerId === selfId) {
      if (e.streak >= 8) hud.showKill('⚡ 疯狂杀戮！');
      else if (e.streak === 5) hud.showKill('五连杀！');
      else if (e.streak === 3) hud.showKill('三连杀！');
      else hud.showKill(victim);
      audio.playLocal('kill');
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
    if (dist < 9) shake = Math.max(shake, 0.4 * (1 - dist / 9));
  } else if (e.type === 'spawn') {
    if (e.playerId === selfId) {
      hud.hideDeath();
      killcam = null;
    }
  } else if (e.type === 'gameOver') {
    gameState = 'ended';
    document.exitPointerLock();
    hud.showEnd(e.winnerId === selfId, e.standings, selfId);
  }
}

function applyMenuSettings(): void {
  const bindRange = (
    id: string,
    labelId: string,
    fmt: (v: number) => string,
    cb: (v: number) => void,
  ) => {
    const el = document.getElementById(id) as HTMLInputElement;
    const label = document.getElementById(labelId)!;
    const apply = () => {
      cb(Number(el.value));
      label.textContent = fmt(Number(el.value));
    };
    el.addEventListener('input', apply);
    apply();
  };
  bindRange('botsrange', 'botsval', (v) => String(v), (v) => (settings.bots = v));
  bindRange('sensrange', 'sensval', (v) => (v / 10).toFixed(1), (v) => (settings.sensitivity = v / 10000));
  bindRange('volrange', 'volval', (v) => `${v}%`, (v) => {
    settings.volume = v / 100;
    audio.setVolume(v / 100);
  });
  bindRange('fovrange', 'fovval', (v) => String(v), (v) => (settings.fov = v));
  const shadowcheck = document.getElementById('shadowcheck') as HTMLInputElement;
  const applyShadows = () => {
    ctx.renderer.shadowMap.enabled = shadowcheck.checked;
    ctx.sun.castShadow = shadowcheck.checked;
    ctx.scene.traverse((o) => {
      const mesh = o as THREE.Mesh;
      if (mesh.material) {
        if (Array.isArray(mesh.material)) mesh.material.forEach((m) => (m.needsUpdate = true));
        else (mesh.material as THREE.Material).needsUpdate = true;
      }
    });
  };
  shadowcheck.addEventListener('change', applyShadows);
  const diffsel = document.getElementById('diffsel') as HTMLSelectElement;
  diffsel.addEventListener('change', () => (settings.difficulty = diffsel.value as Settings['difficulty']));
  const qualitysel = document.getElementById('qualitysel') as HTMLSelectElement;
  qualitysel.addEventListener('change', () => {
    settings.quality = qualitysel.value as Settings['quality'];
    fx.quality = settings.quality;
  });
}

applyMenuSettings();
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

  if (gameState === 'playing' && selfId >= 0 && input.locked && selfSnap?.a !== false) {
    acc = Math.min(acc + dt, 0.2);
    let steps = 0;
    while (acc >= TICK_DT && steps < 5) {
      const inp = input.buildInput(++seq);
      if (inp.slot > 0) {
        const want = WEAPON_SLOTS[inp.slot - 1];
        if (want !== vm.activeWeapon) {
          desiredWeapon = want;
          vm.setWeapon(want);
          localSwitchBusyUntil = nowMs2 + WEAPONS[want].switchTime * 1000;
          localFireCd = Math.max(localFireCd, WEAPONS[want].switchTime);
          audio.playLocal('switch');
        }
      }
      predictor.step(inp);
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
        const base = viewDir(input.yaw + recoil.yaw * DEG2RAD, input.pitch + recoil.pitch * DEG2RAD);
        const dir = jitterDir(base, spread, Math.random);
        const dist = predictor.raycastObstacles(ox, oy, oz, dir.x, dir.y, dir.z) ?? SHOT_MAX_DISTANCE;
        const origin = new THREE.Vector3(ox, oy, oz);
        const end = new THREE.Vector3(ox + dir.x * dist, oy + dir.y * dist, oz + dir.z * dist);
        fx.tracer(origin, end);
        fx.impact(end, new THREE.Vector3(dir.x, dir.y, dir.z), impactKindAt(end.x, end.y, end.z));
        const fwd = new THREE.Vector3(dir.x, dir.y, dir.z);
        const rightV = new THREE.Vector3().crossVectors(fwd, new THREE.Vector3(0, 1, 0)).normalize();
        fx.shell(
          origin.clone().addScaledVector(fwd, 0.4).addScaledVector(rightV, 0.1).add(new THREE.Vector3(0, -0.08, 0)),
          fwd,
          rightV,
        );
        fx.muzzle(origin.clone().addScaledVector(fwd, 0.9).add(new THREE.Vector3(0, -0.05, 0)), fwd, 0.5);
        vm.kick();
        addRecoilShot(recoil, w, nowMs2 / 1000, Math.random);
        audio.playLocal(`${vm.activeWeapon}_shot` as 'ar_shot');
      }
      session!.send({ kind: 'input', input: inp });
      acc -= TICK_DT;
      steps++;
    }
    if (steps === 5) acc = 0;
  } else {
    acc = 0;
  }

  const s = predictor.state;
  const alpha = Math.max(0, Math.min(1, acc / TICK_DT));
  updateRecoil(recoil, now, dt);
  shake = Math.max(0, shake - shake * 6 * dt);
  const shakeX = (Math.random() - 0.5) * shake * 0.25;
  const shakeY = (Math.random() - 0.5) * shake * 0.25;

  if (killcam && nowMs2 < killcam.until) {
    const pose = remotes.getPose(killcam.id);
    if (pose) {
      ctx.camera.position.set(pose.x + shakeX, pose.y + pose.h * 0.9 + shakeY, pose.z);
      ctx.camera.rotation.y = pose.yaw;
      ctx.camera.rotation.x = pose.pitch;
      ctx.camera.rotation.z = 0;
    }
  } else {
    killcam = null;
    const ex = THREE.MathUtils.lerp(predictor.prevX, s.x, alpha);
    const ey = THREE.MathUtils.lerp(predictor.prevY, s.y, alpha);
    const ez = THREE.MathUtils.lerp(predictor.prevZ, s.z, alpha);
    const eh = THREE.MathUtils.lerp(predictor.prevH, s.height, alpha) * PLAYER_EYE_RATIO;
    ctx.camera.position.set(ex + shakeX, ey + eh + shakeY, ez);
    ctx.camera.rotation.y = input.yaw + recoil.yaw * DEG2RAD;
    ctx.camera.rotation.x = input.pitch + recoil.pitch * DEG2RAD;
    camRoll += ((s.sliding ? -5 * DEG2RAD : 0) - camRoll) * Math.min(1, 10 * dt);
    ctx.camera.rotation.z = camRoll;
  }

  const w = WEAPONS[vm.activeWeapon];
  const targetFov = input.ads ? w.adsFov : settings.fov + (s.sprinting ? 10 : 0);
  fov += (targetFov - fov) * Math.min(1, 12 * dt);
  ctx.camera.fov = fov;
  ctx.camera.updateProjectionMatrix();
  const scoped = vm.activeWeapon === 'sr' && input.ads && fov < 34;
  hud.setScope(scoped);
  const speed = Math.hypot(s.vx, s.vz);
  vm.update(dt, speed, s.onGround, input.ads, s.sprinting, 0, 0);
  vm.group.visible = !scoped && selfSnap?.a !== false;

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
  hud.update(selfSnap, sessionTimeLeft, nowMs2, session?.rtt ?? null);
  hud.setScoreboard(input.scoreboard && gameState === 'playing', lastPlayers, selfId);
  if (gameState === 'playing') minimap.render({ x: s.x, z: s.z, yaw: input.yaw }, blips, nowMs2);

  remotes.render(now, dt);
  fx.update(dt);
  ctx.renderer.render(ctx.scene, ctx.camera);
}

requestAnimationFrame(frame);
