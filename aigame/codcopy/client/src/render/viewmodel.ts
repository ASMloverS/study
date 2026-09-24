import * as THREE from 'three';
import { WEAPONS, type WeaponId } from 'shared';

const M = {
  gunMetal: new THREE.MeshStandardMaterial({ color: 0x2a2d31, metalness: 0.85, roughness: 0.35 }),
  darkMetal: new THREE.MeshStandardMaterial({ color: 0x1a1c20, metalness: 0.75, roughness: 0.45 }),
  polymer: new THREE.MeshStandardMaterial({ color: 0x3a3f45, metalness: 0.15, roughness: 0.8 }),
  wood: new THREE.MeshStandardMaterial({ color: 0x6b4a2a, metalness: 0.02, roughness: 0.8 }),
  glove: new THREE.MeshStandardMaterial({ color: 0x33302c, metalness: 0.05, roughness: 0.9 }),
  skin: new THREE.MeshStandardMaterial({ color: 0xc9a184, metalness: 0, roughness: 0.9 }),
  shellRed: new THREE.MeshStandardMaterial({ color: 0xa03028, metalness: 0.1, roughness: 0.6 }),
  lens: new THREE.MeshStandardMaterial({ color: 0x223a52, metalness: 0.9, roughness: 0.12 }),
  sightDot: new THREE.MeshBasicMaterial({ color: 0xffdd66 }),
};

function box(
  w: number,
  h: number,
  d: number,
  mat: THREE.Material,
  x: number,
  y: number,
  z: number,
  rx = 0,
  ry = 0,
  rz = 0,
): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), mat);
  m.position.set(x, y, z);
  m.rotation.set(rx, ry, rz);
  return m;
}

function cyl(
  r1: number,
  r2: number,
  h: number,
  mat: THREE.Material,
  x: number,
  y: number,
  z: number,
  rx = Math.PI / 2,
  seg = 10,
): THREE.Mesh {
  const m = new THREE.Mesh(new THREE.CylinderGeometry(r1, r2, h, seg), mat);
  m.position.set(x, y, z);
  m.rotation.x = rx;
  return m;
}

function handRight(g: THREE.Group, gripX: number, gripY: number, gripZ: number): void {
  g.add(box(0.06, 0.095, 0.075, M.glove, gripX, gripY, gripZ, 0.28, 0, 0));
  const forearm = cyl(0.034, 0.042, 0.3, M.skin, gripX + 0.075, gripY - 0.13, gripZ + 0.16, Math.PI / 2);
  forearm.rotation.z = 0.55;
  forearm.rotation.x = Math.PI / 2 - 0.45;
  g.add(forearm);
}

function handLeft(g: THREE.Group, hx: number, hy: number, hz: number, armAng: number): void {
  g.add(box(0.07, 0.075, 0.1, M.glove, hx, hy, hz));
  const forearm = cyl(0.034, 0.042, 0.32, M.skin, hx - 0.09, hy - 0.13, hz + 0.18, Math.PI / 2);
  forearm.rotation.z = -0.5;
  forearm.rotation.x = Math.PI / 2 - armAng;
  g.add(forearm);
}

export class ViewModel {
  readonly group = new THREE.Group();
  private adsAmt = 0;
  private kickAmt = 0;
  private bobT = 0;
  private switchT = -1;
  private sprintAmt = 0;
  private weapon: WeaponId = 'ar';
  private readonly models = new Map<WeaponId, THREE.Group>();
  private readonly flashPos = new Map<WeaponId, THREE.Vector3>();
  private readonly flashMat: THREE.MeshBasicMaterial;
  private readonly flashMesh: THREE.Mesh;
  private swayX = 0;
  private swayY = 0;

