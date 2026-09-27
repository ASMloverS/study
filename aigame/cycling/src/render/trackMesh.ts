import * as THREE from 'three';
import { ITEM_BOXES, RACE } from '../sim/params';
import { mulberry32 } from '../sim/rng';
import { terrainHeight } from './terrain';
import type { Track } from '../sim/track';

function ribbon(track: Track, left: number, right: number, color: number, dy: number): THREE.Mesh {
  const pts = track.densePoints();
  const n = pts.length - 1;
  const positions = new Float32Array((n + 1) * 6);
  const indices = new Uint32Array(n * 6);
  for (let i = 0; i <= n; i++) {
    const [x, y, z] = pts[i];
    const [x2, , z2] = pts[i === n ? i - 1 : i + 1];
    let dx = x2 - x, dz = z2 - z;
    if (i === n) {
      dx = -dx;
      dz = -dz;
    }
    const len = Math.hypot(dx, dz) || 1;
    dx /= len; dz /= len;
    const nx = -dz, nz = dx;
    const o = i * 6;
    positions[o] = x + nx * left; positions[o + 1] = y + dy; positions[o + 2] = z + nz * left;
    positions[o + 3] = x + nx * right; positions[o + 4] = y + dy; positions[o + 5] = z + nz * right;
  }
  for (let i = 0; i < n; i++) {
    const a = i * 2, b = a + 1, c = a + 2, d = a + 3;
    indices.set([a, c, b, b, c, d], i * 6);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geo.setIndex(new THREE.BufferAttribute(indices, 1));
  geo.computeVertexNormals();
  const mesh = new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color, side: THREE.DoubleSide }));
  mesh.frustumCulled = false;
  return mesh;
}

function dashes(track: Track, halfW: number, segLen: number, gap: number, color: number, dy: number): THREE.Mesh {
  const verts: number[] = [];
  for (let d = 0; d < track.length; d += segLen + gap) {
    for (const s of [track.sampleAt(d), track.sampleAt(Math.min(d + segLen, track.length - 0.01))]) {
      const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
      verts.push(s.x + nx * halfW, s.y + dy, s.z + nz * halfW, s.x - nx * halfW, s.y + dy, s.z - nz * halfW);
    }
  }
  const indices: number[] = [];
  for (let q = 0; q < verts.length / 12; q++) {
    const a = q * 4;
    indices.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(verts), 3));
  geo.setIndex(indices);
  return new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide }));
}

function gate(track: Track, dist: number, color: number): THREE.Group {
  const s = track.sampleAt(dist);
  const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
  const g = new THREE.Group();
  const postMat = new THREE.MeshLambertMaterial({ color: 0x30343c });
  for (const side of [-1, 1]) {
    const post = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.25, 6, 8), postMat);
    post.position.set(s.x + nx * side * 4.5, s.y + 3, s.z + nz * side * 4.5);
    g.add(post);
  }
  const top = new THREE.Mesh(new THREE.BoxGeometry(9.6, 0.8, 0.5), new THREE.MeshLambertMaterial({ color }));
  top.position.set(s.x, s.y + 6.2, s.z);
  top.rotation.y = -s.heading + Math.PI / 2;
  g.add(top);
  return g;
}

function trees(track: Track): THREE.Group {
  const rng = mulberry32(1234);
  const count = 320;
  const trunks = new THREE.InstancedMesh(
    new THREE.CylinderGeometry(0.35, 0.5, 2.4, 6),
    new THREE.MeshLambertMaterial({ color: 0x7a5230 }),
    count,
  );
  const crowns = new THREE.InstancedMesh(
    new THREE.ConeGeometry(2.2, 5.5, 7),
    new THREE.MeshLambertMaterial({ color: 0x3d7a33, flatShading: true }),
    count,
  );
  const m = new THREE.Matrix4();
  let placed = 0;
  while (placed < count) {
    const s = track.sampleAt(rng() * track.length);
    const side = rng() < 0.5 ? -1 : 1;
    const off = 10 + rng() * 40;
    const nx = -Math.sin(s.heading), nz = Math.cos(s.heading);
    const x = s.x + nx * side * off;
    const z = s.z + nz * side * off;
    const sc = 0.7 + rng() * 0.9;
    m.makeScale(sc, sc, sc);
    m.setPosition(x, terrainHeight(track, x, z) + 1.2 * sc, z);
    trunks.setMatrixAt(placed, m);
    m.setPosition(x, terrainHeight(track, x, z) + 5.15 * sc, z);
    crowns.setMatrixAt(placed, m);
    placed++;
  }
  const g = new THREE.Group();
  g.add(trunks, crowns);
  return g;
}

export function buildTrackMesh(track: Track): THREE.Group {
  const half = RACE.trackWidth / 2;
  const group = new THREE.Group();
  group.add(ribbon(track, half + 0.3, -half - 0.3, 0x4a4a50, -0.03));
  group.add(ribbon(track, half, -half, 0x2b2b30, 0));
  group.add(ribbon(track, half - 0.05, half - 0.3, 0xe8e8e8, 0.008));
  group.add(ribbon(track, -half + 0.3, -half + 0.05, 0xe8e8e8, 0.008));
  group.add(dashes(track, 0.09, 3, 8, 0xffffff, 0.012));
  group.add(gate(track, 0, 0xe0533d));
  group.add(trees(track));
  return group;
}

export function buildItemBoxes(): { group: THREE.Group; meshes: THREE.Mesh[] } {
  const group = new THREE.Group();
  const meshes: THREE.Mesh[] = [];
  ITEM_BOXES.forEach((box, i) => {
    const cluster = Math.floor(i / 3);
    const mat = new THREE.MeshPhongMaterial({
      color: new THREE.Color().setHSL((cluster % 29) / 29, 0.85, 0.6),
      transparent: true,
      opacity: 0.55,
    });
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(0.9, 0.9, 0.9), mat);
    mesh.userData.d = box.d;
    mesh.userData.lat = box.lat;
    group.add(mesh);
    meshes.push(mesh);
  });
  return { group, meshes };
}
