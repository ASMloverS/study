import * as THREE from 'three';
import type { WeaponId } from 'shared';

export class ViewModel {
  readonly group = new THREE.Group();
  private adsAmt = 0;
  private kickAmt = 0;
  private bobT = 0;
  private switchAmt = 0;
  private weapon: WeaponId = 'ar';
  private readonly models = new Map<WeaponId, THREE.Group>();
  private readonly flashMat: THREE.MeshBasicMaterial;
  private readonly flashMesh: THREE.Mesh;
  private swayX = 0;
  private swayY = 0;

  constructor(camera: THREE.PerspectiveCamera) {
    this.models.set('ar', this.buildAr());
    this.models.set('sg', this.buildSg());
    this.models.set('sr', this.buildSr());
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
    flash.position.set(0, 0.045, -0.56);
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
    const dark = new THREE.MeshLambertMaterial({ color: 0x2c3036 });
    const grip = new THREE.MeshLambertMaterial({ color: 0x4a3b2a });
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.13, 0.55), dark);
    const barrel = new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.05, 0.3), dark);
    barrel.position.set(0, 0.045, -0.4);
    const stock = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.11, 0.22), grip);
    stock.position.set(0, -0.02, 0.35);
    const mag = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.18, 0.1), grip);
    mag.position.set(0, -0.13, 0.02);
    g.add(body, barrel, stock, mag);
    return g;
  }

  private buildSg(): THREE.Group {
    const g = new THREE.Group();
    const dark = new THREE.MeshLambertMaterial({ color: 0x3a3026 });
    const wood = new THREE.MeshLambertMaterial({ color: 0x6b4a2a });
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.11, 0.14, 0.45), dark);
    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 0.45, 8), dark);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 0.05, -0.42);
    const pump = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.07, 0.2), wood);
    pump.position.set(0, -0.02, -0.3);
    const stock = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.12, 0.26), wood);
    stock.position.set(0, -0.03, 0.36);
    g.add(body, barrel, pump, stock);
    return g;
  }

  private buildSr(): THREE.Group {
    const g = new THREE.Group();
    const dark = new THREE.MeshLambertMaterial({ color: 0x25282e });
    const grip = new THREE.MeshLambertMaterial({ color: 0x3d4750 });
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.12, 0.6), dark);
    const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.028, 0.028, 0.6, 8), dark);
    barrel.rotation.x = Math.PI / 2;
    barrel.position.set(0, 0.04, -0.55);
    const scope = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, 0.26, 10), grip);
    scope.rotation.x = Math.PI / 2;
    scope.position.set(0, 0.115, -0.1);
    const stock = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.13, 0.3), grip);
    stock.position.set(0, -0.02, 0.4);
    g.add(body, barrel, scope, stock);
    return g;
  }

  setWeapon(w: WeaponId): void {
    if (w === this.weapon) return;
    this.weapon = w;
    for (const [id, model] of this.models) model.visible = id === w;
    this.switchAmt = 1;
  }

  get activeWeapon(): WeaponId {
    return this.weapon;
  }

  kick(): void {
    this.kickAmt = Math.min(1, this.kickAmt + 0.6);
    this.flashMat.opacity = 1;
    this.flashMesh.rotation.z = Math.random() * Math.PI;
  }

  get adsAmount(): number {
    return this.adsAmt;
  }

  update(dt: number, speed: number, onGround: boolean, ads: boolean, lookDX: number, lookDY: number): void {
    this.adsAmt += ((ads ? 1 : 0) - this.adsAmt) * Math.min(1, 14 * dt);
    this.kickAmt = Math.max(0, this.kickAmt - this.kickAmt * 11 * dt);
    this.switchAmt = Math.max(0, this.switchAmt - dt / 0.5);
    this.flashMat.opacity = Math.max(0, this.flashMat.opacity - dt * 18);
    if (onGround) this.bobT += dt * Math.min(speed, 7) * 1.6;
    this.swayX += (THREE.MathUtils.clamp(-lookDX * 0.02, -0.03, 0.03) - this.swayX) * Math.min(1, 10 * dt);
    this.swayY += (THREE.MathUtils.clamp(lookDY * 0.02, -0.03, 0.03) - this.swayY) * Math.min(1, 10 * dt);
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
      hipX + (adsX - hipX) * this.adsAmt + bobX + this.swayX,
      hipY + (adsY - hipY) * this.adsAmt + bobY + this.swayY - this.switchAmt * 0.35,
      hipZ + (adsZ - hipZ) * this.adsAmt + this.kickAmt * 0.07,
    );
    this.group.rotation.x = this.kickAmt * 0.06 - this.switchAmt * 0.5;
    this.group.rotation.z = -this.adsAmt * 0.02 + this.swayX * 0.8;
  }
}