  constructor(camera: THREE.PerspectiveCamera) {
    this.models.set('ar', this.buildAr());
    this.models.set('sg', this.buildSg());
    this.models.set('sr', this.buildSr());
    this.flashPos.set('ar', new THREE.Vector3(0, 0.075, -0.74));
    this.flashPos.set('sg', new THREE.Vector3(0, 0.06, -0.72));
    this.flashPos.set('sr', new THREE.Vector3(0, 0.05, -1.14));
    const flash = new THREE.Mesh(
      new THREE.PlaneGeometry(0.22, 0.22),
      (this.flashMat = new THREE.MeshBasicMaterial({
        color: 0xffdd88,
        transparent: true,
        opacity: 0,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
      })),
    );
    this.flashMesh = flash;
    this.group.add(flash);
    this.group.add(this.models.get('ar')!);
    this.group.traverse((o) => {
      o.frustumCulled = false;
      if (o instanceof THREE.Mesh) o.renderOrder = 999;
    });
    camera.add(this.group);
  }

  private buildAr(): THREE.Group {
    const g = new THREE.Group();
    g.add(box(0.07, 0.09, 0.3, M.polymer, 0, 0, -0.02));
    g.add(box(0.065, 0.07, 0.34, M.gunMetal, 0, 0.075, -0.05));
    g.add(box(0.055, 0.02, 0.52, M.darkMetal, 0, 0.118, -0.12));
    for (let i = 0; i < 6; i++) g.add(box(0.06, 0.009, 0.02, M.gunMetal, 0, 0.129, -0.34 + i * 0.075));
    const handguard = cyl(0.052, 0.052, 0.28, M.polymer, 0, 0.075, -0.37, Math.PI / 2, 8);
    handguard.rotation.y = Math.PI / 8;
    g.add(handguard);
    g.add(cyl(0.014, 0.014, 0.24, M.darkMetal, 0, 0.075, -0.6));
    g.add(cyl(0.023, 0.023, 0.07, M.gunMetal, 0, 0.075, -0.71));
    g.add(box(0.012, 0.052, 0.012, M.darkMetal, 0, 0.13, -0.47));
    g.add(box(0.008, 0.008, 0.008, M.sightDot, 0, 0.158, -0.47));
    g.add(box(0.012, 0.04, 0.012, M.darkMetal, -0.022, 0.135, 0.06));
    g.add(box(0.012, 0.04, 0.012, M.darkMetal, 0.022, 0.135, 0.06));
    const aperture = new THREE.Mesh(new THREE.TorusGeometry(0.014, 0.004, 6, 12), M.darkMetal);
    aperture.position.set(0, 0.155, 0.06);
    g.add(aperture);
    g.add(box(0.05, 0.016, 0.045, M.darkMetal, 0.045, 0.098, 0.115));
    g.add(box(0.004, 0.032, 0.09, M.darkMetal, 0.034, 0.07, -0.02));
    g.add(box(0.05, 0.125, 0.075, M.polymer, 0, -0.1, -0.02, 0.1, 0, 0));
    g.add(box(0.05, 0.105, 0.07, M.polymer, 0, -0.2, 0.02, 0.26, 0, 0));
    g.add(box(0.008, 0.02, 0.06, M.darkMetal, 0, -0.055, 0.035));
    g.add(box(0.008, 0.032, 0.012, M.darkMetal, 0, -0.042, 0.02));
    g.add(box(0.045, 0.115, 0.055, M.polymer, 0, -0.1, 0.1, 0.3, 0, 0));
    g.add(cyl(0.023, 0.023, 0.18, M.darkMetal, 0, 0.05, 0.22));
    g.add(box(0.055, 0.11, 0.12, M.polymer, 0, 0.025, 0.33));
    g.add(box(0.05, 0.032, 0.1, M.polymer, 0, 0.095, 0.31));
    handRight(g, 0.005, -0.115, 0.09);
    handLeft(g, -0.012, 0.015, -0.37, 0.35);
    return g;
  }

