import * as THREE from 'three';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';
import { WEAPONS, WEAPON_LIST, type WeaponId } from 'shared';
import { toonMat } from './palette';

/** 程序化高精度武器视图模型：无手臂、独立场景双 pass、逐枪 ADS 锚点对齐、换弹/泵动/栓动动画；[M13] 材质鲜艳化（几何/动画零变更）。 */

const M = {
  gunMetal: toonMat(0xeef2f6),
  darkMetal: toonMat(0xb8c2cd),
  boltSteel: toonMat(0xffffff),
  polymer: toonMat(0x35424e),
  wood: toonMat(0xe09a3c),
  accent: toonMat(0xff7a1a),
  shellRed: toonMat(0xff4030),
  brass: toonMat(0xffd60a),
  lens: new THREE.MeshBasicMaterial({ color: 0x9fd8ff }),
  sightDot: new THREE.MeshBasicMaterial({ color: 0xffd60a }),
};

function box(w: number, h: number, d: number, mat: THREE.Material, x = 0, y = 0, z = 0, round = 0.004): THREE.Mesh {
  const m = new THREE.Mesh(new RoundedBoxGeometry(w, h, d, 2, round), mat);
  m.position.set(x, y, z);
  return m;
}

function cyl(r0: number, r1: number, len: number, mat: THREE.Material, x = 0, y = 0, z = 0, seg = 16, axis: 'x' | 'y' | 'z' = 'z'): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(r0, r1, len, seg), mat);
  if (axis === 'z') m.rotation.x = Math.PI / 2;
  if (axis === 'x') m.rotation.z = Math.PI / 2;
  m.position.set(x, y, z);
  return m;
}

function torus(r: number, tube: number, mat: THREE.Material, x = 0, y = 0, z = 0, seg = 16): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.TorusGeometry(r, tube, 8, seg), mat);
  m.position.set(x, y, z);
  return m;
}

interface GunModel {
  root: THREE.Group;
  sightY: number;
  muzzle: THREE.Vector3;
  mag: THREE.Group | null;
  bolt: THREE.Group | null;
  pump: THREE.Group | null;
  shells: THREE.Mesh[];
  reloadStyle: 'mag' | 'shell';
  ejectZ: number;
}

export interface VmUpdateOpts {
  dt: number;
  speed: number;
  onGround: boolean;
  ads: boolean;
  sprinting: boolean;
  lookDX: number;
  lookDY: number;
  reloadT: number;
  reloadTotal: number;
  scoped: boolean;
}

export class ViewModel {
  readonly group = new THREE.Group();
  readonly scene = new THREE.Scene();
  readonly camera = new THREE.PerspectiveCamera(55, 1, 0.01, 5);
  private readonly models = new Map<WeaponId, GunModel>();
  private active: WeaponId = 'ar';
  private adsAmt = 0;
  private kickAmt = 0;
  private sprintAmt = 0;
  private switchT = 1;
  private switching = false;
  private prevWeapon: WeaponId = 'ar';
  private bobT = 0;
  private idleT = 0;
  private swayX = 0;
  private swayY = 0;
  private fireAnimT = 99;
  private reloadPhase = 0;

  constructor() {
    const hemi = new THREE.HemisphereLight(0xdff2ff, 0x8a95a0, 1.1);
    this.scene.add(hemi);
    const key = new THREE.DirectionalLight(0xfff6e2, 2.0);
    key.position.set(0.8, 1.4, 0.6);
    this.scene.add(key);
    const rim = new THREE.DirectionalLight(0xbfe0f2, 0.6);
    rim.position.set(-1, 0.4, -0.8);
    this.scene.add(rim);
    this.scene.add(this.group);
    for (const w of WEAPON_LIST) {
      const gun = BUILDERS[w]();
      gun.root.visible = false;
      this.group.add(gun.root);
      this.models.set(w, gun);
    }
    this.models.get('ar')!.root.visible = true;
  }

  get activeWeapon(): WeaponId {
    return this.active;
  }

  setWeapon(w: WeaponId): void {
    if (w === this.active) return;
    this.prevWeapon = this.active;
    this.active = w;
    this.switchT = 0;
    this.switching = true;
  }

  kick(): void {
    this.kickAmt = Math.min(1, this.kickAmt + 0.85);
    this.fireAnimT = 0;
  }

