import * as THREE from 'three';
import type { Track } from '../sim/track';

export function baseHills(x: number, z: number): number {
  return 6 * Math.sin(x / 430) * Math.cos(z / 380) + 3 * Math.sin(x / 150 + z / 170);
}

export function smoothstep(v: number, e0: number, e1: number): number {
  const t = Math.min(1, Math.max(0, (v - e0) / (e1 - e0)));
  return t * t * (3 - 2 * t);
}

export function terrainHeight(track: Track, x: number, z: number): number {
  const near = track.nearest(x, z);
  return near.y + baseHills(x, z) * smoothstep(near.dist, 12, 80);
}

export function buildTerrain(track: Track): THREE.Mesh {
  const pts = track.densePoints();
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const [x, , z] of pts) {
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
  }
  const margin = 300;
  const geo = new THREE.PlaneGeometry(maxX - minX + margin * 2, maxZ - minZ + margin * 2, 150, 150);
  geo.rotateX(-Math.PI / 2);
  const pos = geo.attributes.position as THREE.BufferAttribute;
  const cx = (minX + maxX) / 2;
  const cz = (minZ + maxZ) / 2;
  for (let i = 0; i < pos.count; i++) {
    pos.setY(i, terrainHeight(track, pos.getX(i) + cx, pos.getZ(i) + cz) - 0.15);
  }
  geo.computeVertexNormals();
  return new THREE.Mesh(geo, new THREE.MeshLambertMaterial({ color: 0x6fae57 }));
}
