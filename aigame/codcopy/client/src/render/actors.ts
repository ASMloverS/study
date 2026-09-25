import * as THREE from 'three';
import type { PlayerSnap, WeaponId } from 'shared';
import { MAPS, mapToObstacles, raycastBoxes, type AABB } from 'shared';
import type { EnemyColor } from '../input';

/** 远端角色（M7.3）：真实比例分节骨架 + 程序化步态 + 双臂 IK 持枪 + 类 ragdoll 死亡 + 敌色名牌。 */

const PALETTE = [0xc94f3f, 0x3f7fc9, 0x8fca4f, 0xc9a23a, 0x9a5fc9, 0x3fc9b0, 0xd06f9e, 0x8a8f3f];
const INTERP_DELAY = 0.066;
const CORPSE_FADE = 3.0;
const NAME_RANGE = 60;

/** [M9] 敌我识别：敌色（描边 / emissive / 名牌 / 小地图统一取色） */
export const ENEMY_COLORS: Record<EnemyColor, number> = {
  red: 0xff3b30,
  orange: 0xff8c1a,
  yellow: 0xffd60a,
  purple: 0xa545ff,
  cyan: 0x2ad4ee,
};

export const ENEMY_COLOR_RGB: Record<EnemyColor, string> = {
  red: '255,59,48',
  orange: '255,140,26',
  yellow: '255,214,10',
  purple: '165,69,255',
  cyan: '42,212,238',
};

let enemyOutlineOn = true;
let enemyShellsOn = true;
let enemyColorHex = ENEMY_COLORS.red;

const OBSTACLES: AABB[] = mapToObstacles(MAPS.warehouse);

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
  ps: number;
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

const GEO = {
  pelvis: new THREE.BoxGeometry(0.34, 0.2, 0.24),
  abdomen: new THREE.BoxGeometry(0.36, 0.2, 0.24),
  chest: new THREE.BoxGeometry(0.42, 0.3, 0.27),
  vest: new THREE.BoxGeometry(0.45, 0.34, 0.31),
  pouch: new THREE.BoxGeometry(0.1, 0.09, 0.06),
  pad: new THREE.BoxGeometry(0.14, 0.12, 0.18),
  neck: new THREE.BoxGeometry(0.09, 0.08, 0.09),
  head: new THREE.BoxGeometry(0.2, 0.24, 0.22),
  helmet: new THREE.BoxGeometry(0.25, 0.13, 0.26),
  brim: new THREE.BoxGeometry(0.26, 0.05, 0.27),
  band: new THREE.BoxGeometry(0.262, 0.05, 0.272),
  goggles: new THREE.BoxGeometry(0.21, 0.05, 0.04),
  upperArm: new THREE.BoxGeometry(0.09, 0.3, 0.1),
  forearm: new THREE.BoxGeometry(0.08, 0.26, 0.09),
  hand: new THREE.BoxGeometry(0.075, 0.08, 0.1),
  thigh: new THREE.BoxGeometry(0.14, 0.42, 0.17),
  shin: new THREE.BoxGeometry(0.115, 0.4, 0.13),
  boot: new THREE.BoxGeometry(0.125, 0.1, 0.26),
  gunBody: new THREE.BoxGeometry(0.05, 0.08, 0.5),
  gunMag: new THREE.BoxGeometry(0.04, 0.13, 0.06),
  gunStock: new THREE.BoxGeometry(0.045, 0.07, 0.2),
  gunScope: new THREE.BoxGeometry(0.04, 0.05, 0.18),
  gunPump: new THREE.BoxGeometry(0.05, 0.05, 0.16),
};

