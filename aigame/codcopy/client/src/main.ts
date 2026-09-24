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
import { Killcam } from './render/killcam';
import { AudioSys } from './audio';
import { LocalSession, NetSession, type Session } from './transport';

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
const killcamRec = new Killcam();
let kcReplay: { killerId: number; deathAt: number; startAt: number } | null = null;
let killcamViews: RemoteViews | null = null;

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
  desiredWeapon = 'ar';
  vm.setWeapon('ar');
  fov = settings.fov;
  breath = 1;
  breathPhase = 0;
  swayYaw = 0;
  swayPitch = 0;
  announcedStart = false;
  endKillcamReplay();
  gameState = 'playing';
  overlay.style.display = 'none';
  const mode = (document.getElementById('modesel') as HTMLSelectElement).value;
  if (mode === 'net') {
    const raw = (document.getElementById('serverinput') as HTMLInputElement).value.trim();
    const url = raw || `ws://${location.host}`;
    const name = (document.getElementById('nameinput') as HTMLInputElement).value.trim() || '玩家';
    session = new NetSession(url, name);
  } else {
    session = new LocalSession({
      bots: settings.bots,
      botDifficulty: settings.difficulty,
      killLimit: settings.killLimit,
      durationSec: settings.matchMinutes * 60,
    });
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
    if (raw.cfg) matchKillLimit = raw.cfg.killLimit;
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
    if (dist < 10) shake = Math.max(shake, 0.45 * (1 - dist / 10));
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
  bindRange('killrange', 'killval', (v) => `${v} 杀`, (v) => (settings.killLimit = v));
  bindRange('durrange', 'durval', (v) => `${v} 分钟`, (v) => (settings.matchMinutes = v));
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
    ctx.setQuality(settings.quality);
  });
  qualitysel.dispatchEvent(new Event('change'));
  const ttscheck = document.getElementById('ttscheck') as HTMLInputElement;
  ttscheck.addEventListener('change', () => {
    audio.setTts(ttscheck.checked);
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
      const inp = input.buildInput(++seq, swayYaw, swayPitch);
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
  fov += (targetFov - fov) * Math.min(1, 12 * dt);
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
  hud.setScoreboard(input.scoreboard && gameState === 'playing', lastPlayers, selfId);
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
