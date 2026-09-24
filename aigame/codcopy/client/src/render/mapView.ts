import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import type { MapDef } from 'shared';

const COLORS: Record<string, number> = {
  wall: 0x8b9096,
  lowwall: 0x70757c,
  container: 0xc06a38,
  crate: 0x8f6b3f,
  barrel: 0xc9a23a,
};

const DOOR_COLOR = 0xd8b24a;
const LIFT_TRAVEL = 2;

function coverMaterial(color: number, type: string): THREE.MeshStandardMaterial {
  const metal = type === 'container' || type === 'barrel';
  return new THREE.MeshStandardMaterial({
    color,
    metalness: metal ? 0.55 : 0.05,
    roughness: type === 'wall' || type === 'lowwall' ? 0.92 : metal ? 0.48 : 0.85,
  });
}

export class MapView {
  readonly group = new THREE.Group();
  private readonly coverMeshes = new Map<number, THREE.Mesh>();
  private readonly dynEntries: {
    index: number;
    kind: 'door' | 'lift';
    mesh: THREE.Mesh;
    base: [number, number, number];
    size: [number, number, number];
  }[] = [];

  constructor(scene: THREE.Scene, map: MapDef) {
    const byColor = new Map<number, THREE.BufferGeometry[]>();
    map.covers.forEach((c, i) => {
      if (c.dynamic) {
        const mesh = new THREE.Mesh(
          new THREE.BoxGeometry(c.size[0], c.size[1], c.size[2]),
          coverMaterial(c.dynamic === 'door' ? DOOR_COLOR : COLORS[c.type], c.type),
        );
        mesh.position.set(c.pos[0], c.pos[1], c.pos[2]);
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        this.group.add(mesh);
        this.coverMeshes.set(i, mesh);
        this.dynEntries.push({ index: i, kind: c.dynamic, mesh, base: [...c.pos] as [number, number, number], size: c.size });
        return;
      }
      if (c.destructible) {
        const mesh = new THREE.Mesh(
          new THREE.BoxGeometry(c.size[0], c.size[1], c.size[2]),
          coverMaterial(COLORS[c.type], c.type),
        );
        mesh.position.set(c.pos[0], c.pos[1], c.pos[2]);
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        this.group.add(mesh);
        this.coverMeshes.set(i, mesh);
        return;
      }
      const color = c.type === 'container' && i % 2 === 1 ? 0x3f6f8f : COLORS[c.type];
      const geo = new THREE.BoxGeometry(c.size[0], c.size[1], c.size[2]);
      geo.translate(c.pos[0], c.pos[1], c.pos[2]);
      const list = byColor.get(color) ?? [];
      list.push(geo);
      byColor.set(color, list);
    });
    for (const [color, geos] of byColor) {
      const merged = mergeGeometries(geos);
      if (!merged) continue;
      const mesh = new THREE.Mesh(merged, coverMaterial(color, 'wall'));
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      this.group.add(mesh);
    }
    scene.add(this.group);
  }

  breakCover(index: number): void {
    const mesh = this.coverMeshes.get(index);
    if (!mesh) return;
    this.group.remove(mesh);
    mesh.geometry.dispose();
    (mesh.material as THREE.Material).dispose();
    this.coverMeshes.delete(index);
  }

  applySnapshot(destroyed: number[], values: number[]): void {
    for (const i of destroyed) this.breakCover(i);
    this.dynEntries.forEach((e, k) => {
      const v = values[k] ?? 0;
      if (e.kind === 'door') e.mesh.position.x = e.base[0] + v * (e.size[0] / 2);
      else e.mesh.position.y = e.base[1] + v * LIFT_TRAVEL;
    });
  }

  dispose(): void {
    this.group.traverse((o) => {
      if (o instanceof THREE.Mesh) {
        o.geometry.dispose();
        const m = o.material;
        if (Array.isArray(m)) m.forEach((x) => x.dispose());
        else m.dispose();
      }
    });
    this.group.removeFromParent();
    this.coverMeshes.clear();
    this.dynEntries.length = 0;
  }
}

export function dynIndicesOf(map: MapDef): number[] {
  return map.covers.map((c, i) => (c.dynamic ? i : -1)).filter((i) => i >= 0);
}