  update(o: VmUpdateOpts): void {
    const gun = this.models.get(this.active)!;
    this.idleT += o.dt;
    this.bobT += o.dt * Math.min(o.speed, 7) * 1.6;
    this.fireAnimT += o.dt;

    const w = WEAPONS[this.active];
    if (this.switching) {
      this.switchT = Math.min(1, this.switchT + o.dt / w.switchTime);
      if (this.switchT >= 1) this.switching = false;
    }
    const switchDown = this.switching ? (this.switchT < 0.4 ? this.switchT / 0.4 : 1 - (this.switchT - 0.4) / 0.6) : 0;
    if (this.switching && this.switchT >= 0.4 && this.models.get(this.prevWeapon)!.root.visible) {
      this.models.get(this.prevWeapon)!.root.visible = false;
      gun.root.visible = true;
    }

    this.adsAmt += ((o.ads ? 1 : 0) - this.adsAmt) * Math.min(1, 14 * o.dt);
    this.kickAmt = Math.max(0, this.kickAmt - 16 * o.dt);
    this.sprintAmt += ((o.sprinting && !o.ads ? 1 : 0) - this.sprintAmt) * Math.min(1, 8 * o.dt);

    const swayTargetX = THREE.MathUtils.clamp(-o.lookDX * 0.012, -0.03, 0.03);
    const swayTargetY = THREE.MathUtils.clamp(o.lookDY * 0.01, -0.02, 0.02);
    this.swayX += (swayTargetX - this.swayX) * Math.min(1, 10 * o.dt);
    this.swayY += (swayTargetY - this.swayY) * Math.min(1, 10 * o.dt);

    const hip = new THREE.Vector3(0.24, -0.22, -0.5);
    const ads = new THREE.Vector3(0, -gun.sightY, -0.32);
    const pos = hip.clone().lerp(ads, this.adsAmt);
    const bobK = (1 - this.adsAmt) * (o.onGround ? 1 : 0.2);
    pos.x += (Math.sin(this.bobT) * 0.012 + this.swayX) * bobK + Math.sin(this.idleT * 1.2) * 0.0016;
    pos.y += (-Math.abs(Math.cos(this.bobT)) * 0.01 + this.swayY) * bobK + Math.cos(this.idleT * 1.7) * 0.0012;
    pos.x += this.sprintAmt * 0.05;
    pos.y += this.sprintAmt * 0.07 + switchDown * -0.35;
    pos.z += this.sprintAmt * 0.12 + this.kickAmt * 0.07 + switchDown * -0.12;
    this.group.position.copy(pos);

    let rotX = this.kickAmt * 0.06 + switchDown * 0.5 + this.sprintAmt * -0.45;
    const rotZ = this.sprintAmt * -0.25 - this.adsAmt * 0.02 + this.swayX * 0.8;
    rotX += Math.sin(this.idleT * 1.1) * 0.004 * (1 - this.adsAmt);
    this.group.rotation.set(rotX, this.sprintAmt * 0.3, rotZ);

    this.group.visible = !(this.active === 'sr' && this.adsAmt > 0.7);

    // 换弹动画（服务端 rl 驱动，0 → total）
    if (o.reloadT > 0 && o.reloadTotal > 0) {
      this.reloadPhase = 1 - Math.min(1, o.reloadT / o.reloadTotal);
      this.applyReload(gun, this.reloadPhase);
    } else if (this.reloadPhase > 0) {
      this.applyReload(gun, 1);
      this.reloadPhase = 0;
      this.resetAnimRefs(gun);
    }
    this.applyFireAnim(gun, this.fireAnimT);
  }

  private resetAnimRefs(gun: GunModel): void {
    if (gun.mag) {
      gun.mag.position.set(0, 0, 0);
      gun.mag.rotation.set(0, 0, 0);
      gun.mag.visible = true;
    }
    for (const s of gun.shells) s.visible = true;
  }

  private applyReload(gun: GunModel, t: number): void {
    const dip = t < 0.15 ? t / 0.15 : t > 0.85 ? (1 - t) / 0.15 : 1;
    this.group.rotation.x += dip * 0.28;
    this.group.rotation.z += dip * 0.18;
    this.group.position.y -= dip * 0.06;
    if (gun.reloadStyle === 'mag' && gun.mag) {
      if (t < 0.3) {
        gun.mag.visible = true;
        const k = t / 0.3;
        gun.mag.position.y = -k * 0.16;
        gun.mag.rotation.z = k * 0.5;
      } else if (t < 0.45) {
        gun.mag.visible = false;
      } else if (t < 0.72) {
        gun.mag.visible = true;
        const k = (t - 0.45) / 0.27;
        gun.mag.position.y = -0.16 * (1 - k);
        gun.mag.rotation.z = 0.5 * (1 - k);
      } else {
        gun.mag.position.set(0, 0, 0);
        gun.mag.rotation.set(0, 0, 0);
      }
      if (t > 0.72 && gun.bolt) {
        const k = Math.min(1, (t - 0.72) / 0.28);
        gun.bolt.position.z = k < 0.5 ? k * 2 * 0.05 : (1 - k) * 2 * 0.05;
      }
    } else if (gun.reloadStyle === 'shell') {
      const per = 0.8 / Math.max(1, gun.shells.length);
      gun.shells.forEach((s, i) => {
        const start = 0.1 + i * per;
        const k = Math.max(0, Math.min(1, (t - start) / per));
        if (k >= 1) {
          s.visible = false;
        } else if (k > 0) {
          s.visible = true;
          s.position.y = THREE.MathUtils.lerp(s.userData.baseY as number, s.userData.baseY - 0.12, Math.sin(k * Math.PI));
          s.position.x = THREE.MathUtils.lerp(s.userData.baseX as number, s.userData.baseX - 0.1, Math.sin(k * Math.PI));
        }
      });
    }
  }

