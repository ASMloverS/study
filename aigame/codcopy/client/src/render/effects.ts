import * as THREE from 'three';

interface Tracer {
  line: THREE.Line;
  born: number;
}

interface Particle {
  mesh: THREE.Mesh;
  vel: THREE.Vector3;
  born: number;
  life: number;
  gravity: number;
  grow: number;
}

interface ParticleOpts {
  color: number;
  size: number;
  vel: THREE.Vector3;
  life: number;
  gravity?: number;
  grow?: number;
  additive?: boolean;
}

export class Effects {
  private tracers: Tracer[] = [];
  private particles: Particle[] = [];
  private readonly scene: THREE.Scene;

  constructor(scene: THREE.Scene) {
    this.scene = scene;
  }

  tracer(o: THREE.Vector3, end: THREE.Vector3): void {
    const geo = new THREE.BufferGeometry().setFromPoints([o.clone(), end.clone()]);
    const mat = new THREE.LineBasicMaterial({ color: 0xffe9a0, transparent: true, opacity: 0.9 });
    const line = new THREE.Line(geo, mat);
    this.scene.add(line);
    this.tracers.push({ line, born: performance.now() });
    if (this.tracers.length > 48) this.removeTracer(this.tracers.shift()!);
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
    });
  }

  update(): void {
    const now = performance.now();
    const dt = 1 / 60;
    for (let i = this.tracers.length - 1; i >= 0; i--) {
      const t = this.tracers[i];
      const age = (now - t.born) / 90;
      if (age >= 1) {
        this.removeTracer(t);
        this.tracers.splice(i, 1);
      } else {
        (t.line.material as THREE.LineBasicMaterial).opacity = 0.9 * (1 - age);
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
      if (p.mesh.position.y < 0.03 && p.vel.y < 0) {
        p.mesh.position.y = 0.03;
        p.vel.y = 0;
        p.vel.x *= 0.6;
        p.vel.z *= 0.6;
      }
      const k = 1 + p.grow * dt;
      p.mesh.scale.multiplyScalar(k);
      (p.mesh.material as THREE.MeshBasicMaterial).opacity = 1 - age / p.life;
    }
  }

  private removeTracer(t: Tracer): void {
    this.scene.remove(t.line);
    t.line.geometry.dispose();
    (t.line.material as THREE.Material).dispose();
  }
}
