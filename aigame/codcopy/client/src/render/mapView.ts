import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import type { MapDef } from 'shared';
import { PALETTE, toonMat } from './palette';

/** 掩体渲染：[M13] 工业波普纯色 toon 材质合批、圆柱油桶、装饰道具层（零贴图）。 */

const LIFT_TRAVEL = 2;

export class MapView {
  readonly group = new THREE.Group();
  private readonly coverMeshes = new Map<number, THREE.Object3D>();
  private readonly ownMaterials: THREE.Material[] = [];
  private readonly dynEntries: {
    index: number;
    kind: 'door' | 'lift';
    mesh: THREE.Mesh;
    base: [number, number, number];
    size: [number, number, number];
  }[] = [];

  constructor(scene: THREE.Scene, map: MapDef) {
    const wallMat = toonMat(PALETTE.wall);
    const lowMat = toonMat(PALETTE.lowWall);
    const contOrangeMat = toonMat(PALETTE.containerOrange);
    const contBlueMat = toonMat(PALETTE.containerBlue);
    const crateMat = toonMat(PALETTE.crate);
    const barrelMat = toonMat(PALETTE.barrel);
    const doorMat = toonMat(PALETTE.band);
    const liftMat = toonMat(PALETTE.platform);
    const metalMat = toonMat(PALETTE.metal);
    this.ownMaterials.push(wallMat, lowMat, contOrangeMat, contBlueMat, crateMat, barrelMat, doorMat, liftMat, metalMat);

    const batches = new Map<THREE.Material, THREE.BufferGeometry[]>();

    map.covers.forEach((c, i) => {
      if (c.dynamic) {
        const mesh = new THREE.Mesh(
          new THREE.BoxGeometry(c.size[0], c.size[1], c.size[2]),
          c.dynamic === 'door' ? doorMat : liftMat,
        );
        mesh.position.set(c.pos[0], c.pos[1], c.pos[2]);
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        this.group.add(mesh);
        this.coverMeshes.set(i, mesh);
        this.dynEntries.push({ index: i, kind: c.dynamic, mesh, base: [...c.pos] as [number, number, number], size: c.size });
        return;
      }
      if (c.destructible && c.type === 'barrel') {
        const g = new THREE.Group();
        const body = new THREE.Mesh(new THREE.CylinderGeometry(0.34, 0.34, 1.2, 20), barrelMat);
        body.castShadow = true;
        body.receiveShadow = true;
        g.add(body);
        for (const y of [-0.2, 0.2]) {
          const ring = new THREE.Mesh(new THREE.CylinderGeometry(0.352, 0.352, 0.09, 20), doorMat);
          ring.position.y = y;
          g.add(ring);
        }
        const cap = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.12, 0.06, 12), liftMat);
        cap.position.y = 0.62;
        g.add(cap);
        g.position.set(c.pos[0], c.pos[1] + 0.6, c.pos[2]);
        this.group.add(g);
        this.coverMeshes.set(i, g);
        return;
      }
      const geo = new THREE.BoxGeometry(c.size[0], c.size[1], c.size[2]);
      geo.translate(c.pos[0], c.pos[1], c.pos[2]);
      const mat =
        c.type === 'container'
          ? i % 2 === 1
            ? contBlueMat
            : contOrangeMat
          : c.type === 'lowwall'
            ? lowMat
            : c.type === 'crate'
              ? crateMat
              : wallMat;
      if (c.destructible) {
        const mesh = new THREE.Mesh(geo, mat);
        mesh.castShadow = true;
        mesh.receiveShadow = true;
        this.group.add(mesh);
        this.coverMeshes.set(i, mesh);
        return;
      }
      const list = batches.get(mat) ?? [];
      list.push(geo);
      batches.set(mat, list);
    });

    for (const [mat, geos] of batches) {
      const merged = mergeGeometries(geos);
      if (!merged) continue;
      const mesh = new THREE.Mesh(merged, mat);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      this.group.add(mesh);
    }

    this.group.add(buildProps(crateMat, metalMat, doorMat, map));
    scene.add(this.group);
  }

  breakCover(index: number): void {
    const mesh = this.coverMeshes.get(index);
    if (!mesh) return;
    this.group.remove(mesh);
    mesh.traverse((o) => {
      if (o instanceof THREE.Mesh) o.geometry.dispose();
    });
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
      }
    });
    for (const m of this.ownMaterials) m.dispose();
    this.group.removeFromParent();
    this.coverMeshes.clear();
    this.dynEntries.length = 0;
  }
}