  private buildSg(): THREE.Group {
    const g = new THREE.Group();
    g.add(box(0.075, 0.1, 0.3, M.gunMetal, 0, 0.02, -0.05));
    g.add(box(0.004, 0.032, 0.1, M.darkMetal, 0.039, 0.02, -0.05));
    g.add(cyl(0.017, 0.017, 0.52, M.darkMetal, 0, 0.062, -0.46));
    g.add(cyl(0.015, 0.015, 0.44, M.darkMetal, 0, 0.018, -0.41));
    g.add(box(0.08, 0.062, 0.19, M.wood, 0, 0.018, -0.34));
    for (let i = 0; i < 3; i++) g.add(box(0.084, 0.008, 0.014, M.wood, 0, 0.018, -0.4 + i * 0.05));
    g.add(cyl(0.008, 0.008, 0.02, M.sightDot, 0, 0.128, -0.68, Math.PI / 2, 6));
    g.add(box(0.012, 0.038, 0.012, M.darkMetal, -0.022, 0.125, 0.05));
    g.add(box(0.012, 0.038, 0.012, M.darkMetal, 0.022, 0.125, 0.05));
    g.add(box(0.052, 0.014, 0.052, M.darkMetal, 0, 0.148, 0.05));
    g.add(box(0.008, 0.02, 0.055, M.darkMetal, 0, -0.035, 0.04));
    g.add(box(0.008, 0.03, 0.012, M.darkMetal, 0, -0.022, 0.02));
    g.add(box(0.072, 0.055, 0.1, M.wood, 0, -0.035, 0.13, 0.35, 0, 0));
    g.add(box(0.07, 0.125, 0.26, M.wood, 0, -0.02, 0.3, -0.08, 0, 0));
    g.add(box(0.075, 0.035, 0.24, M.wood, 0, 0.06, 0.3));
    for (let i = 0; i < 3; i++) g.add(cyl(0.011, 0.011, 0.06, M.shellRed, 0.05, 0.06, -0.12 + i * 0.07, 0, 8));
    handRight(g, 0.005, -0.058, 0.1);
    handLeft(g, -0.012, 0.02, -0.34, 0.3);
    return g;
  }

  private buildSr(): THREE.Group {
    const g = new THREE.Group();
    g.add(box(0.07, 0.1, 0.56, M.darkMetal, 0, 0.03, -0.06));
    g.add(box(0.055, 0.018, 0.4, M.gunMetal, 0, 0.088, -0.1));
    g.add(cyl(0.02, 0.016, 0.72, M.gunMetal, 0, 0.052, -0.76));
    g.add(cyl(0.029, 0.029, 0.1, M.darkMetal, 0, 0.052, -1.1));
    for (let i = 0; i < 3; i++) g.add(box(0.06, 0.02, 0.014, M.darkMetal, 0, 0.052, -1.07 + i * 0.03));
    g.add(cyl(0.034, 0.034, 0.3, M.darkMetal, 0, 0.158, -0.06));
    g.add(cyl(0.048, 0.036, 0.1, M.darkMetal, 0, 0.158, -0.25));
    g.add(cyl(0.038, 0.046, 0.07, M.darkMetal, 0, 0.158, 0.11));
    const lens = new THREE.Mesh(new THREE.CircleGeometry(0.042, 16), M.lens);
    lens.position.set(0, 0.158, -0.295);
    g.add(lens);
    g.add(box(0.03, 0.05, 0.03, M.darkMetal, 0, 0.118, -0.02));
    g.add(box(0.03, 0.05, 0.03, M.darkMetal, 0, 0.118, 0.02));
    g.add(cyl(0.014, 0.014, 0.03, M.darkMetal, 0, 0.2, -0.06, 0, 8));
    g.add(cyl(0.014, 0.014, 0.03, M.darkMetal, 0.045, 0.158, -0.06, Math.PI / 2, 8));
    g.add(cyl(0.007, 0.007, 0.06, M.gunMetal, 0.05, 0.075, 0.1, 0, 6));
    const knob = new THREE.Mesh(new THREE.SphereGeometry(0.016, 8, 8), M.gunMetal);
    knob.position.set(0.07, 0.075, 0.1);
    g.add(knob);
    g.add(box(0.05, 0.1, 0.08, M.darkMetal, 0, -0.065, -0.06));
    g.add(box(0.06, 0.09, 0.3, M.polymer, 0, 0.005, 0.32));
    g.add(box(0.055, 0.035, 0.22, M.polymer, 0, 0.08, 0.32));
    g.add(box(0.045, 0.1, 0.05, M.polymer, 0, -0.045, 0.16, 0.3, 0, 0));
    g.add(box(0.008, 0.02, 0.05, M.darkMetal, 0, -0.09, 0.09));
    handRight(g, 0.005, -0.058, 0.15);
    handLeft(g, -0.012, -0.005, -0.45, 0.4);
    return g;
  }