function gunFor(w: WeaponId, mat: THREE.Material): THREE.Group {
  const g = new THREE.Group();
  const add = (geo: THREE.BufferGeometry, x: number, y: number, z: number) => {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x, y, z);
    m.castShadow = true;
    g.add(m);
  };
  if (w === 'ar') {
    add(GEO.gunBody, 0, 0, -0.06);
    add(GEO.gunMag, 0, -0.09, 0.02);
    add(GEO.gunStock, 0, -0.01, 0.26);
    add(GEO.gunScope, 0, 0.07, 0.04);
  } else if (w === 'sg') {
    add(GEO.gunBody, 0, 0.01, -0.08);
    add(GEO.gunPump, 0, -0.05, -0.14);
    add(GEO.gunStock, 0, -0.02, 0.24);
  } else if (w === 'sr') {
    add(GEO.gunBody, 0, 0.01, -0.16);
    add(GEO.gunMag, 0, -0.08, -0.02);
    add(GEO.gunStock, 0, -0.01, 0.3);
    add(GEO.gunScope, 0, 0.09, -0.04);
  } else if (w === 'smg') {
    add(GEO.gunBody, 0, 0, -0.02);
    add(GEO.gunMag, 0, -0.1, 0.04);
    add(GEO.gunStock, 0, 0, 0.2);
    add(GEO.gunScope, 0, 0.06, 0);
  } else if (w === 'lmg') {
    add(GEO.gunBody, 0, 0, -0.1);
    add(GEO.gunMag, 0, -0.1, 0);
    add(GEO.gunStock, 0, -0.01, 0.28);
    add(GEO.gunScope, 0, 0.07, 0.06);
  } else if (w === 'dmr') {
    add(GEO.gunBody, 0, 0.01, -0.12);
    add(GEO.gunMag, 0, -0.07, 0);
    add(GEO.gunStock, 0, -0.01, 0.28);
    add(GEO.gunScope, 0, 0.08, -0.02);
  } else {
    add(GEO.gunBody, 0, 0.01, 0.1);
    add(GEO.gunMag, 0, -0.07, 0.12);
  }
  return g;
}

function nameSprite(name: string): THREE.Sprite {
  const c = document.createElement('canvas');
  c.width = 320;
  c.height = 80;
  const ctx = c.getContext('2d')!;
  ctx.font = 'bold 44px sans-serif';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.lineWidth = 8;
  ctx.strokeStyle = 'rgba(0,0,0,0.85)';
  ctx.strokeText(name, 160, 40);
  ctx.fillStyle = '#ffffff';
  ctx.fillText(name, 160, 40);
  const tex = new THREE.CanvasTexture(c);
  tex.colorSpace = THREE.SRGBColorSpace;
  const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, depthTest: true, transparent: true }));
  sprite.material.color.setHex(enemyColorHex);
  sprite.scale.set(1.75, 0.44, 1);
  return sprite;
}

interface ArmRig {
  root: THREE.Group;
  fore: THREE.Group;
  hand: THREE.Group;
}

class RemoteView {
  readonly group = new THREE.Group();
  private buffer: SnapEntry[] = [];
  private deathAt: number | null = null;
  private fallYaw = 0;
  pose: ActorPose = { x: 0, y: 0, z: 0, yaw: 0, pitch: 0, h: 1.8, speed: 0 };
  private walkT = 0;
  private readonly mats: THREE.MeshStandardMaterial[];
  private readonly bandMat: THREE.MeshStandardMaterial;
  private readonly hips = new THREE.Group();
  private readonly torso = new THREE.Group();
  private readonly aim = new THREE.Group();
  private readonly neck = new THREE.Group();
  private readonly thighL = new THREE.Group();
  private readonly thighR = new THREE.Group();
  private readonly shinL = new THREE.Group();
  private readonly shinR = new THREE.Group();
  private readonly armL: ArmRig;
  private readonly armR: ArmRig;
  private readonly guns: Record<WeaponId, THREE.Group>;
  private readonly nameTag: THREE.Sprite;
  private readonly shellMat = new THREE.MeshBasicMaterial({
    color: ENEMY_COLORS.red,
    side: THREE.BackSide,
    toneMapped: false,
  });
  private readonly shells: THREE.Mesh[] = [];
  private readonly bodyMats: THREE.MeshStandardMaterial[];
  private nameOccludeAt = 0;
  private nameVisible = true;

