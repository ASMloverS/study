import * as THREE from 'three';

const COUNT = 48;

export class SpeedLines {
  private lines: THREE.LineSegments;
  private pos: Float32Array;
  private seeds: { x: number; y: number; z: number }[] = [];

  constructor(scene: THREE.Scene) {
    this.pos = new Float32Array(COUNT * 6);
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(this.pos, 3));
    const mat = new THREE.LineBasicMaterial({
      color: 0xffffff,
      transparent: true,
      opacity: 0,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });
    this.lines = new THREE.LineSegments(geo, mat);
    this.lines.frustumCulled = false;
    scene.add(this.lines);
    for (let i = 0; i < COUNT; i++) {
      this.seeds.push({ x: 0, y: 0, z: 0 });
      this.respawn(i, true);
    }
  }

  private respawn(i: number, anywhere: boolean): void {
    const r = 14 + Math.random() * 26;
    const a = Math.random() * Math.PI * 2;
    this.seeds[i] = {
      x: Math.cos(a) * r,
      y: Math.abs(Math.sin(a)) * r * 0.55 + 1,
      z: anywhere ? (Math.random() - 0.5) * 220 : 90 + Math.random() * 60,
    };
  }

  update(speed: number, frameDt: number, camera: THREE.Camera): void {
    const mat = this.lines.material as THREE.LineBasicMaterial;
    mat.opacity = Math.max(0, Math.min(0.5, (speed - 30) / 120));
    if (mat.opacity <= 0.01) return;
    const len = Math.min(34, speed * 0.16);
    this.lines.position.copy(camera.position);
    this.lines.quaternion.copy(camera.quaternion);
    for (let i = 0; i < COUNT; i++) {
      const s = this.seeds[i];
      s.z -= speed * frameDt;
      if (s.z < -70) this.respawn(i, false);
      const o = i * 6;
      this.pos[o] = s.x;
      this.pos[o + 1] = s.y;
      this.pos[o + 2] = s.z;
      this.pos[o + 3] = s.x;
      this.pos[o + 4] = s.y;
      this.pos[o + 5] = s.z + len;
    }
    (this.lines.geometry.attributes.position as THREE.BufferAttribute).needsUpdate = true;
  }
}