  private applyFireAnim(gun: GunModel, t: number): void {
    if (this.active === 'sg' && gun.pump) {
      const k = t < 0.12 ? t / 0.12 : t < 0.24 ? 1 - (t - 0.12) / 0.12 : 0;
      gun.pump.position.z = (gun.pump.userData.baseZ as number) + k * 0.07;
    } else if (this.active === 'sr' && gun.bolt) {
      const k = t < 0.2 ? t / 0.2 : t < 0.5 ? 1 - (t - 0.2) / 0.3 : 0;
      gun.bolt.position.z = (gun.bolt.userData.baseZ as number) + k * 0.09;
      gun.bolt.rotation.z = k * 0.9;
    } else if (gun.bolt) {
      const k = t < 0.05 ? t / 0.05 : t < 0.1 ? 1 - (t - 0.05) / 0.05 : 0;
      gun.bolt.position.z = (gun.bolt.userData.baseZ as number) + k * 0.035;
    }
  }

  resize(aspect: number): void {
    this.camera.aspect = aspect;
    this.camera.updateProjectionMatrix();
  }
}

function railTeeth(g: THREE.Group, count: number, y: number, z0: number, step: number, w = 0.02): void {
  for (let i = 0; i < count; i++) g.add(box(w, 0.006, 0.012, M.darkMetal, 0, y, z0 + i * step));
}

/** 泵动霰弹枪：木质护木 + 泵动动画 + 侧装弹 + 珠形准星（~40 部件） */
function buildSg(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.062;

  root.add(box(0.05, 0.062, 0.2, M.gunMetal, 0, 0.01, 0.06));
  root.add(box(0.046, 0.05, 0.05, M.darkMetal, 0, 0.012, 0.17));

  root.add(cyl(0.0135, 0.0135, 0.5, M.darkMetal, 0, 0.032, -0.24, 20));
  root.add(cyl(0.0115, 0.0115, 0.5, M.darkMetal, 0, -0.005, -0.24, 16));
  root.add(cyl(0.0125, 0.0125, 0.02, M.boltSteel, 0, 0.032, -0.5, 16));

  const vent = new THREE.Group();
  for (let i = 0; i < 6; i++) vent.add(box(0.002, 0.012, 0.028, M.darkMetal, 0.0135, 0.032, -0.12 - i * 0.032));
  root.add(vent);

  const bead = new THREE.Mesh(new THREE.SphereGeometry(0.0035, 10, 8), M.sightDot);
  bead.position.set(0, sightY + 0.012, -0.48);
  root.add(bead);
  root.add(box(0.006, 0.012, 0.01, M.darkMetal, 0, sightY, -0.47));
  root.add(box(0.03, 0.012, 0.02, M.darkMetal, 0, sightY - 0.012, 0.1));
  root.add(box(0.024, 0.006, 0.014, M.darkMetal, 0, sightY - 0.004, 0.1));

  const pump = new THREE.Group();
  pump.userData.baseZ = -0.19;
  const pumpBody = new THREE.Mesh(new THREE.CylinderGeometry(0.024, 0.024, 0.14, 16), M.wood);
  pumpBody.rotation.x = Math.PI / 2;
  pump.add(pumpBody);
  for (let i = 0; i < 3; i++) pump.add(torus(0.0245, 0.003, M.darkMetal, 0, 0, -0.05 + i * 0.05, 16));
  for (let i = 0; i < 5; i++) pump.add(box(0.002, 0.01, 0.1, M.wood, 0.0235, 0, 0));
  pump.position.set(0, -0.005, -0.19);
  root.add(pump);

  const shells: THREE.Mesh[] = [];
  for (let i = 0; i < 3; i++) {
    const s = new THREE.Mesh(new THREE.CylinderGeometry(0.0085, 0.0085, 0.036, 12), M.shellRed);
    s.rotation.z = Math.PI / 2;
    s.rotation.y = 0.12;
    s.position.set(-0.03, -0.012 - i * 0.016, 0.12);
    s.userData.baseY = s.position.y;
    s.userData.baseX = s.position.x;
    shells.push(s);
    root.add(s);
    const base = cyl(0.0088, 0.0088, 0.008, M.brass, -0.03, -0.012 - i * 0.016, 0.138, 12, 'z');
    base.rotation.y = 0.12;
    root.add(base);
  }

  root.add(box(0.03, 0.005, 0.052, M.darkMetal, 0, -0.032, 0.065));
  root.add(box(0.008, 0.016, 0.006, M.boltSteel, 0, -0.038, 0.062));
  const grip = box(0.032, 0.072, 0.042, M.wood, 0, -0.05, 0.14, 0.008);
  grip.rotation.x = 0.38;
  root.add(grip);

  const stock = new THREE.Group();
  stock.add(box(0.038, 0.052, 0.1, M.wood, 0, 0.006, 0.21, 0.01));
  stock.add(box(0.042, 0.06, 0.12, M.wood, 0, -0.002, 0.31, 0.012));
  stock.add(box(0.046, 0.078, 0.016, M.polymer, 0, -0.006, 0.375, 0.006));
  root.add(stock);

  root.add(box(0.012, 0.014, 0.016, M.boltSteel, 0.028, 0.03, 0.06));
  root.add(box(0.014, 0.008, 0.02, M.boltSteel, 0, 0.045, 0.05));

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.032, -0.51),
    mag: null,
    bolt: null,
    pump,
    shells,
    reloadStyle: 'shell',
    ejectZ: 0.05,
  };
}

