import * as THREE from 'three';
import { MAPS, coverToAABB, type AABB, type NadeSnap } from 'shared';
import { toonMat } from './palette';

export type Quality = 'low' | 'medium' | 'high';
export type ImpactKind = 'metal' | 'concrete' | 'wood';

const COVER_AABBS: { box: AABB; kind: ImpactKind }[] = MAPS.warehouse.covers.map((c) => ({
  box: coverToAABB(c),
  kind: c.type === 'container' || c.type === 'barrel' ? 'metal' : c.type === 'crate' ? 'wood' : 'concrete',
}));

export function impactKindAt(x: number, y: number, z: number): ImpactKind {
  for (const c of COVER_AABBS) {
    const b = c.box;
    if (x >= b.minX - 0.02 && x <= b.maxX + 0.02 && y >= b.minY - 0.02 && y <= b.maxY + 0.02 && z >= b.minZ - 0.02 && z <= b.maxZ + 0.02) {
      return c.kind;
    }
  }
  return 'concrete';
}

interface ParticleOpts {
  color: number;
  size: number;
  vel: THREE.Vector3;
  life: number;
  gravity?: number;
  grow?: number;
  additive?: boolean;
  bounce?: number;
  spin?: number;
}

interface LiveParticle {
  slot: number;
  pool: InstancePool;
  vel: THREE.Vector3;
  born: number;
  life: number;
  gravity: number;
  grow: number;
  bounce: number;
  spin: number;
  color: THREE.Color;
  pos: THREE.Vector3;
  size: number;
  rotX: number;
  rotZ: number;
}

const PARTICLE_VERT = `
attribute mat4 instanceMatrix;
attribute vec3 instanceColor;
attribute float aAlpha;
varying vec3 vColor;
varying float vAlpha;
void main() {
  vColor = instanceColor;
  vAlpha = aAlpha;
  gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(position, 1.0);
}`;

const PARTICLE_FRAG = `
varying vec3 vColor;
varying float vAlpha;
uniform bool additive;
void main() {
  vec4 c = vec4(vColor, vAlpha);
  if (additive) c.rgb *= vAlpha;
  gl_FragColor = c;
}`;

/** InstancedMesh 粒子池（M7.2）：单 draw call，per-instance 颜色与透明度 */
class InstancePool {
  readonly mesh: THREE.InstancedMesh;
  private readonly free: number[] = [];
  private readonly alpha: THREE.InstancedBufferAttribute;
  private readonly dummy = new THREE.Object3D();

  constructor(scene: THREE.Scene, capacity: number, additive: boolean) {
    const geo = new THREE.BoxGeometry(1, 1, 1);
    this.alpha = new THREE.InstancedBufferAttribute(new Float32Array(capacity), 1);
    geo.setAttribute('aAlpha', this.alpha);
    const mat = new THREE.ShaderMaterial({
      vertexShader: PARTICLE_VERT,
      fragmentShader: PARTICLE_FRAG,
      uniforms: { additive: { value: additive } },
      transparent: true,
      depthWrite: false,
      blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
    });
    this.mesh = new THREE.InstancedMesh(geo, mat, capacity);
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    this.mesh.frustumCulled = false;
    this.mesh.count = capacity;
    const colors = new THREE.InstancedBufferAttribute(new Float32Array(capacity * 3), 3);
    this.mesh.instanceColor = colors;
    for (let i = capacity - 1; i >= 0; i--) {
      this.free.push(i);
      this.hide(i);
    }
    scene.add(this.mesh);
  }

  private hide(i: number): void {
    this.dummy.position.set(0, -100, 0);
    this.dummy.scale.setScalar(0.0001);
    this.dummy.rotation.set(0, 0, 0);
    this.dummy.updateMatrix();
    this.mesh.setMatrixAt(i, this.dummy.matrix);
    this.alpha.setX(i, 0);
  }

  acquire(): number | null {
    return this.free.length > 0 ? this.free.pop()! : null;
  }

