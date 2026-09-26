import * as THREE from 'three';

export class ChaseCamera {
  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.camera.position.x += (target.x - fx * 8.5 - this.camera.position.x) * k;
    this.camera.position.y += (target.y + 3.2 - this.camera.position.y) * k;
    this.camera.position.z += (target.z - fz * 8.5 - this.camera.position.z) * k;
    this.camera.lookAt(target.x + fx * 12, target.y + 1.2 + gradient * 6, target.z + fz * 12);
  }
}