  constructor(color: number, name: string) {
    const uniform = new THREE.MeshStandardMaterial({ color: 0x59604c, roughness: 0.92, metalness: 0.04 });
    const gear = new THREE.MeshStandardMaterial({ color: 0x33362e, roughness: 0.85, metalness: 0.12 });
    const skin = new THREE.MeshStandardMaterial({ color: 0xc9a184, roughness: 0.88, metalness: 0 });
    const gun = new THREE.MeshStandardMaterial({ color: 0x24272c, roughness: 0.45, metalness: 0.75 });
    this.bandMat = new THREE.MeshStandardMaterial({ color, roughness: 0.6, metalness: 0.2 });
    this.mats = [uniform, gear, skin, gun, this.bandMat];
    this.bodyMats = [uniform, gear, skin];

    const M = (geo: THREE.BufferGeometry, mat: THREE.Material, x = 0, y = 0, z = 0): THREE.Mesh => {
      const m = new THREE.Mesh(geo, mat);
      m.position.set(x, y, z);
      m.castShadow = true;
      m.receiveShadow = true;
      return m;
    };

    this.hips.position.y = 0.96;
    this.group.add(this.hips);
    this.hips.add(M(GEO.pelvis, uniform));

    this.torso.position.y = 0.1;
    this.hips.add(this.torso);
    this.torso.add(M(GEO.abdomen, uniform, 0, 0.1, 0));
    this.torso.add(M(GEO.chest, uniform, 0, 0.32, 0));
    this.torso.add(M(GEO.vest, gear, 0, 0.32, 0.01));
    this.torso.add(M(GEO.pouch, gear, 0.11, 0.2, 0.15));
    this.torso.add(M(GEO.pouch, gear, -0.11, 0.2, 0.15));
    this.torso.add(M(GEO.pouch, gear, 0, 0.2, -0.15));
    this.torso.add(M(GEO.band, this.bandMat, 0, 0.46, 0));
    this.torso.add(M(GEO.pad, gear, 0.26, 0.42, 0));
    this.torso.add(M(GEO.pad, gear, -0.26, 0.42, 0));

    this.neck.position.y = 0.52;
    this.torso.add(this.neck);
    this.neck.add(M(GEO.neck, skin, 0, 0.04, 0));
    this.neck.add(M(GEO.head, skin, 0, 0.2, 0.01));
    this.neck.add(M(GEO.helmet, gear, 0, 0.34, 0));
    this.neck.add(M(GEO.brim, this.bandMat, 0, 0.28, 0));
    this.neck.add(M(GEO.goggles, gear, 0, 0.22, 0.12));

    this.aim.position.set(0, 0.4, 0.02);
    this.torso.add(this.aim);
    this.armR = this.buildArm(M, uniform, gear, -1);
    this.armL = this.buildArm(M, uniform, gear, 1);

    this.guns = {
      ar: gunFor('ar', gun),
      sg: gunFor('sg', gun),
      sr: gunFor('sr', gun),
      smg: gunFor('smg', gun),
      lmg: gunFor('lmg', gun),
      dmr: gunFor('dmr', gun),
      pistol: gunFor('pistol', gun),
    };
    for (const g of Object.values(this.guns)) {
      g.position.set(-0.09, -0.02, -0.26);
      this.aim.add(g);
    }
    for (const k of Object.keys(this.guns)) {
      if (k !== 'ar') this.guns[k as WeaponId].visible = false;
    }

    this.thighL.position.set(0.1, -0.08, 0);
    this.hips.add(this.thighL);
    this.thighL.add(M(GEO.thigh, uniform, 0, -0.21, 0));
    this.shinL.position.y = -0.46;
    this.thighL.add(this.shinL);
    this.shinL.add(M(GEO.shin, uniform, 0, -0.2, 0));
    this.shinL.add(M(GEO.boot, gear, 0, -0.44, -0.04));

    this.thighR.position.set(-0.1, -0.08, 0);
    this.hips.add(this.thighR);
    this.thighR.add(M(GEO.thigh, uniform, 0, -0.21, 0));
    this.shinR.position.y = -0.46;
    this.thighR.add(this.shinR);
    this.shinR.add(M(GEO.shin, uniform, 0, -0.2, 0));
    this.shinR.add(M(GEO.boot, gear, 0, -0.44, -0.04));

    this.nameTag = nameSprite(name);
    this.nameTag.position.y = 2.05;
    this.group.add(this.nameTag);
    this.buildShells();
    this.applyEnemyVisuals();
  }