  release(i: number): void {
    this.hide(i);
    this.mesh.instanceMatrix.needsUpdate = true;
    this.alpha.needsUpdate = true;
    this.free.push(i);
  }

  write(i: number, p: LiveParticle, k: number, now: number): void {
    this.dummy.position.copy(p.pos);
    this.dummy.rotation.set(p.rotX, 0, p.rotZ);
    const s = Math.max(0.0001, p.size * (1 + p.grow * ((now - p.born) / 1000)));
    this.dummy.scale.setScalar(s);
    this.dummy.updateMatrix();
    this.mesh.setMatrixAt(i, this.dummy.matrix);
    this.mesh.instanceColor!.setXYZ(i, p.color.r * k, p.color.g * k, p.color.b * k);
    this.alpha.setX(i, k);
  }

  flagUpdate(): void {
    this.mesh.instanceMatrix.needsUpdate = true;
    if (this.mesh.instanceColor) this.mesh.instanceColor.needsUpdate = true;
    this.alpha.needsUpdate = true;
  }

  dispose(): void {
    this.mesh.removeFromParent();
    this.mesh.geometry.dispose();
    (this.mesh.material as THREE.Material).dispose();
  }
}

interface TracerFx {
  mesh: THREE.Mesh;
  from: THREE.Vector3;
  dir: THREE.Vector3;
  dist: number;
  at: number;
}

const TRACER_SPEED = 180;
const DECAL_MAX = 32;

export class Effects {
  quality: Quality = 'high';
  private tracers: TracerFx[] = [];
  private readonly scene: THREE.Scene;
  private readonly decals: THREE.Mesh[] = [];
  private decalIdx = 0;
  private readonly muzzleLights: THREE.PointLight[] = [];
  private lightIdx = 0;
  private readonly rings: THREE.Mesh[] = [];
  private ringIdx = 0;
  private readonly blastLights: THREE.PointLight[] = [];
  private lightBlastIdx = 0;
  private readonly scorches: THREE.Mesh[] = [];
  private scorchIdx = 0;
  private readonly emitters: { x: number; z: number; until: number; nextAt: number }[] = [];
  private jetView: { x: number; z: number; yaw: number; born: number; group: THREE.Group; lastTrail: number } | null = null;
  private nowMs = performance.now();
  private readonly nadeViews = new Map<number, { mesh: THREE.Mesh; target: THREE.Vector3 }>();
  private readonly additivePool: InstancePool;
  private readonly alphaPool: InstancePool;
  private readonly live: LiveParticle[] = [];

  constructor(scene: THREE.Scene) {
    this.scene = scene;
    this.additivePool = new InstancePool(scene, 1024, true);
    this.alphaPool = new InstancePool(scene, 1024, false);
    const decalMat = new THREE.MeshBasicMaterial({ color: 0x22262c, transparent: true, opacity: 0.75, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 });
    const decalGeo = new THREE.CircleGeometry(0.035, 10);
    for (let i = 0; i < DECAL_MAX; i++) {
      const m = new THREE.Mesh(decalGeo, decalMat);
      m.visible = false;
      scene.add(m);
      this.decals.push(m);
    }
    for (let i = 0; i < 3; i++) {
      const l = new THREE.PointLight(0xffcc88, 0, 7);
      l.visible = false;
      scene.add(l);
      this.muzzleLights.push(l);
    }
    // [M17] 冲击波环池 ×8（全画质档）
    const ringGeo = new THREE.RingGeometry(0.86, 1, 40);
    for (let i = 0; i < 8; i++) {
      const m = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: 0xffc890, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
      m.rotation.x = -Math.PI / 2;
      m.visible = false;
      scene.add(m);
      this.rings.push(m);
    }
    // [M17] 爆炸闪光灯池 ×3（中/高档）
    for (let i = 0; i < 3; i++) {
      const l = new THREE.PointLight(0xffa050, 0, 22);
      l.visible = false;
      scene.add(l);
      this.blastLights.push(l);
    }
    // [M17] 地面焦痕池 ×12（中/高档，12s 渐隐）
    const scorchGeo = new THREE.CircleGeometry(1, 24);
    for (let i = 0; i < 12; i++) {
      const m = new THREE.Mesh(scorchGeo, new THREE.MeshBasicMaterial({ color: 0x14161a, transparent: true, opacity: 0, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 }));
      m.rotation.x = -Math.PI / 2;
      m.visible = false;
      scene.add(m);
      this.scorches.push(m);
    }
  }

