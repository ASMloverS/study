import * as THREE from 'three';
import { MAPS, coverToAABB, type AABB } from 'shared';

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

interface Particle {
  mesh: THREE.Mesh;
  vel: THREE.Vector3;
  born: number;
  life: number;
  gravity: number;
  grow: number;
  bounce: number;
  spin: number;
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
  private particles: Particle[] = [];
  private readonly scene: THREE.Scene;
  private readonly decals: THREE.Mesh[] = [];
  private decalIdx = 0;
  private readonly muzzleLights: THREE.PointLight[] = [];
  private lightIdx = 0;

  constructor(scene: THREE.Scene) {
    this.scene = scene;
    const decalMat = new THREE.MeshBasicMaterial({ color: 0x14140f, transparent: true, opacity: 0.75, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2 });
    const decalGeo = new THREE.CircleGeometry(0.035, 8);
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
  }

  tracer(o: THREE.Vector3, end: THREE.Vector3): void {
    const from = o.clone();
    const delta = end.clone().sub(from);
    const dist = delta.length();
    if (dist < 0.5) return;
    const dir = delta.divideScalar(dist);
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(0.015, 0.015, 1.1),
      new THREE.MeshBasicMaterial({ color: 0xffe9a0, transparent: true, opacity: 0.95, blending: THREE.AdditiveBlending, depthWrite: false }),
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
          color: i === 0 ? 0xffd866 : 0xb8b8b8,
          size: 0.035,
          vel: new THREE.Vector3(dir.x * 2 + (Math.random() - 0.5) * 3, Math.random() * 2.5 + 1, dir.z * 2 + (Math.random() - 0.5) * 3),
          life: 0.4,
          gravity: 12,
          additive: true,
        });
      }
    } else if (kind === 'wood') {
      for (let i = 0; i < Math.round(7 * density); i++) {
        this.spawn(pos, {
          color: 0x9a7448,
          size: 0.06,
          vel: new THREE.Vector3(dir.x * 1.5 + (Math.random() - 0.5) * 2.5, Math.random() * 2 + 0.8, dir.z * 1.5 + (Math.random() - 0.5) * 2.5),
          life: 0.6,
          gravity: 13,
          spin: 8,
        });
      }
    } else {
      this.spawn(pos, {
        color: 0x9a9a92,
        size: 0.14,
        vel: new THREE.Vector3(dir.x, 0.8, dir.z),
        life: 0.5,
        grow: 3,
      });
      for (let i = 0; i < Math.round(4 * density); i++) {
        this.spawn(pos, {
          color: 0x7d7d75,
          size: 0.045,
          vel: new THREE.Vector3((Math.random() - 0.5) * 2.5, Math.random() * 1.8 + 0.5, (Math.random() - 0.5) * 2.5),
          life: 0.45,
          gravity: 10,
        });
      }
    }
    if (this.quality !== 'low') this.addDecal(pos, dir);
  }

  private addDecal(pos: THREE.Vector3, dir: THREE.Vector3): void {
    const m = this.decals[this.decalIdx];
    this.decalIdx = (this.decalIdx + 1) % DECAL_MAX;
    m.visible = true;
    m.position.copy(pos).addScaledVector(dir, -0.015);
    m.lookAt(pos.clone().sub(dir));
    m.rotateZ(Math.random() * Math.PI);
    m.scale.setScalar(0.7 + Math.random() * 0.6);
  }

  blood(pos: THREE.Vector3): void {
    const density = this.quality === 'low' ? 0.4 : 1;
    this.spawn(pos, {
      color: 0x7a1212,
      size: 0.16,
      vel: new THREE.Vector3(0, 0.6, 0),
      life: 0.35,
      grow: 4,
    });
    for (let i = 0; i < Math.round(8 * density); i++) {
      this.spawn(pos, {
        color: 0x8a1010,
        size: 0.05,
        vel: new THREE.Vector3((Math.random() - 0.5) * 3.5, Math.random() * 2.2, (Math.random() - 0.5) * 3.5),
        life: 0.4,
        gravity: 14,
      });
    }
  }

  shell(pos: THREE.Vector3, forward: THREE.Vector3, right: THREE.Vector3): void {
    if (this.quality === 'low') return;
    this.spawn(pos, {
      color: 0xc9a23a,
      size: 0.022,
      vel: right.clone().multiplyScalar(1.6 + Math.random()).add(new THREE.Vector3(0, 2.2 + Math.random(), 0)).addScaledVector(forward, 0.4),
      life: 1.3,
      gravity: 12,
      bounce: 0.35,
      spin: 20,
    });
  }

  muzzle(pos: THREE.Vector3, dir: THREE.Vector3, scale = 1): void {
    const star = new THREE.Mesh(
      new THREE.PlaneGeometry(0.5 * scale, 0.5 * scale),
      new THREE.MeshBasicMaterial({ color: 0xffdd88, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false }),
    );
    star.position.copy(pos);
    star.lookAt(pos.clone().add(dir));
    star.rotateZ(Math.random() * Math.PI);
    this.scene.add(star);
    this.particles.push({ mesh: star, vel: new THREE.Vector3(), born: performance.now(), life: 0.06, gravity: 0, grow: -3, bounce: 0, spin: 0 });
    if (this.quality === 'high') {
      const l = this.muzzleLights[this.lightIdx];
      this.lightIdx = (this.lightIdx + 1) % this.muzzleLights.length;
      l.visible = true;
      l.intensity = 9;
      l.position.copy(pos);
      l.userData.until = performance.now() + 50;
    }
  }

  spark(pos: THREE.Vector3): void {
    for (let i = 0; i < 4; i++) {
      this.spawn(pos, {
        color: i === 0 ? 0xffd866 : 0xb8b8b8,
        size: 0.05,
        vel: new THREE.Vector3(Math.random() - 0.5, Math.random() * 0.8, Math.random() - 0.5).multiplyScalar(4),
        life: 0.35,
        gravity: 12,
      });
    }
  }

  debris(pos: THREE.Vector3, color = 0x8f6b3f): void {
    for (let i = 0; i < 12; i++) {
      this.spawn(pos, {
        color,
        size: 0.16,
        vel: new THREE.Vector3(Math.random() - 0.5, Math.random() * 1.2, Math.random() - 0.5).multiplyScalar(6),
        life: 0.7,
        gravity: 14,
      });
    }
  }

  explosion(pos: THREE.Vector3): void {
    this.spawn(pos, { color: 0xffaa33, size: 0.8, vel: new THREE.Vector3(0, 1.5, 0), life: 0.35, grow: 9, additive: true });
    this.spawn(pos, { color: 0xffe08a, size: 0.4, vel: new THREE.Vector3(0, 2.5, 0), life: 0.25, grow: 12, additive: true });
    for (let i = 0; i < 10; i++) {
      this.spawn(pos, {
        color: 0x3a3a3a,
        size: 0.35,
        vel: new THREE.Vector3(Math.random() - 0.5, Math.random() * 0.9 + 0.4, Math.random() - 0.5).multiplyScalar(3.5),
        life: 1.1,
        gravity: -1,
        grow: 2,
      });
    }
    this.debris(pos, 0x6a5a30);
  }

  private spawn(pos: THREE.Vector3, o: ParticleOpts): void {
    const mesh = new THREE.Mesh(
      new THREE.BoxGeometry(o.size, o.size, o.size),
      new THREE.MeshBasicMaterial({
        color: o.color,
        transparent: true,
        depthWrite: false,
        ...(o.additive ? { blending: THREE.AdditiveBlending } : {}),
      }),
    );
    mesh.position.copy(pos);
    this.scene.add(mesh);
    this.particles.push({
      mesh,
      vel: o.vel.clone(),
      born: performance.now(),
      life: o.life,
      gravity: o.gravity ?? 0,
      grow: o.grow ?? 0,
      bounce: o.bounce ?? 0,
      spin: o.spin ?? 0,
    });
  }

  update(dt: number): void {
    const now = performance.now();
    for (const l of this.muzzleLights) {
      if (l.visible && now > l.userData.until) {
        l.visible = false;
        l.intensity = 0;
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
    for (let i = this.particles.length - 1; i >= 0; i--) {
      const p = this.particles[i];
      const age = (now - p.born) / 1000;
      if (age >= p.life) {
        this.scene.remove(p.mesh);
        p.mesh.geometry.dispose();
        (p.mesh.material as THREE.Material).dispose();
        this.particles.splice(i, 1);
        continue;
      }
      p.vel.y -= p.gravity * dt;
      p.mesh.position.addScaledVector(p.vel, dt);
      if (p.spin) {
        p.mesh.rotation.x += p.spin * dt;
        p.mesh.rotation.z += p.spin * 0.7 * dt;
      }
      if (p.mesh.position.y < 0.03 && p.vel.y < 0) {
        p.mesh.position.y = 0.03;
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
      const k = 1 + p.grow * dt;
      if (k > 0) p.mesh.scale.multiplyScalar(k);
      (p.mesh.material as THREE.MeshBasicMaterial).opacity = 1 - age / p.life;
    }
  }
}