  /** [M9] shell 法描边：每个身体网格挂一个 BackSide 外扩壳（自带遮挡，随姿态动画更新） */
  private buildShells(): void {
    const meshes: THREE.Mesh[] = [];
    this.group.traverse((o) => {
      if ((o as THREE.Mesh).isMesh) meshes.push(o as THREE.Mesh);
    });
    for (const mesh of meshes) {
      const shell = new THREE.Mesh(mesh.geometry, this.shellMat);
      shell.scale.setScalar(1.08);
      mesh.add(shell);
      this.shells.push(shell);
    }
  }

  applyEnemyVisuals(): void {
    for (const s of this.shells) s.visible = enemyOutlineOn && enemyShellsOn;
    this.shellMat.color.setHex(enemyColorHex);
    for (const m of this.bodyMats) {
      if (enemyOutlineOn) {
        m.emissive.setHex(enemyColorHex);
        m.emissiveIntensity = 0.15;
      } else {
        m.emissive.setHex(0x000000);
        m.emissiveIntensity = 0;
      }
    }
    this.nameTag.material.color.setHex(enemyColorHex);
  }

  private buildArm(M: (geo: THREE.BufferGeometry, mat: THREE.Material, x?: number, y?: number, z?: number) => THREE.Mesh, uniform: THREE.Material, gear: THREE.Material, side: 1 | -1): ArmRig {
    const root = new THREE.Group();
    root.position.set(0.26 * side, 0.4, 0);
    this.torso.add(root);
    root.add(M(GEO.upperArm, uniform, 0, -0.15, 0));
    const fore = new THREE.Group();
    fore.position.y = -0.3;
    root.add(fore);
    fore.add(M(GEO.forearm, uniform, 0, -0.13, 0));
    const hand = new THREE.Group();
    hand.position.y = -0.27;
    fore.add(hand);
    hand.add(M(GEO.hand, gear, 0, -0.03, 0.02));
    return { root, fore, hand };
  }

  kill(fromDir: THREE.Vector3): void {
    const d = fromDir.clone().setY(0);
    if (d.lengthSq() > 1e-6) this.fallYaw = Math.atan2(-d.x, -d.z);
    this.deathAt = performance.now() / 1000;
    for (const m of [...this.mats, this.shellMat]) {
      m.transparent = true;
      m.needsUpdate = true;
    }
  }