  tracer(o: THREE.Vector3, end: THREE.Vector3): void {
    const from = o.clone();
    const delta = end.clone().sub(from);
    const dist = delta.length();
    if (dist < 0.5) return;
    const dir = delta.divideScalar(dist);
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(0.015, 0.015, 1.1),
      new THREE.MeshBasicMaterial({ color: 0xffe066, transparent: true, opacity: 0.95, blending: THREE.AdditiveBlending, depthWrite: false }),
    );
    mesh.position.copy(from);
    mesh.lookAt(end);
    this.scene.add(mesh);
    this.tracers.push({ mesh, from, dir, dist, at: 0 });
    if (this.tracers.length > 48) {
      const t = this.tracers.shift()!;
      this.scene.remove(t.mesh);
      t.mesh.geometry.dispose();
      (t.mesh.material as THREE.Material).dispose();
    }
  }

  impact(pos: THREE.Vector3, dir: THREE.Vector3, kind: ImpactKind): void {
    const density = this.quality === 'low' ? 0.4 : this.quality === 'medium' ? 0.7 : 1;
    if (kind === 'metal') {
      for (let i = 0; i < Math.round(6 * density); i++) {
        this.spawn(pos, {
          color: i === 0 ? 0xffe896 : 0xff9a2e,
          size: 0.03,
          vel: dir.clone().multiplyScalar(2 + Math.random() * 3).add(randVec(3.5)),
          life: 0.3,
          gravity: 9,
          additive: true,
        });
      }
    } else if (kind === 'wood') {
      for (let i = 0; i < Math.round(7 * density); i++) {
        this.spawn(pos, {
          color: 0xd09a4a,
          size: 0.045,
          vel: dir.clone().multiplyScalar(1.5 + Math.random() * 2).add(randVec(2.6)),
          life: 0.55,
          gravity: 12,
          spin: 8,
        });
      }
    } else {
      this.spawn(pos, { color: 0xd8dee6, size: 0.16, vel: new THREE.Vector3(0, 0.7, 0), life: 0.4, grow: 3 });
      for (let i = 0; i < Math.round(4 * density); i++) {
        this.spawn(pos, {
          color: 0xbac4ce,
          size: 0.04,
          vel: dir.clone().multiplyScalar(2 + Math.random() * 2).add(randVec(2.8)),
          life: 0.45,
          gravity: 13,
        });
      }
    }
  }

  decal(pos: THREE.Vector3, normal: THREE.Vector3): void {
    if (this.quality === 'low') return;
    const d = this.decals[this.decalIdx];
    this.decalIdx = (this.decalIdx + 1) % DECAL_MAX;
    d.visible = true;
    d.position.copy(pos).addScaledVector(normal, 0.012);
    d.lookAt(pos.clone().add(normal));
    d.rotation.z = Math.random() * Math.PI * 2;
  }

  blood(pos: THREE.Vector3): void {
    this.spawn(pos, { color: 0xd93a2e, size: 0.22, vel: new THREE.Vector3(0, 0.9, 0), life: 0.35, grow: 4 });
    for (let i = 0; i < 8; i++) {
      this.spawn(pos, {
        color: 0xb3261d,
        size: 0.035,
        vel: randVec(3.2).add(new THREE.Vector3(0, 1.4, 0)),
        life: 0.5,
        gravity: 14,
      });
    }
  }

  shell(pos: THREE.Vector3, fwd: THREE.Vector3, right: THREE.Vector3): void {
    if (this.quality === 'low') return;
    this.spawn(pos, {
      color: 0xffd60a,
      size: 0.022,
      vel: right.clone().multiplyScalar(1.6 + Math.random()).add(new THREE.Vector3(0, 2.2, 0)).add(fwd.clone().multiplyScalar(0.3)),
      life: 1.3,
      gravity: 16,
      bounce: 0.35,
      spin: 20,
    });
  }

  muzzle(pos: THREE.Vector3, dir: THREE.Vector3, scale = 1): void {
    this.spawn(pos, {
      color: 0xfff3b0,
      size: 0.42 * scale,
      vel: dir.clone().multiplyScalar(1.5),
      life: 0.06,
      grow: -3,
      additive: true,
    });
    this.spawn(pos, {
      color: 0x9fb0bc,
      size: 0.2 * scale,
      vel: dir.clone().multiplyScalar(0.8).add(new THREE.Vector3(0, 0.5, 0)),
      life: 0.5,
      grow: 2.4,
    });
    if (this.quality === 'high') {
      const l = this.muzzleLights[this.lightIdx];
      this.lightIdx = (this.lightIdx + 1) % this.muzzleLights.length;
      l.visible = true;
      l.intensity = 9 * scale;
      l.position.copy(pos);
      l.userData.until = performance.now() + 50;
    }
  }

  debris(pos: THREE.Vector3, color = 0xc98f42, count = 12): void {
    for (let i = 0; i < count; i++) {
      this.spawn(pos, {
        color,
        size: 0.16,
        vel: randVec(1).multiplyScalar(6).add(new THREE.Vector3(0, 2, 0)),
        life: 0.7,
        gravity: 14,
      });
    }
  }

  explosion(pos: THREE.Vector3, mag = 1): void {
    const density = this.quality === 'low' ? 0.4 : this.quality === 'medium' ? 0.7 : 1;
    // 白热核心 + 橙红火球（additive 双层，峰值 ~5.2×mag）
    this.spawn(pos, { color: 0xfff6d0, size: 1.5 * mag, vel: new THREE.Vector3(0, 1.2, 0), life: 0.5, grow: 5, additive: true });
    this.spawn(pos, { color: 0xff9a2e, size: 0.8 * mag, vel: new THREE.Vector3(0, 2.2, 0), life: 0.4, grow: 9, additive: true });
    // 冲击波环：贴地扩散至 7×mag
    const ring = this.rings[this.ringIdx];
    this.ringIdx = (this.ringIdx + 1) % this.rings.length;
    ring.visible = true;
    ring.position.set(pos.x, 0.15, pos.z);
    ring.scale.setScalar(0.6 * mag);
    ring.userData.born = this.nowMs;
    ring.userData.dur = 450;
    ring.userData.mag = mag;
    (ring.material as THREE.MeshBasicMaterial).opacity = 0.85;
    // 上升烟柱（蘑菇状烟云）
    for (let i = 0; i < Math.round(34 * density); i++) {
      this.spawn(pos, {
        color: Math.random() < 0.5 ? 0x4a525c : 0x6a7480,
        size: 0.5 + Math.random() * 0.4,
        vel: new THREE.Vector3((Math.random() - 0.5) * 1.6, 2.2 + Math.random() * 1.4, (Math.random() - 0.5) * 1.6),
        life: 2.6 + Math.random() * 1.6,
        gravity: -0.6,
        grow: 2.5,
      });
    }
    // 余烬火星
    for (let i = 0; i < Math.round(22 * density); i++) {
      this.spawn(pos, {
        color: Math.random() < 0.5 ? 0xffc356 : 0xff8432,
        size: 0.06 + Math.random() * 0.04,
        vel: randVec(1).multiplyScalar(5 + Math.random() * 4).add(new THREE.Vector3(0, 3, 0)),
        life: 0.9 + Math.random() * 0.7,
        gravity: 12,
        additive: true,
      });
    }
    this.debris(pos, 0xc98f42, Math.round(12 * mag));
    // 爆炸闪光灯（中/高档）
    if (this.quality !== 'low') {
      const l = this.blastLights[this.lightBlastIdx];
      this.lightBlastIdx = (this.lightBlastIdx + 1) % this.blastLights.length;
      l.visible = true;
      l.intensity = 55 * mag;
      l.position.set(pos.x, pos.y + 1.5, pos.z);
      l.userData.born = this.nowMs;
      l.userData.dur = 400;
      l.userData.peak = 55 * mag;
    }
    // 地面焦痕（中/高档）
    if (this.quality !== 'low') {
      const s = this.scorches[this.scorchIdx];
      this.scorchIdx = (this.scorchIdx + 1) % this.scorches.length;
      s.visible = true;
      s.position.set(pos.x, 0.02, pos.z);
      s.scale.setScalar(2.4 * mag);
      s.userData.born = this.nowMs;
      s.userData.dur = 12000;
      (s.material as THREE.MeshBasicMaterial).opacity = 0.82;
    }
  }

  /** [M15] 空袭/集束落点红烟标记（持续约 3s 的上升烟柱） */
  smokeMarker(pos: THREE.Vector3): void {
    for (let i = 0; i < 42; i++) {
      this.spawn(pos.clone().add(new THREE.Vector3((Math.random() - 0.5) * 1.6, Math.random() * 0.4, (Math.random() - 0.5) * 1.6)), {
        color: Math.random() < 0.7 ? 0xd8382f : 0x8a1f18,
        size: 0.45,
        vel: new THREE.Vector3((Math.random() - 0.5) * 0.8, 1.6 + Math.random() * 1.2, (Math.random() - 0.5) * 0.8),
        life: 2.6 + Math.random() * 0.8,
        gravity: -0.6,
        grow: 2.2,
      });
    }
  }

  /** [M17] 连杀落弹区余烬：durMs 内持续火苗 + 浓烟（池 4，FIFO 淘汰） */
  aftermath(x: number, z: number, durMs = 7000): void {
    if (this.emitters.length >= 4) this.emitters.shift();
    this.emitters.push({ x, z, until: this.nowMs + durMs, nextAt: this.nowMs });
  }

  /** [M17] 可见喷气机：沿航线 y=14 掠过（~1.56s，与 jetFlyby 音效对齐），双翼尖尾迹 */
  jet(x: number, z: number, yaw: number): void {
    this.disposeJet();
    const g = new THREE.Group();
    const body = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.6, 5.6), toonMat(0x2a2e34));
    g.add(body);
    const wing = new THREE.Mesh(new THREE.BoxGeometry(6.4, 0.12, 1.3), toonMat(0x33383f));
    wing.position.z = 0.6;
    g.add(wing);
    const tail = new THREE.Mesh(new THREE.BoxGeometry(2.2, 0.1, 0.8), toonMat(0x33383f));
    tail.position.z = 2.4;
    g.add(tail);
    const fin = new THREE.Mesh(new THREE.BoxGeometry(0.1, 1.1, 1), toonMat(0x33383f));
    fin.position.set(0, 0.6, 2.5);
    g.add(fin);
    g.rotation.y = yaw;
    this.scene.add(g);
    this.jetView = { x, z, yaw, born: this.nowMs, group: g, lastTrail: this.nowMs };
  }

  private disposeJet(): void {
    if (!this.jetView) return;
    this.scene.remove(this.jetView.group);
    for (const m of this.jetView.group.children) {
      const mesh = m as THREE.Mesh;
      mesh.geometry.dispose();
      (mesh.material as THREE.Material).dispose();
    }
    this.jetView = null;
  }

  private spawn(pos: THREE.Vector3, o: ParticleOpts): void {
    const pool = o.additive ? this.additivePool : this.alphaPool;
    const slot = pool.acquire();
    if (slot === null) return;
    this.live.push({
      slot,
      pool,
      vel: o.vel.clone(),
      born: this.nowMs,
      life: o.life,
      gravity: o.gravity ?? 0,
      grow: o.grow ?? 0,
      bounce: o.bounce ?? 0,
      spin: o.spin ?? 0,
      color: new THREE.Color(o.color),
      pos: pos.clone(),
      size: o.size,
      rotX: Math.random() * Math.PI,
      rotZ: Math.random() * Math.PI,
    });
  }

  nadeThrow(id: number, kind: 'frag' | 'flash', pos: THREE.Vector3): void {
    let v = this.nadeViews.get(id);
    if (!v) {
      const mesh = new THREE.Mesh(
        kind === 'frag' ? new THREE.SphereGeometry(0.07, 10, 8) : new THREE.CylinderGeometry(0.045, 0.045, 0.12, 10),
        toonMat(kind === 'frag' ? 0x3d5a4a : 0xffffff),
      );
      mesh.castShadow = false;
      this.scene.add(mesh);
      v = { mesh, target: pos.clone() };
      this.nadeViews.set(id, v);
    }
    v.mesh.position.copy(pos);
    v.target.copy(pos);
  }

  nadesSync(list: NadeSnap[]): void {
    const alive = new Set<number>();
    for (const n of list) {
      alive.add(n.i);
      const v = this.nadeViews.get(n.i);
      if (v) v.target.set(n.x, n.y, n.z);
      else this.nadeThrow(n.i, n.k, new THREE.Vector3(n.x, n.y, n.z));
    }
    for (const [id, v] of this.nadeViews) {
      if (alive.has(id)) continue;
      this.scene.remove(v.mesh);
      v.mesh.geometry.dispose();
      (v.mesh.material as THREE.Material).dispose();
      this.nadeViews.delete(id);
    }
  }

  update(dt: number, now: number = performance.now()): void {
    this.nowMs = now;
    for (const v of this.nadeViews.values()) {
      v.mesh.position.lerp(v.target, Math.min(1, 14 * dt));
      v.mesh.rotation.x += 6 * dt;
      v.mesh.rotation.z += 4 * dt;
    }
    for (const l of this.muzzleLights) {
      if (l.visible && now > l.userData.until) {
        l.visible = false;
        l.intensity = 0;
      }
    }
    // [M17] 冲击波环：扩散 + 淡出
    for (const r of this.rings) {
      if (!r.visible) continue;
      const k = (now - r.userData.born) / r.userData.dur;
      if (k >= 1) {
        r.visible = false;
        continue;
      }
      r.scale.setScalar((0.6 + 6.4 * k) * (r.userData.mag as number));
      (r.material as THREE.MeshBasicMaterial).opacity = 0.85 * (1 - k);
    }
    // [M17] 爆炸灯：二次方衰减
    for (const l of this.blastLights) {
      if (!l.visible) continue;
      const k = (now - l.userData.born) / l.userData.dur;
      if (k >= 1) {
        l.visible = false;
        l.intensity = 0;
        continue;
      }
      l.intensity = (l.userData.peak as number) * (1 - k) * (1 - k);
    }
    // [M17] 焦痕：12s 线性渐隐
    for (const s of this.scorches) {
      if (!s.visible) continue;
      const k = (now - s.userData.born) / s.userData.dur;
      if (k >= 1) {
        s.visible = false;
        continue;
      }
      (s.material as THREE.MeshBasicMaterial).opacity = 0.82 * (1 - k);
    }
    // [M17] 余烬发射器：每 120ms 火苗 + 浓烟
    for (let i = this.emitters.length - 1; i >= 0; i--) {
      const em = this.emitters[i];
      if (now >= em.until) {
        this.emitters.splice(i, 1);
        continue;
      }
      while (now >= em.nextAt) {
        const px = em.x + (Math.random() - 0.5) * 2.4;
        const pz = em.z + (Math.random() - 0.5) * 2.4;
        this.spawn(new THREE.Vector3(px, 0.3, pz), { color: 0xff9040, size: 0.25, vel: new THREE.Vector3(0, 1.4, 0), life: 0.5, additive: true });
        this.spawn(new THREE.Vector3(px, 0.5, pz), { color: 0x3c424a, size: 0.7, vel: new THREE.Vector3((Math.random() - 0.5) * 0.6, 1.8, (Math.random() - 0.5) * 0.6), life: 3, grow: 3 });
        em.nextAt += 120;
      }
    }
    // [M17] 喷气机：航迹推进 + 翼尖尾迹
    if (this.jetView) {
      const j = this.jetView;
      const t = (now - j.born) / 1000;
      if (t >= (JET_HALF_RUN * 2) / JET_SPEED) {
        this.disposeJet();
      } else {
        j.group.position.copy(jetPath(j.x, j.z, j.yaw, t));
        if (now - j.lastTrail >= 30) {
          j.lastTrail = now;
          const px = Math.cos(j.yaw);
          const pz = -Math.sin(j.yaw);
          for (const side of [-3.2, 3.2]) {
            this.spawn(new THREE.Vector3(j.group.position.x + px * side, JET_Y - 0.2, j.group.position.z + pz * side), { color: 0xdfe6ec, size: 0.5, vel: new THREE.Vector3(0, -0.2, 0), life: 1.8, grow: 2 });
          }
        }
      }
    }
    for (let i = this.tracers.length - 1; i >= 0; i--) {
      const t = this.tracers[i];
      t.at += TRACER_SPEED * dt;
      if (t.at >= t.dist) {
        this.scene.remove(t.mesh);
        t.mesh.geometry.dispose();
        (t.mesh.material as THREE.Material).dispose();
        this.tracers.splice(i, 1);
      } else {
        t.mesh.position.copy(t.from).addScaledVector(t.dir, t.at);
        const fade = Math.min(1, (t.dist - t.at) / 2 + 0.15);
        (t.mesh.material as THREE.MeshBasicMaterial).opacity = 0.95 * fade;
      }
    }
    for (let i = this.live.length - 1; i >= 0; i--) {
      const p = this.live[i];
      const age = (now - p.born) / 1000;
      const k = Math.max(0, 1 - age / p.life);
      if (k <= 0) {
        p.pool.release(p.slot);
        this.live.splice(i, 1);
        continue;
      }
      p.vel.y -= p.gravity * dt;
      p.pos.addScaledVector(p.vel, dt);
      if (p.pos.y < 0.03 && p.vel.y < 0) {
        p.pos.y = 0.03;
        if (p.bounce > 0 && Math.abs(p.vel.y) > 0.5) {
          p.vel.y = -p.vel.y * p.bounce;
          p.vel.x *= 0.6;
          p.vel.z *= 0.6;
        } else {
          p.vel.y = 0;
          p.vel.x *= 0.6;
          p.vel.z *= 0.6;
        }
      }
      if (p.spin) {
        p.rotX += p.spin * dt;
        p.rotZ += p.spin * 0.7 * dt;
      }
      p.pool.write(p.slot, p, k, now);
    }
    this.additivePool.flagUpdate();
    this.alphaPool.flagUpdate();
  }
}

export const JET_SPEED = 90;
const JET_Y = 14;
const JET_HALF_RUN = 70;

/** [M17] 喷气机航迹（纯函数）：tSec 秒时机身位置，从航线后方 70m 掠过目标至前方 70m */
export function jetPath(x: number, z: number, yaw: number, tSec: number): THREE.Vector3 {
  const dx = -Math.sin(yaw);
  const dz = -Math.cos(yaw);
  return new THREE.Vector3(x - dx * JET_HALF_RUN + dx * JET_SPEED * tSec, JET_Y, z - dz * JET_HALF_RUN + dz * JET_SPEED * tSec);
}

function randVec(scale: number): THREE.Vector3 {
  return new THREE.Vector3(Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5).multiplyScalar(2 * scale);
}
