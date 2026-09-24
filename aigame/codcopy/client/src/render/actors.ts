import * as THREE from 'three';
import type { PlayerSnap, WeaponId } from 'shared';

const PALETTE = [0xc94f3f, 0x3f7fc9, 0x8fca4f, 0xc9a23a, 0x9a5fc9, 0x3fc9b0, 0xd06f9e, 0x8a8f3f];
const INTERP_DELAY = 0.066;

interface SnapEntry {
  time: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  h: number;
  sl: boolean;
  alive: boolean;
  speed: number;
  w: WeaponId;
}

export interface ActorPose {
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  h: number;
  speed: number;
}

interface BodyMats {
  uniform: THREE.MeshStandardMaterial;
  vest: THREE.MeshStandardMaterial;
  skin: THREE.MeshStandardMaterial;
  helmet: THREE.MeshStandardMaterial;
  boots: THREE.MeshStandardMaterial;
  gun: THREE.MeshStandardMaterial;
  band: THREE.MeshStandardMaterial;
}

function buildHeldGun(w: WeaponId, mats: BodyMats): THREE.Group {
  const g = new THREE.Group();
  const P = (ww: number, h: number, d: number, x: number, y: number, z: number) => {
    const m = new THREE.Mesh(new THREE.BoxGeometry(ww, h, d), mats.gun);
    m.position.set(x, y, z);
    m.castShadow = true;
    g.add(m);
  };
  if (w === 'ar') {
    P(0.06, 0.09, 0.62, 0, 0, -0.1);
    P(0.04, 0.12, 0.06, 0, -0.09, 0.05);
    P(0.05, 0.07, 0.16, 0, -0.01, 0.22);
  } else if (w === 'sg') {
    P(0.07, 0.09, 0.5, 0, 0, -0.1);
    P(0.05, 0.06, 0.2, 0, -0.05, -0.18);
    P(0.06, 0.09, 0.24, 0, -0.02, 0.18);
  } else {
    P(0.06, 0.08, 0.9, 0, 0, -0.15);
    P(0.04, 0.05, 0.26, 0, 0.08, -0.05);
    P(0.05, 0.08, 0.24, 0, -0.01, 0.22);
  }
  return g;
}

class RemoteView {
  readonly group = new THREE.Group();
  private buffer: SnapEntry[] = [];
  private deathAt: number | null = null;
  pose: ActorPose = { x: 0, y: 0, z: 0, yaw: 0, pitch: 0, h: 1.8, speed: 0 };
  private walkT = 0;
  private readonly hips = new THREE.Group();
  private readonly torso = new THREE.Group();
  private readonly aim = new THREE.Group();
  private readonly thighL = new THREE.Group();
  private readonly thighR = new THREE.Group();
  private readonly shinL = new THREE.Group();
  private readonly shinR = new THREE.Group();
  private readonly armL = new THREE.Group();
  private readonly armR = new THREE.Group();
  private readonly guns: Record<WeaponId, THREE.Group>;
  private readonly mats: BodyMats;