  push(s: PlayerSnap, time: number): void {
    if (!s.a && this.buffer.length > 0 && this.buffer[this.buffer.length - 1].alive) {
      if (this.deathAt === null) this.kill(new THREE.Vector3(0, 0, 1));
    }
    if (s.a) {
      this.deathAt = null;
      for (const m of [...this.mats, this.shellMat]) {
        m.opacity = 1;
        m.transparent = false;
        m.needsUpdate = true;
      }
    }
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
      ps: s.ps ?? 0,
    });
    if (this.buffer.length > 10) this.buffer.shift();
  }

  render(now: number, dt: number, viewer: { x: number; y: number; z: number }): void {
    const target = now - INTERP_DELAY;
    while (this.buffer.length > 2 && this.buffer[1].time < target) this.buffer.shift();
    const b = this.buffer;
    if (b.length === 0) return;
    const latest = b[b.length - 1];
    if (this.deathAt !== null) {
      const age = now - this.deathAt;
      if (age > CORPSE_FADE + 0.6) {
        this.group.visible = false;
        return;
      }
      this.group.visible = true;
      this.nameTag.visible = false;
      const fall = Math.min(1, age / 0.45);
      const ease = 1 - (1 - fall) * (1 - fall);
      this.group.position.set(this.pose.x, this.pose.y, this.pose.z);
      this.group.quaternion
        .setFromAxisAngle(new THREE.Vector3(0, 1, 0), this.fallYaw)
        .multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), -ease * Math.PI / 2));
      const loosen = Math.min(1, age / 0.3);
      this.hips.position.y = 0.96 - ease * 0.55;
      this.torso.rotation.set(-0.3 * loosen, 0.2 * loosen, 0.35 * loosen);
      this.thighL.rotation.set(0.5 * loosen, 0, 0.25 * loosen);
      this.thighR.rotation.set(0.2 * loosen, 0, -0.3 * loosen);
      this.shinL.rotation.x = -0.7 * loosen;
      this.shinR.rotation.x = -0.2 * loosen;
      this.armL.root.rotation.set(1.3 * loosen, 0, 0.6 * loosen);
      this.armR.root.rotation.set(0.9 * loosen, 0, -0.8 * loosen);
      this.armL.fore.rotation.x = -0.5 * loosen;
      this.armR.fore.rotation.x = -0.9 * loosen;
      this.aim.rotation.x = 0.4 * loosen;
      const fade = age < CORPSE_FADE ? 1 : Math.max(0, 1 - (age - CORPSE_FADE) / 0.6);
      for (const m of this.mats) m.opacity = fade;
      this.shellMat.opacity = fade;
      return;
    }
    for (const m of this.mats) m.opacity = 1;
    this.group.rotation.set(0, 0, 0);
    this.group.quaternion.setFromAxisAngle(new THREE.Vector3(0, 1, 0), this.poseYaw());
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

    const pose = e.ps !== 0 ? e.ps : e.sl ? 2 : e.h < 1.5 ? 1 : 0;
    if (e.speed > 0.4) this.walkT += dt * (3 + e.speed * 1.5);
    const swing = Math.min(1, e.speed / 4.5) * 0.62;
    const cyc = Math.sin(this.walkT);

    if (pose === 2) {
      this.hips.position.y = 0.42;
      this.torso.rotation.set(1.0, 0, 0.15);
      this.thighL.rotation.set(-1.15, 0, 0.12);
      this.thighR.rotation.set(-1.45, 0, -0.1);
      this.shinL.rotation.x = 0.35;
      this.shinR.rotation.x = 0.6;
      this.aim.rotation.x = e.pitch - 0.6;
    } else if (pose === 3) {
      this.hips.position.y = 0.96;
      this.torso.rotation.set(0.22, 0, 0);
      this.thighL.rotation.set(-0.75, 0, 0.08);
      this.thighR.rotation.set(0.35, 0, -0.08);
      this.shinL.rotation.x = 1.0;
      this.shinR.rotation.x = 0.3;
      this.aim.rotation.x = e.pitch - 0.25;
    } else {
      const crouchAmt = Math.max(0, Math.min(1, (1.8 - e.h) / 0.6));
      this.hips.position.y = 0.96 - 0.34 * crouchAmt + Math.abs(cyc) * swing * 0.05;
      this.torso.rotation.set(0.1 * crouchAmt + Math.min(0.14, e.speed * 0.02), cyc * swing * 0.06, 0);
      this.thighL.rotation.x = cyc * swing + 0.95 * crouchAmt;
      this.thighR.rotation.x = -cyc * swing + 0.95 * crouchAmt;
      this.shinL.rotation.x = Math.max(0, -cyc) * swing * 1.1 - 1.15 * crouchAmt;
      this.shinR.rotation.x = Math.max(0, cyc) * swing * 1.1 - 1.15 * crouchAmt;
      this.aim.rotation.x = e.pitch;
    }

    for (const [id, g] of Object.entries(this.guns)) {
      g.visible = id === e.w;
    }
    this.solveArms(e);

    const dx = viewer.x - e.x;
    const dz = viewer.z - e.z;
    const dist = Math.hypot(dx, dz);
    const nowMs = performance.now();
    if (dist > NAME_RANGE) {
      this.nameTag.visible = false;
    } else {
      if (nowMs > this.nameOccludeAt) {
        this.nameOccludeAt = nowMs + 200;
        const eyeY = e.y + e.h + 0.2;
        const d3 = Math.hypot(dx, eyeY - viewer.y, dz) || 1;
        const hit = raycastBoxes(e.x, eyeY, e.z, dx / d3, (eyeY - viewer.y) / d3, dz / d3, d3, OBSTACLES);
        this.nameVisible = !hit;
      }
      this.nameTag.visible = this.nameVisible;
      const k = Math.max(0.7, Math.min(1.6, dist / 18));
      this.nameTag.scale.set(1.75 * k, 0.44 * k, 1);
      this.nameTag.position.y = e.h + 0.3;
    }
  }

  private poseYaw(): number {
    const b = this.buffer;
    return b.length > 0 ? b[b.length - 1].yaw : 0;
  }

  /** 双臂 2 骨 IK：手部朝向枪身握把 / 护木目标点 */
  private solveArms(e: SnapEntry): void {
    const gripR = new THREE.Vector3(-0.09, -0.04, 0.12);
    const gripL = new THREE.Vector3(-0.09, -0.02, -0.2);
    for (const [arm, side, target] of [
      [this.armR, -1, gripR],
      [this.armL, 1, gripL],
    ] as const) {
      const shoulder = new THREE.Vector3(0.26 * side, 0.4, 0.02);
      const to = target.clone().sub(shoulder);
      const reach = to.length();
      const l1 = 0.3;
      const l2 = 0.32;
      const d = Math.max(0.18, Math.min(l1 + l2 - 0.02, reach));
      const cosElbow = Math.min(1, Math.max(-1, (l1 * l1 + l2 * l2 - d * d) / (2 * l1 * l2)));
      const elbow = Math.PI - Math.acos(cosElbow);
      const cosShoulder = Math.min(1, Math.max(-1, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)));
      const shoulderAng = Math.acos(cosShoulder);
      const pitch = Math.atan2(to.y, Math.hypot(to.x, to.z));
      const yaw = Math.atan2(-to.x, -to.z);
      arm.root.rotation.set(pitch - shoulderAng + Math.PI / 2, yaw, side * 0.35);
      arm.fore.rotation.x = -elbow;
      arm.hand.rotation.x = -pitch;
    }
    void e;
  }
}

