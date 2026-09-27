import * as THREE from 'three';

export class ChaseCamera {
  private t = 0;
  private basePos = new THREE.Vector3();

  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number, speed: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.basePos.x += (target.x - fx * 6 - this.basePos.x) * k;
    this.basePos.y += (target.y + 2.4 - this.basePos.y) * k;
    this.basePos.z += (target.z - fz * 6 - this.basePos.z) * k;
    this.t += frameDt;
    const amp = Math.min(0.22, Math.max(0, (speed - 8) / 70));
    const sx = amp * (Math.sin(this.t * 13.7) * 0.6 + Math.sin(this.t * 7.3 + 1.7) * 0.4);
    const sy = amp * (Math.sin(this.t * 11.3 + 0.5) * 0.6 + Math.sin(this.t * 17.1) * 0.4);
    this.camera.position.set(this.basePos.x + sx, this.basePos.y + sy, this.basePos.z);
    this.camera.lookAt(target.x + fx * 18, target.y + 1.2 + gradient * 6, target.z + fz * 18);
    this.camera.fov = 65 + Math.min(20, speed * 1.5);
    this.camera.updateProjectionMatrix();
  }
}