  constructor(color: number) {
    this.mats = {
      uniform: new THREE.MeshStandardMaterial({ color: 0x5a5f4a, roughness: 0.9, metalness: 0.05, transparent: true }),
      vest: new THREE.MeshStandardMaterial({ color: 0x3a3d33, roughness: 0.85, metalness: 0.1, transparent: true }),
      skin: new THREE.MeshStandardMaterial({ color: 0xc9a184, roughness: 0.9, metalness: 0, transparent: true }),
      helmet: new THREE.MeshStandardMaterial({ color: 0x4a4f42, roughness: 0.6, metalness: 0.3, transparent: true }),
      boots: new THREE.MeshStandardMaterial({ color: 0x26241f, roughness: 0.9, metalness: 0.05, transparent: true }),
      gun: new THREE.MeshStandardMaterial({ color: 0x25282e, roughness: 0.5, metalness: 0.7, transparent: true }),
      band: new THREE.MeshStandardMaterial({ color, roughness: 0.6, metalness: 0.2, transparent: true }),
    };
    const M = this.mats;
    const P = (w: number, h: number, d: number, mat: THREE.Material, x: number, y: number, z: number) => {
      const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
      m.position.set(x, y, z);
      m.castShadow = true;
      return m;
    };

    this.hips.position.y = 0.92;
    this.group.add(this.hips);
    this.hips.add(P(0.34, 0.18, 0.24, M.uniform, 0, 0, 0));

    this.torso.position.y = 0.08;
    this.hips.add(this.torso);
    this.torso.add(P(0.4, 0.5, 0.26, M.uniform, 0, 0.25, 0));
    this.torso.add(P(0.42, 0.3, 0.3, M.vest, 0, 0.28, 0));
    this.torso.add(P(0.44, 0.06, 0.28, M.band, 0, 0.44, 0));
    this.torso.add(P(0.14, 0.1, 0.1, M.vest, 0, 0.22, -0.18));

    const headG = new THREE.Group();
    headG.position.y = 0.56;
    this.torso.add(headG);
    headG.add(P(0.22, 0.24, 0.23, M.skin, 0, 0.12, 0.01));
    headG.add(P(0.27, 0.13, 0.28, M.helmet, 0, 0.235, 0));
    headG.add(P(0.285, 0.05, 0.29, M.band, 0, 0.185, 0));

    this.aim.position.y = 0.42;
    this.torso.add(this.aim);
    this.armR.position.set(-0.24, 0.02, 0);
    this.aim.add(this.armR);
    this.armR.add(P(0.1, 0.1, 0.34, M.uniform, 0, 0, -0.16));
    this.armL.position.set(0.22, 0.02, -0.05);
    this.aim.add(this.armL);
    this.armL.add(P(0.1, 0.1, 0.3, M.uniform, 0, 0, -0.14));
    this.guns = { ar: buildHeldGun('ar', M), sg: buildHeldGun('sg', M), sr: buildHeldGun('sr', M) };
    for (const g of Object.values(this.guns)) {
      g.position.set(-0.05, 0.02, -0.3);
      this.aim.add(g);
    }
    this.guns.sg.visible = false;
    this.guns.sr.visible = false;

    this.thighL.position.set(0.11, -0.06, 0);
    this.hips.add(this.thighL);
    this.thighL.add(P(0.15, 0.42, 0.18, M.uniform, 0, -0.21, 0));
    this.shinL.position.y = -0.44;
    this.thighL.add(this.shinL);
    this.shinL.add(P(0.12, 0.4, 0.14, M.uniform, 0, -0.2, 0));
    this.shinL.add(P(0.13, 0.09, 0.24, M.boots, 0, -0.42, -0.03));

    this.thighR.position.set(-0.11, -0.06, 0);
    this.hips.add(this.thighR);
    this.thighR.add(P(0.15, 0.42, 0.18, M.uniform, 0, -0.21, 0));
    this.shinR.position.y = -0.44;
    this.thighR.add(this.shinR);
    this.shinR.add(P(0.12, 0.4, 0.14, M.uniform, 0, -0.2, 0));
    this.shinR.add(P(0.13, 0.09, 0.24, M.boots, 0, -0.42, -0.03));
  }

  push(s: PlayerSnap, time: number): void {
    if (!s.a && this.buffer.length > 0 && this.buffer[this.buffer.length - 1].alive) {
      this.deathAt = time;
    }
    if (s.a) this.deathAt = null;
    this.buffer.push({
      time,
      x: s.x,
      y: s.y,
      z: s.z,
      yaw: s.yaw,
      pitch: s.pitch,
      h: s.h,
      sl: s.sl,
      alive: s.a,
      speed: Math.hypot(s.vx, s.vz),
      w: s.w,
    });
    if (this.buffer.length > 10) this.buffer.shift();
  }