export class RemoteViews {
  private views = new Map<number, RemoteView>();

  constructor(private scene: THREE.Scene) {}

  /** [M9] 敌我识别设置：更新全局敌色/描边并应用到现有角色（shells=false 为低画质降级：仅 emissive） */
  setEnemyVisuals(outline: boolean, color: EnemyColor, shells = true): void {
    enemyOutlineOn = outline;
    enemyShellsOn = shells;
    enemyColorHex = ENEMY_COLORS[color];
    for (const v of this.views.values()) v.applyEnemyVisuals();
  }

  pushAll(players: PlayerSnap[], selfId: number, time: number): void {
    for (const p of players) {
      if (p.id === selfId) continue;
      let v = this.views.get(p.id);
      if (!v) {
        v = new RemoteView(PALETTE[p.id % PALETTE.length], p.name);
        this.views.set(p.id, v);
        this.scene.add(v.group);
      }
      v.push(p, time);
    }
  }

  render(now: number, dt: number, viewer?: { x: number; y: number; z: number }): void {
    for (const v of this.views.values()) v.render(now, dt, viewer ?? { x: 0, y: 0, z: 0 });
  }

  kill(id: number, fromDir: THREE.Vector3): void {
    this.views.get(id)?.kill(fromDir);
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

  setVisible(v: boolean): void {
    for (const view of this.views.values()) view.group.visible = v;
  }

  clear(): void {
    for (const v of this.views.values()) this.scene.remove(v.group);
    this.views.clear();
  }
}
