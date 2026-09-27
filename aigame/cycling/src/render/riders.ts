import * as THREE from 'three';
import type { TrackSample } from '../sim/track';

export function buildRiderMesh(jersey: number, isPlayer: boolean): THREE.Group {
  const group = new THREE.Group();
  const frameMat = new THREE.MeshLambertMaterial({ color: isPlayer ? 0xff2a2a : 0x22262e });
  const bodyMat = new THREE.MeshLambertMaterial({ color: jersey, flatShading: true });
  const wheelGeo = new THREE.CylinderGeometry(0.34, 0.34, 0.08, 12);
  const wheelMat = new THREE.MeshLambertMaterial({ color: 0x111111 });
  for (const z of [0.52, -0.52]) {
    const wheel = new THREE.Mesh(wheelGeo, wheelMat);
    wheel.rotation.z = Math.PI / 2;
    wheel.position.set(0, 0.34, z);
    group.add(wheel);
    for (let s = 0; s < 8; s++) {
      const spoke = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 0.66, 4), frameMat);
      spoke.rotation.set((s / 8) * Math.PI, 0, 0);
      spoke.position.set(0, 0.34, z);
      group.add(spoke);
    }
    if (isPlayer) {
      const shell = new THREE.Mesh(
        new THREE.CylinderGeometry(0.4, 0.4, 0.14, 12),
        new THREE.MeshBasicMaterial({ color: 0xff2a2a, side: THREE.BackSide }),
      );
      shell.rotation.z = Math.PI / 2;
      shell.position.set(0, 0.34, z);
      group.add(shell);
    }
  }
  const tube = (len: number, r: number, x: number, y: number, z: number, rx: number, rz = 0) => {
    const mesh = new THREE.Mesh(new THREE.CylinderGeometry(r, r, len, 6), frameMat);
    mesh.rotation.set(rx, 0, rz);
    mesh.position.set(x, y, z);
    group.add(mesh);
  };
  tube(1.04, 0.04, 0, 0.62, 0, Math.PI / 2);
  tube(0.7, 0.035, 0, 0.52, 0.45, 0.5);
  tube(0.7, 0.035, 0, 0.52, -0.45, -0.5);
  tube(0.5, 0.03, 0, 0.85, 0.18, 0.35);
  tube(0.42, 0.03, 0, 1.06, 0.3, 1.1);
  tube(0.36, 0.025, 0.26, 1.12, 0.3, 0, Math.PI / 2);
  tube(0.36, 0.025, -0.26, 1.12, 0.3, 0, Math.PI / 2);
  const torso = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.5, 0.56), bodyMat);
  torso.position.set(0, 1.12, -0.02);
  torso.rotation.x = 0.95;
  group.add(torso);
  if (isPlayer) {
    const outline = new THREE.Mesh(
      new THREE.BoxGeometry(0.4, 0.55, 0.61),
      new THREE.MeshBasicMaterial({ color: 0xff2a2a, side: THREE.BackSide }),
    );
    outline.position.copy(torso.position);
    outline.rotation.copy(torso.rotation);
    group.add(outline);
  }
  const head = new THREE.Mesh(new THREE.SphereGeometry(0.14, 10, 10), new THREE.MeshLambertMaterial({ color: 0xf2c89a }));
  head.position.set(0, 1.32, 0.22);
  group.add(head);
  for (const [x, z, rx] of [[0.18, 0.35, -1.05], [-0.18, 0.35, -1.05], [0.18, -0.2, 0.95], [-0.18, -0.2, 0.95]] as const) {
    const limb = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.75, 6), bodyMat);
    limb.position.set(x, 0.78, z);
    limb.rotation.x = rx;
    group.add(limb);
  }
  if (isPlayer) group.scale.setScalar(1.15);
  return group;
}

export function placeRider(mesh: THREE.Object3D, s: TrackSample, lateral: number): void {
  const nx = -Math.sin(s.heading);
  const nz = Math.cos(s.heading);
  mesh.position.set(s.x + nx * lateral, s.y, s.z + nz * lateral);
  mesh.rotation.order = 'YXZ';
  mesh.rotation.y = Math.PI / 2 - s.heading;
  mesh.rotation.x = -Math.atan(s.gradient) * 0.8;
}
