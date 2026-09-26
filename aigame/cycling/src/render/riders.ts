import * as THREE from 'three';
import type { TrackSample } from '../sim/track';

export function buildRiderMesh(jersey: number): THREE.Group {
  const group = new THREE.Group();
  const frameMat = new THREE.MeshLambertMaterial({ color: 0x22262e });
  const bodyMat = new THREE.MeshLambertMaterial({ color: jersey, flatShading: true });
  const wheelGeo = new THREE.CylinderGeometry(0.34, 0.34, 0.08, 10);
  const wheelMat = new THREE.MeshLambertMaterial({ color: 0x111111 });
  for (const z of [0.52, -0.52]) {
    const wheel = new THREE.Mesh(wheelGeo, wheelMat);
    wheel.rotation.z = Math.PI / 2;
    wheel.position.set(0, 0.34, z);
    group.add(wheel);
  }
  const tube = (len: number, x: number, y: number, z: number, rx: number) => {
    const mesh = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, len, 6), frameMat);
    mesh.rotation.set(rx, 0, 0);
    mesh.position.set(x, y, z);
    group.add(mesh);
  };
  tube(1.04, 0, 0.62, 0, Math.PI / 2);
  tube(0.7, 0, 0.52, 0.45, 0.5);
  tube(0.7, 0, 0.52, -0.45, -0.5);
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.5, 0.56), bodyMat);
  torso.position.set(0, 1.18, -0.08);
  torso.rotation.x = 0.7;
  group.add(torso);
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.14, 8, 8), new THREE.MeshLambertMaterial({ color: 0xf2c89a }));
  head.position.set(0, 1.42, 0.12);
  group.add(head);
  for (const [x, z, rx] of [[0.18, 0.35, -0.9], [-0.18, 0.35, -0.9], [0.18, -0.2, 0.9], [-0.18, -0.2, 0.9]] as const) {
    const limb = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.75, 6), bodyMat);
    limb.position.set(x, 0.78, z);
    limb.rotation.x = rx;
    group.add(limb);
  }
  return group;
}

export function placeRider(mesh: THREE.Object3D, s: TrackSample, lateral: number): void {
  const nx = -Math.sin(s.heading);
  const nz = Math.cos(s.heading);
  mesh.position.set(s.x + nx * lateral, s.y, s.z + nz * lateral);
  mesh.rotation.y = Math.PI / 2 - s.heading;
  mesh.rotation.x = -Math.atan(s.gradient) * 0.8;
}