/** 栓动狙击：锥形枪管 + 双腔制退器 + 精瞄镜筒（透射镜片/塔轮/遮光罩）+ 栓动动画（~46 部件） */
function buildSr(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.108;

  root.add(box(0.052, 0.066, 0.3, M.gunMetal, 0, 0.016, 0.02));
  root.add(box(0.034, 0.01, 0.34, M.darkMetal, 0, 0.055, 0));
  railTeeth(root, 8, 0.061, -0.14, 0.024);

  root.add(cyl(0.017, 0.015, 0.3, M.darkMetal, 0, 0.018, -0.32, 18));
  root.add(cyl(0.015, 0.013, 0.18, M.darkMetal, 0, 0.018, -0.55, 18));
  root.add(cyl(0.017, 0.017, 0.05, M.gunMetal, 0, 0.018, -0.66, 18));
  const brake = new THREE.Group();
  brake.add(cyl(0.019, 0.019, 0.05, M.boltSteel, 0, 0.018, -0.7, 18));
  brake.add(box(0.04, 0.008, 0.012, M.darkMetal, 0, 0.018, -0.69));
  brake.add(box(0.008, 0.026, 0.012, M.darkMetal, 0, 0.018, -0.7));
  brake.add(box(0.008, 0.026, 0.012, M.darkMetal, 0, 0.018, -0.72));
  root.add(brake);

  const scope = new THREE.Group();
  scope.add(cyl(0.02, 0.02, 0.2, M.gunMetal, 0, sightY, -0.04, 20));
  scope.add(cyl(0.026, 0.021, 0.05, M.gunMetal, 0, sightY, -0.16, 20));
  scope.add(cyl(0.021, 0.025, 0.05, M.gunMetal, 0, sightY, 0.08, 20));
  scope.add(cyl(0.028, 0.026, 0.03, M.darkMetal, 0, sightY, 0.11, 20));
  const lensF = new THREE.Mesh(new THREE.CircleGeometry(0.0245, 24), M.lens);
  lensF.position.set(0, sightY, -0.185);
  lensF.rotation.y = Math.PI;
  scope.add(lensF);
  const lensR = new THREE.Mesh(new THREE.CircleGeometry(0.022, 24), M.lens);
  lensR.position.set(0, sightY, 0.126);
  scope.add(lensR);
  scope.add(cyl(0.014, 0.014, 0.02, M.darkMetal, 0, sightY + 0.024, -0.02, 14, 'y'));
  scope.add(cyl(0.011, 0.011, 0.016, M.darkMetal, 0.021, sightY + 0.022, -0.02, 12, 'x'));
  scope.add(cyl(0.011, 0.011, 0.016, M.darkMetal, -0.021, sightY + 0.022, -0.02, 12, 'x'));
  scope.add(box(0.014, 0.016, 0.02, M.darkMetal, 0, sightY - 0.022, -0.08));
  scope.add(box(0.014, 0.016, 0.02, M.darkMetal, 0, sightY - 0.022, 0.02));
  root.add(scope);

  const bolt = new THREE.Group();
  bolt.userData.baseZ = 0.14;
  const handle = cyl(0.004, 0.004, 0.03, M.boltSteel, 0.032, 0.028, 0.14, 10, 'x');
  bolt.add(handle);
  const knob = new THREE.Mesh(new THREE.SphereGeometry(0.008, 10, 10), M.boltSteel);
  knob.position.set(0.048, 0.028, 0.14);
  bolt.add(knob);
  bolt.add(box(0.008, 0.012, 0.05, M.boltSteel, 0, 0.028, 0.12));
  root.add(bolt);

  const mag = new THREE.Group();
  mag.add(box(0.028, 0.04, 0.07, M.polymer, 0, -0.035, -0.03, 0.005));
  mag.add(box(0.03, 0.008, 0.072, M.darkMetal, 0, -0.057, -0.03, 0.003));
  root.add(mag);

  const chassis = new THREE.Group();
  chassis.add(box(0.044, 0.05, 0.22, M.polymer, 0, 0.004, 0.24, 0.012));
  chassis.add(box(0.036, 0.02, 0.16, M.polymer, 0, 0.05, 0.26, 0.006));
  chassis.add(box(0.05, 0.026, 0.03, M.darkMetal, 0, 0.062, 0.24));
  chassis.add(box(0.048, 0.075, 0.02, M.polymer, 0, 0.006, 0.36, 0.006));
  chassis.add(box(0.036, 0.012, 0.2, M.darkMetal, 0, -0.022, 0.22));
  root.add(chassis);

  const grip = box(0.03, 0.066, 0.04, M.polymer, 0, -0.042, 0.13, 0.008);
  grip.rotation.x = 0.3;
  root.add(grip);
  for (let i = 0; i < 3; i++) root.add(cyl(0.004, 0.004, 0.036, M.darkMetal, 0, -0.042 - i * 0.014, 0.152, 8, 'x'));

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.018, -0.73),
    mag,
    bolt,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: 0.12,
  };
}

