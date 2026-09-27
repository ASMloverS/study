import * as THREE from 'three';

export class ChaseCamera {
  constructor(private camera: THREE.PerspectiveCamera) {}

  update(target: THREE.Vector3, heading: number, gradient: number, frameDt: number, speed: number): void {
    const fx = Math.cos(heading);
    const fz = Math.sin(heading);
    const k = 1 - Math.exp(-6 * frameDt);
    this.camera.position.x += (target.x - fx * 6 - this.camera.position.x) * k;
    this.camera.position.y += (target.y + 2.4 - this.camera.position.y) * k;
    this.camera.position.z += (target.z - fz * 6 - this.camera.position.z) * k;
    this.camera.lookAt(target.x + fx * 18, target.y + 1.2 + gradient * 6, target.z + fz * 18);
    this.camera.fov = 65 + Math.min(23, speed / 7);
    this.camera.updateProjectionMatrix();
  }
}