/** 装饰道具层（零碰撞 / 导航影响）：托盘堆、沿墙管道、灯柱、地面安全线、集装箱顶细节（[M13] 纯色） */
function buildProps(
  crateMat: THREE.Material,
  metalMat: THREE.Material,
  cautionMat: THREE.Material,
  map: MapDef,
): THREE.Group {
  const g = new THREE.Group();
  const woodGeos: THREE.BufferGeometry[] = [];
  const metalGeos: THREE.BufferGeometry[] = [];
  const cautionGeos: THREE.BufferGeometry[] = [];

  const addBox = (list: THREE.BufferGeometry[], w: number, h: number, d: number, x: number, y: number, z: number, ry = 0): void => {
    const geo = new THREE.BoxGeometry(w, h, d);
    if (ry !== 0) geo.rotateY(ry);
    geo.translate(x, y, z);
    list.push(geo);
  };
  const addCyl = (list: THREE.BufferGeometry[], r: number, len: number, x: number, y: number, z: number, axis: 'x' | 'y' | 'z', seg = 12): void => {
    const geo = new THREE.CylinderGeometry(r, r, len, seg);
    if (axis === 'x') geo.rotateZ(Math.PI / 2);
    if (axis === 'z') geo.rotateX(Math.PI / 2);
    geo.translate(x, y, z);
    list.push(geo);
  };

  for (const [px, pz, count] of [
    [-21, -18, 2],
    [21.5, 19, 3],
    [-20, 20, 2],
    [20.5, -20, 3],
    [-21.5, 2, 2],
    [21.5, -3, 2],
  ] as const) {
    for (let i = 0; i < count; i++) {
      addBox(woodGeos, 1.15, 0.13, 1.0, px, 0.07 + i * 0.145, pz, i * 0.12);
    }
  }

  for (const [axis, fixed] of [
    ['x', -23.4],
    ['x', 23.4],
    ['z', -23.4],
    ['z', 23.4],
  ] as const) {
    if (axis === 'x') addCyl(metalGeos, 0.075, 45, 0, 3.4, fixed, 'x');
    else addCyl(metalGeos, 0.075, 45, fixed, 3.4, 0, 'z');
    addCyl(metalGeos, 0.075, 45, 0, 3.15, fixed, axis === 'x' ? 'x' : 'z');
    for (let k = -2; k <= 2; k++) {
      const off = k * 9;
      if (axis === 'x') addCyl(metalGeos, 0.16, 0.05, off, 3.3, fixed, 'y');
      else addCyl(metalGeos, 0.16, 0.05, fixed, 3.3, off, 'y');
    }
  }

  for (const [px, pz] of [
    [-16.5, -16.5],
    [16.5, 16.5],
    [-16.5, 16.5],
    [16.5, -16.5],
  ] as const) {
    addCyl(metalGeos, 0.06, 4.6, px, 2.3, pz, 'y', 10);
    addBox(metalGeos, 0.5, 0.1, 0.24, px, 4.55, pz);
    addBox(metalGeos, 0.42, 0.07, 0.16, px + 0.16, 4.48, pz);
  }

  for (const x of [-1.9, 1.9]) {
    const geo = new THREE.PlaneGeometry(0.24, 24);
    geo.rotateX(-Math.PI / 2);
    geo.translate(x, 0.015, 0);
    cautionGeos.push(geo);
  }
  for (const z of [-1.9, 1.9]) {
    const geo = new THREE.PlaneGeometry(24, 0.24);
    geo.rotateX(-Math.PI / 2);
    geo.translate(0, 0.015, z);
    cautionGeos.push(geo);
  }

  map.covers.forEach((c, i) => {
    if (c.type !== 'container' || c.dynamic || c.destructible) return;
    const top = c.pos[1] + c.size[1];
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        addBox(metalGeos, 0.22, 0.18, 0.22, c.pos[0] + sx * (c.size[0] / 2 - 0.13), top + 0.09, c.pos[2] + sz * (c.size[2] / 2 - 0.13));
      }
    }
    addBox(metalGeos, 0.5, 0.12, 0.36, c.pos[0] + (i % 2 === 0 ? 1.2 : -1.2), top + 0.06, c.pos[2]);
  });

  const emit = (geos: THREE.BufferGeometry[], mat: THREE.Material, shadow: boolean): void => {
    const merged = mergeGeometries(geos);
    if (!merged) return;
    const mesh = new THREE.Mesh(merged, mat);
    mesh.castShadow = shadow;
    mesh.receiveShadow = shadow;
    g.add(mesh);
  };
  emit(woodGeos, crateMat, true);
  emit(metalGeos, metalMat, true);
  emit(cautionGeos, cautionMat, false);
  return g;
}

export function dynIndicesOf(map: MapDef): number[] {
  return map.covers.map((c, i) => (c.dynamic ? i : -1)).filter((i) => i >= 0);
}