/** 突击步枪：导轨机匣 + 八棱护木 + M-LOK + 弧形弹匣 + 真铁瞄（~48 部件） */
function buildAr(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.075;

  root.add(box(0.048, 0.052, 0.26, M.polymer, 0, 0, 0.02));
  root.add(box(0.052, 0.046, 0.3, M.gunMetal, 0, 0.042, -0.02));
  root.add(box(0.03, 0.008, 0.34, M.darkMetal, 0, 0.068, -0.03));
  railTeeth(root, 7, 0.074, -0.16, 0.024);
  root.add(box(0.02, 0.012, 0.03, M.boltSteel, 0, 0.068, 0.11));

  const bolt = new THREE.Group();
  bolt.userData.baseZ = 0;
  bolt.add(box(0.008, 0.02, 0.05, M.boltSteel, 0.024, 0.03, 0.03));
  root.add(bolt);

  root.add(box(0.012, 0.02, 0.05, M.darkMetal, -0.026, 0.032, 0.02));
  root.add(box(0.01, 0.014, 0.04, M.darkMetal, 0.026, 0.032, 0.0));

  const handguard = new THREE.Group();
  const oct = new THREE.Mesh(new THREE.CylinderGeometry(0.026, 0.026, 0.24, 8), M.polymer);
  oct.rotation.x = Math.PI / 2;
  oct.rotation.y = Math.PI / 8;
  handguard.add(oct);
  for (let i = 0; i < 3; i++) {
    handguard.add(box(0.056, 0.007, 0.05, M.darkMetal, 0, -0.019, -0.1 - i * 0.07));
    handguard.add(box(0.007, 0.03, 0.05, M.darkMetal, 0.027, 0, -0.1 - i * 0.07));
    handguard.add(box(0.007, 0.03, 0.05, M.darkMetal, -0.027, 0, -0.1 - i * 0.07));
  }
  handguard.add(box(0.03, 0.008, 0.24, M.darkMetal, 0, 0.026, -0.1));
  handguard.position.set(0, 0.028, -0.14);
  root.add(handguard);

  root.add(cyl(0.008, 0.008, 0.2, M.darkMetal, 0, 0.024, -0.3, 16));
  root.add(box(0.016, 0.018, 0.02, M.gunMetal, 0, 0.03, -0.24));
  root.add(cyl(0.011, 0.011, 0.05, M.darkMetal, 0, 0.024, -0.4, 16));
  root.add(cyl(0.013, 0.013, 0.008, M.boltSteel, 0, 0.024, -0.425, 16));
  root.add(box(0.005, 0.018, 0.03, M.darkMetal, 0, 0.024, -0.4));

  const fsight = new THREE.Group();
  fsight.add(box(0.008, 0.026, 0.008, M.darkMetal, 0, sightY - 0.02, -0.2));
  fsight.add(torus(0.008, 0.0022, M.darkMetal, 0, sightY, -0.2, 12));
  const dot = new THREE.Mesh(new THREE.SphereGeometry(0.0018, 8, 8), M.sightDot);
  dot.position.set(0, sightY, -0.2);
  fsight.add(dot);
  root.add(fsight);

  const rsight = new THREE.Group();
  rsight.add(box(0.006, 0.02, 0.006, M.darkMetal, -0.011, sightY - 0.017, 0.1));
  rsight.add(box(0.006, 0.02, 0.006, M.darkMetal, 0.011, sightY - 0.017, 0.1));
  rsight.add(torus(0.011, 0.0022, M.darkMetal, 0, sightY, 0.1, 14));
  root.add(rsight);

  const mag = new THREE.Group();
  mag.userData.baseY = 0;
  for (let i = 0; i < 3; i++) {
    const seg = box(0.026, 0.032, 0.055, M.polymer, 0, -0.05 - i * 0.03, 0.052 + i * 0.011, 0.006);
    seg.rotation.x = i * 0.14;
    mag.add(seg);
  }
  mag.add(box(0.028, 0.01, 0.058, M.darkMetal, 0, -0.142, 0.075, 0.003));
  root.add(mag);
  root.add(box(0.032, 0.036, 0.062, M.gunMetal, 0, -0.03, 0.05));

  const grip = box(0.03, 0.07, 0.038, M.polymer, 0, -0.045, 0.115, 0.008);
  grip.rotation.x = 0.32;
  root.add(grip);
  root.add(box(0.026, 0.006, 0.05, M.darkMetal, 0, -0.022, 0.075));
  root.add(box(0.008, 0.018, 0.006, M.boltSteel, 0, -0.028, 0.075));

  root.add(cyl(0.014, 0.014, 0.13, M.gunMetal, 0, 0.024, 0.16, 12));
  const stock = new THREE.Group();
  stock.add(box(0.036, 0.056, 0.14, M.polymer, 0, 0.014, 0.26, 0.01));
  stock.add(box(0.03, 0.014, 0.12, M.polymer, 0, 0.048, 0.25, 0.005));
  stock.add(box(0.04, 0.072, 0.018, M.polymer, 0, 0.012, 0.335, 0.006));
  stock.add(torus(0.008, 0.002, M.darkMetal, 0.02, 0.01, 0.22, 10));
  root.add(stock);

  root.add(box(0.014, 0.01, 0.024, M.boltSteel, 0, 0.068, 0.155));
  root.add(box(0.005, 0.016, 0.005, M.boltSteel, -0.008, 0.062, 0.163));
  root.add(box(0.005, 0.016, 0.005, M.boltSteel, 0.008, 0.062, 0.163));

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.024, -0.43),
    mag,
    bolt,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: 0.02,
  };
}