  render(now: number, dt: number): void {
    const target = now - INTERP_DELAY;
    while (this.buffer.length > 2 && this.buffer[1].time < target) this.buffer.shift();
    const b = this.buffer;
    if (b.length === 0) return;
    const latest = b[b.length - 1];
    if (this.deathAt !== null) {
      const age = now - this.deathAt;
      if (age > 0.85) {
        this.group.visible = false;
        return;
      }
      this.group.visible = true;
      const fall = Math.min(1, age / 0.35);
      this.group.rotation.x = (fall * Math.PI) / 2;
      const fade = age < 0.35 ? 1 : Math.max(0, 1 - (age - 0.35) / 0.5);
      for (const m of Object.values(this.mats)) m.opacity = fade;
      return;
    }
    for (const m of Object.values(this.mats)) m.opacity = 1;
    this.group.rotation.x = 0;
    this.group.visible = latest.alive;
    if (!latest.alive) return;
    let e: SnapEntry;
    if (b.length === 1 || b[0].time >= target) {
      e = b[0];
    } else {
      const s0 = b[0];
      const s1 = b[1];
      const a = Math.max(0, Math.min(1, (target - s0.time) / Math.max(1e-6, s1.time - s0.time)));
      let dyaw = s1.yaw - s0.yaw;
      if (dyaw > Math.PI) dyaw -= Math.PI * 2;
      if (dyaw < -Math.PI) dyaw += Math.PI * 2;
      e = {
        ...s1,
        x: s0.x + (s1.x - s0.x) * a,
        y: s0.y + (s1.y - s0.y) * a,
        z: s0.z + (s1.z - s0.z) * a,
        yaw: s0.yaw + dyaw * a,
        speed: s0.speed + (s1.speed - s0.speed) * a,
      };
    }
    this.pose = { x: e.x, y: e.y, z: e.z, yaw: e.yaw, pitch: e.pitch, h: e.h, speed: e.speed };
    this.group.position.set(e.x, e.y, e.z);
    this.group.rotation.y = e.yaw;

    const crouchAmt = Math.max(0, Math.min(1, (1.8 - e.h) / 0.6));
    if (e.speed > 0.4) this.walkT += dt * (3 + e.speed * 1.4);
    const swing = Math.min(1, e.speed / 4.5) * 0.6;

    if (e.sl) {
      this.hips.position.y = 0.5;
      this.torso.rotation.x = 0.95;
      this.thighL.rotation.x = -1.2;
      this.thighR.rotation.x = -1.35;
      this.shinL.rotation.x = 0.2;
      this.shinR.rotation.x = 0.45;
      this.aim.rotation.x = e.pitch - 0.7;
    } else {
      this.hips.position.y = 0.92 - 0.32 * crouchAmt;
      this.torso.rotation.x = 0.12 * crouchAmt + Math.min(0.12, e.speed * 0.015);
      const cyc = Math.sin(this.walkT);
      this.thighL.rotation.x = cyc * swing + 0.95 * crouchAmt;
      this.thighR.rotation.x = -cyc * swing + 0.95 * crouchAmt;
      this.shinL.rotation.x = Math.max(0, -cyc) * swing * 0.9 - 1.15 * crouchAmt;
      this.shinR.rotation.x = Math.max(0, cyc) * swing * 0.9 - 1.15 * crouchAmt;
      this.aim.rotation.x = e.pitch;
    }
    for (const [id, g] of Object.entries(this.guns)) {
      g.visible = id === e.w;
    }
  }
}

export class RemoteViews {
  private views = new Map<number, RemoteView>();

  constructor(private scene: THREE.Scene) {}

  pushAll(players: PlayerSnap[], selfId: number, time: number): void {
    for (const p of players) {
      if (p.id === selfId) continue;
      let v = this.views.get(p.id);
      if (!v) {
        v = new RemoteView(PALETTE[p.id % PALETTE.length]);
        this.views.set(p.id, v);
        this.scene.add(v.group);
      }
      v.push(p, time);
    }
  }

  render(now: number, dt: number): void {
    for (const v of this.views.values()) v.render(now, dt);
  }

  getPose(id: number): ActorPose | null {
    const v = this.views.get(id);
    return v && v.group.visible ? v.pose : null;
  }

  forEach(cb: (id: number, pose: ActorPose) => void): void {
    for (const [id, v] of this.views) {
      if (v.group.visible) cb(id, v.pose);
    }
  }

  clear(): void {
    for (const v of this.views.values()) this.scene.remove(v.group);
    this.views.clear();
  }
}