  setWeapon(w: WeaponId): void {
    if (w === this.weapon) return;
    this.weapon = w;
    for (const [id, model] of this.models) model.visible = id === w;
    this.switchT = 0;
  }

  get activeWeapon(): WeaponId {
    return this.weapon;
  }

  kick(): void {
    this.kickAmt = Math.min(1, this.kickAmt + 0.6);
    this.flashMat.opacity = 1;
    this.flashMesh.rotation.z = Math.random() * Math.PI;
    const p = this.flashPos.get(this.weapon)!;
    this.flashMesh.position.copy(p);
  }

  get adsAmount(): number {
    return this.adsAmt;
  }

  update(dt: number, speed: number, onGround: boolean, ads: boolean, sprint: boolean, lookDX: number, lookDY: number): void {
    this.adsAmt += ((ads ? 1 : 0) - this.adsAmt) * Math.min(1, 14 * dt);
    this.kickAmt = Math.max(0, this.kickAmt - this.kickAmt * 11 * dt);
    this.sprintAmt += ((sprint && !ads ? 1 : 0) - this.sprintAmt) * Math.min(1, 8 * dt);
    const sprintPose = this.sprintAmt * (1 - this.adsAmt);
    const switchDur = WEAPONS[this.weapon].switchTime;
    if (this.switchT >= 0) {
      this.switchT += dt;
      if (this.switchT >= switchDur) this.switchT = -1;
    }
    const t = this.switchT < 0 ? 1 : this.switchT / switchDur;
    const switchAmt = t >= 1 ? 0 : t < 0.3 ? t / 0.3 : 1 - (t - 0.3) / 0.7;
    this.flashMat.opacity = Math.max(0, this.flashMat.opacity - dt * 18);
    if (onGround) this.bobT += dt * Math.min(speed, 7) * 1.6;
    this.swayX += (THREE.MathUtils.clamp(-lookDX * 0.02, -0.03, 0.03) - this.swayX) * Math.min(1, 10 * dt);
    this.swayY += (THREE.MathUtils.clamp(-lookDY * 0.02, -0.03, 0.03) - this.swayY) * Math.min(1, 10 * dt);
    const bobScale = (1 - this.adsAmt) * (onGround ? 1 : 0);
    const bobX = Math.sin(this.bobT) * 0.012 * bobScale;
    const bobY = -Math.abs(Math.cos(this.bobT)) * 0.01 * bobScale;
    const hipX = 0.28;
    const hipY = -0.26;
    const hipZ = -0.55;
    const adsX = 0;
    const adsY = -0.155;
    const adsZ = -0.38;
    const hide = this.weapon === 'sr' && this.adsAmt > 0.7 ? 1 : 0;
    this.group.visible = hide === 0;
    this.group.position.set(
      hipX + (adsX - hipX) * this.adsAmt + bobX + this.swayX + sprintPose * 0.05,
      hipY + (adsY - hipY) * this.adsAmt + bobY + this.swayY - switchAmt * 0.35 + sprintPose * 0.07,
      hipZ + (adsZ - hipZ) * this.adsAmt + this.kickAmt * 0.07 + sprintPose * 0.12,
    );
    this.group.rotation.x = this.kickAmt * 0.06 - switchAmt * 0.5 - sprintPose * 0.45;
    this.group.rotation.z = -this.adsAmt * 0.02 + this.swayX * 0.8 - sprintPose * 0.25;
  }
}