/** [M11] 冲锋枪：紧凑机匣 + 短枪管 + 长弧弹匣 + 折叠托 + 红点环瞄（~38 部件） */
function buildSmg(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.07;

  root.add(box(0.044, 0.05, 0.2, M.polymer, 0, 0, 0.02));
  root.add(box(0.048, 0.042, 0.2, M.gunMetal, 0, 0.038, -0.04));
  root.add(box(0.026, 0.007, 0.22, M.darkMetal, 0, 0.062, -0.04));
  railTeeth(root, 6, 0.068, -0.12, 0.024);

  const bolt = new THREE.Group();
  bolt.userData.baseZ = 0.02;
  bolt.add(box(0.008, 0.018, 0.04, M.boltSteel, 0.023, 0.028, 0.02));
  root.add(bolt);

  root.add(cyl(0.009, 0.009, 0.14, M.darkMetal, 0, 0.024, -0.2, 14));
  root.add(cyl(0.014, 0.014, 0.09, M.gunMetal, 0, 0.024, -0.29, 16));
  root.add(cyl(0.012, 0.012, 0.01, M.boltSteel, 0, 0.024, -0.335, 16));
  root.add(box(0.004, 0.016, 0.026, M.darkMetal, 0, 0.024, -0.3));

  const rd = new THREE.Group();
  rd.add(box(0.026, 0.026, 0.03, M.polymer, 0, sightY + 0.004, 0.02, 0.006));
  rd.add(torus(0.011, 0.0022, M.darkMetal, 0, sightY + 0.004, 0.006, 14));
  const dot = new THREE.Mesh(new THREE.SphereGeometry(0.0018, 8, 8), M.sightDot);
  dot.position.set(0, sightY + 0.004, 0.006);
  rd.add(dot);
  rd.add(box(0.008, 0.016, 0.008, M.darkMetal, 0, sightY - 0.014, 0.02));
  root.add(rd);

  const fsight = new THREE.Group();
  fsight.add(box(0.008, 0.02, 0.008, M.darkMetal, 0, sightY - 0.016, -0.17));
  fsight.add(torus(0.008, 0.002, M.darkMetal, 0, sightY, -0.17, 12));
  root.add(fsight);

  const mag = new THREE.Group();
  for (let i = 0; i < 4; i++) {
    const seg = box(0.024, 0.03, 0.05, M.polymer, 0, -0.048 - i * 0.028, 0.035 + i * 0.013, 0.005);
    seg.rotation.x = i * 0.17;
    mag.add(seg);
  }
  root.add(mag);
  root.add(box(0.03, 0.032, 0.058, M.gunMetal, 0, -0.028, 0.035));

  const foreGrip = box(0.024, 0.05, 0.026, M.polymer, 0, -0.038, -0.13, 0.006);
  foreGrip.rotation.x = -0.18;
  root.add(foreGrip);

  const grip = box(0.028, 0.066, 0.036, M.polymer, 0, -0.042, 0.1, 0.008);
  grip.rotation.x = 0.34;
  root.add(grip);
  root.add(box(0.024, 0.005, 0.042, M.darkMetal, 0, -0.02, 0.062));
  root.add(box(0.007, 0.016, 0.006, M.boltSteel, 0, -0.026, 0.062));

  const stock = new THREE.Group();
  stock.add(cyl(0.006, 0.006, 0.16, M.darkMetal, 0.016, 0.03, 0.19, 10, 'z'));
  stock.add(cyl(0.006, 0.006, 0.16, M.darkMetal, -0.016, 0.03, 0.19, 10, 'z'));
  stock.add(box(0.05, 0.062, 0.02, M.polymer, 0, 0.018, 0.27, 0.006));
  stock.add(box(0.03, 0.04, 0.05, M.polymer, 0, 0.012, 0.2, 0.008));
  root.add(stock);

  root.add(box(0.01, 0.012, 0.016, M.boltSteel, 0.024, 0.024, -0.02));

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.024, -0.34),
    mag,
    bolt,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: 0.02,
  };
}

