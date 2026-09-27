import * as THREE from 'three';
import type { Track } from '../sim/track';

export function baseHills(x: number, z: number): number {
  return 6 * Math.sin(x / 430) * Math.cos(z / 380) + 3 * Math.sin(x / 150 + z / 170);
}

export function smoothstep(v: number, e0: number, e1: number): number {
  const t = Math.min(1, Math.max(0, (v - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

const LATERAL = [-400, -250, -150, -80, -40, -15, 0, 15, 40, 80, 150, 250, 400];

export function buildTerrain(track: Track): THREE.Mesh {
  const pts = track.densePoints();
  const sections: [number, number, number, number, number][] = [];
  for (let i = 0; i < pts.length - 1; i += 3) {
    const [x, y, z] = pts[i];
    const dx = pts[i + 1][0] - x;
    const dz = pts[i + 1][2] - z;
    const len = Math.hypot(dx, dz) || 1;
    sections.push([x, y, z, -dz / len, dx / len]);
  }
  const rows = sections.length;
  const cols = LATERAL.length;
  const pos = new Float32Array(rows * cols * 3);
  for (let r = 0; r < rows; r++) {
    const [x, y, z, lx, lz] = sections[r];
    for (let c = 0; c < cols; c++) {
      const off = LATERAL[c];
      const px = x + lx * off;
      const pz = z + lz * off;
      const o = (r * cols + c) * 3;
      pos[o] = px;
      pos[o + 1] = y + baseHills(px, pz) * smoothstep(off, 12, 80) - 0.15;
      pos[o + 2] = pz;
    }
  }
  const idx: number[] = [];
  for (let r = 0; r < rows - 1; r++) {
    for (let c = 0; c < cols - 1; c++) {
      const a = r * cols + c;
      idx.push(a, a + 1, a + cols, a + 1, a + cols + 1, a + cols);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  geo.setIndex(idx);
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x6fae57 }));
}