/** [M11] 轻机枪：厚重机匣 + 粗长枪管 + 两脚架 + 弹箱 + 提把（~36 部件） */
function buildLmg(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.082;

  root.add(box(0.056, 0.066, 0.3, M.gunMetal, 0, 0.01, 0.04));
  root.add(box(0.05, 0.03, 0.26, M.polymer, 0, 0.05, 0));
  root.add(box(0.03, 0.009, 0.3, M.darkMetal, 0, 0.068, 0));
  railTeeth(root, 8, 0.074, -0.1, 0.026);

  const handle = new THREE.Group();
  handle.add(box(0.014, 0.008, 0.07, M.darkMetal, 0, 0.082, 0.06));
  handle.add(box(0.012, 0.024, 0.01, M.darkMetal, 0, 0.07, 0.09));
  handle.add(box(0.012, 0.024, 0.01, M.darkMetal, 0, 0.07, 0.03));
  root.add(handle);

  const bolt = new THREE.Group();
  bolt.userData.baseZ = 0.04;
  bolt.add(box(0.009, 0.02, 0.045, M.boltSteel, 0.028, 0.026, 0.04));
  root.add(bolt);

  root.add(cyl(0.013, 0.011, 0.34, M.darkMetal, 0, 0.02, -0.26, 16));
  root.add(cyl(0.015, 0.015, 0.07, M.gunMetal, 0, 0.02, -0.46, 16));
  for (let i = 0; i < 4; i++) root.add(box(0.004, 0.02, 0.03, M.darkMetal, 0, 0.02, -0.38 - i * 0.04));
  root.add(cyl(0.013, 0.013, 0.008, M.boltSteel, 0, 0.02, -0.5, 16));

  const bipod = new THREE.Group();
  const legL = cyl(0.004, 0.003, 0.12, M.darkMetal, 0.02, -0.03, -0.44, 8);
  legL.rotation.z = 0.35;
  const legR = cyl(0.004, 0.003, 0.12, M.darkMetal, -0.02, -0.03, -0.44, 8);
  legR.rotation.z = -0.35;
  bipod.add(legL, legR);
  root.add(bipod);

  const rsight = new THREE.Group();
  rsight.add(box(0.006, 0.022, 0.006, M.darkMetal, -0.011, sightY - 0.016, 0.1));
  rsight.add(box(0.006, 0.022, 0.006, M.darkMetal, 0.011, sightY - 0.016, 0.1));
  rsight.add(torus(0.011, 0.0022, M.darkMetal, 0, sightY, 0.1, 14));
  root.add(rsight);
  const fsight = new THREE.Group();
  fsight.add(box(0.008, 0.026, 0.008, M.darkMetal, 0, sightY - 0.018, -0.2));
  fsight.add(torus(0.008, 0.002, M.darkMetal, 0, sightY, -0.2, 12));
  root.add(fsight);

  const mag = new THREE.Group();
  mag.add(box(0.07, 0.075, 0.1, M.polymer, 0, -0.06, 0.06, 0.008));
  mag.add(box(0.074, 0.012, 0.104, M.darkMetal, 0, -0.1, 0.06, 0.004));
  mag.add(box(0.02, 0.05, 0.02, M.darkMetal, 0.037, -0.06, 0.06));
  root.add(mag);
  root.add(box(0.05, 0.03, 0.104, M.gunMetal, 0, -0.02, 0.06));

  const grip = box(0.03, 0.068, 0.038, M.polymer, 0, -0.044, 0.15, 0.008);
  grip.rotation.x = 0.32;
  root.add(grip);
  root.add(box(0.026, 0.005, 0.05, M.darkMetal, 0, -0.022, 0.1));
  root.add(box(0.008, 0.018, 0.006, M.boltSteel, 0, -0.028, 0.1));

  const stock = new THREE.Group();
  stock.add(box(0.04, 0.06, 0.16, M.polymer, 0, 0.016, 0.26, 0.01));
  stock.add(box(0.034, 0.016, 0.12, M.polymer, 0, 0.052, 0.26, 0.005));
  stock.add(box(0.044, 0.08, 0.018, M.polymer, 0, 0.012, 0.35, 0.006));
  root.add(stock);

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.02, -0.51),
    mag,
    bolt,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: 0.04,
  };
}

/** [M11] 射手步枪：修长机匣 + 细长枪管 + 中倍瞄镜 + 骨架托（~34 部件） */
function buildDmr(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.096;

  root.add(box(0.046, 0.058, 0.28, M.gunMetal, 0, 0.012, 0.04));
  root.add(box(0.03, 0.008, 0.3, M.darkMetal, 0, 0.052, 0.02));
  railTeeth(root, 7, 0.058, -0.1, 0.026);

  const bolt = new THREE.Group();
  bolt.userData.baseZ = 0.12;
  const handle = cyl(0.004, 0.004, 0.026, M.boltSteel, 0.03, 0.026, 0.12, 10, 'x');
  bolt.add(handle);
  const knob = new THREE.Mesh(new THREE.SphereGeometry(0.007, 10, 10), M.boltSteel);
  knob.position.set(0.044, 0.026, 0.12);
  bolt.add(knob);
  root.add(bolt);

  root.add(cyl(0.012, 0.01, 0.3, M.darkMetal, 0, 0.016, -0.25, 16));
  root.add(cyl(0.013, 0.013, 0.06, M.gunMetal, 0, 0.016, -0.42, 16));
  root.add(cyl(0.011, 0.011, 0.008, M.boltSteel, 0, 0.016, -0.45, 16));

  const scope = new THREE.Group();
  scope.add(cyl(0.016, 0.016, 0.16, M.gunMetal, 0, sightY, -0.04, 18));
  scope.add(cyl(0.02, 0.016, 0.04, M.gunMetal, 0, sightY, -0.14, 18));
  scope.add(cyl(0.016, 0.019, 0.04, M.gunMetal, 0, sightY, 0.06, 18));
  scope.add(cyl(0.012, 0.012, 0.018, M.darkMetal, 0, sightY + 0.02, -0.04, 12, 'y'));
  scope.add(box(0.012, 0.014, 0.018, M.darkMetal, 0, sightY - 0.018, -0.1));
  scope.add(box(0.012, 0.014, 0.018, M.darkMetal, 0, sightY - 0.018, 0.02));
  const lensF = new THREE.Mesh(new THREE.CircleGeometry(0.018, 20), M.lens);
  lensF.position.set(0, sightY, -0.161);
  lensF.rotation.y = Math.PI;
  scope.add(lensF);
  root.add(scope);

  const mag = new THREE.Group();
  mag.add(box(0.026, 0.05, 0.06, M.polymer, 0, -0.042, 0.02, 0.005));
  mag.add(box(0.028, 0.008, 0.062, M.darkMetal, 0, -0.07, 0.024, 0.003));
  root.add(mag);
  root.add(box(0.032, 0.028, 0.066, M.gunMetal, 0, -0.022, 0.02));

  const grip = box(0.028, 0.066, 0.038, M.polymer, 0, -0.042, 0.13, 0.008);
  grip.rotation.x = 0.32;
  root.add(grip);
  root.add(box(0.024, 0.005, 0.042, M.darkMetal, 0, -0.02, 0.08));
  root.add(box(0.007, 0.016, 0.006, M.boltSteel, 0, -0.026, 0.08));

  const stock = new THREE.Group();
  stock.add(box(0.036, 0.01, 0.2, M.polymer, 0, 0.044, 0.26, 0.005));
  stock.add(box(0.036, 0.01, 0.2, M.polymer, 0, -0.016, 0.26, 0.005));
  stock.add(box(0.036, 0.06, 0.016, M.polymer, 0, 0.014, 0.36, 0.006));
  stock.add(box(0.03, 0.03, 0.1, M.polymer, 0, -0.006, 0.26, 0.006));
  stock.add(box(0.03, 0.012, 0.14, M.darkMetal, 0, 0.014, 0.26));
  root.add(stock);

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.016, -0.46),
    mag,
    bolt,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: 0.1,
  };
}

/** [M11] 手枪：套筒 + 握把 + 短枪管 + 板机护圈（~22 部件） */
function buildPistol(): GunModel {
  const root = new THREE.Group();
  const sightY = 0.028;

  const slide = new THREE.Group();
  slide.userData.baseZ = 0;
  slide.add(box(0.03, 0.032, 0.19, M.gunMetal, 0, 0.018, -0.03));
  for (let i = 0; i < 4; i++) slide.add(box(0.004, 0.01, 0.016, M.darkMetal, 0.016, 0.018, -0.08 + i * 0.02));
  slide.add(box(0.026, 0.008, 0.02, M.darkMetal, 0, 0.036, -0.11));
  slide.add(box(0.006, 0.008, 0.008, M.darkMetal, -0.008, 0.036, -0.11));
  slide.add(box(0.006, 0.008, 0.008, M.darkMetal, 0.008, 0.036, -0.11));
  slide.add(box(0.006, 0.008, 0.008, M.darkMetal, -0.008, 0.036, 0.05));
  slide.add(box(0.006, 0.008, 0.008, M.darkMetal, 0.008, 0.036, 0.05));
  const dot = new THREE.Mesh(new THREE.SphereGeometry(0.0014, 8, 8), M.sightDot);
  dot.position.set(0, sightY + 0.012, -0.11);
  slide.add(dot);
  root.add(slide);

  root.add(box(0.028, 0.026, 0.17, M.polymer, 0, -0.006, -0.02));
  root.add(cyl(0.008, 0.008, 0.03, M.darkMetal, 0, 0.008, -0.125, 12));
  root.add(cyl(0.009, 0.009, 0.006, M.boltSteel, 0, 0.008, -0.14, 12));

  root.add(box(0.024, 0.006, 0.05, M.darkMetal, 0, -0.02, -0.04));
  root.add(torus(0.014, 0.003, M.polymer, 0, -0.032, -0.01, 14));

  const mag = new THREE.Group();
  mag.add(box(0.022, 0.06, 0.032, M.darkMetal, 0, -0.062, 0.012, 0.004));
  mag.add(box(0.024, 0.008, 0.034, M.boltSteel, 0, -0.094, 0.012, 0.003));
  root.add(mag);

  const grip = box(0.03, 0.084, 0.042, M.polymer, 0, -0.044, 0.024, 0.008);
  grip.rotation.x = 0.28;
  root.add(grip);
  for (let i = 0; i < 3; i++) root.add(box(0.032, 0.006, 0.03, M.darkMetal, 0, -0.03 - i * 0.014, 0.006 + i * 0.004));

  const hammer = box(0.008, 0.016, 0.008, M.boltSteel, 0, 0.024, 0.07);
  hammer.rotation.x = -0.4;
  root.add(hammer);

  return {
    root,
    sightY,
    muzzle: new THREE.Vector3(0, 0.008, -0.145),
    mag,
    bolt: slide,
    pump: null,
    shells: [],
    reloadStyle: 'mag',
    ejectZ: -0.03,
  };
}

const BUILDERS: Record<WeaponId, () => GunModel> = {
  ar: buildAr,
  sg: buildSg,
  sr: buildSr,
  smg: buildSmg,
  lmg: buildLmg,
  dmr: buildDmr,
  pistol: buildPistol,
};
