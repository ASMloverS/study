import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { WEAPON_LIST } from 'shared';
import { gunFor } from '../src/render/actors';

const mat = new THREE.MeshBasicMaterial();

describe('[M15] third-person gun silhouettes', () => {
  it('pistol has >=3 parts, long guns >=5, lmg >=6', () => {
    for (const w of WEAPON_LIST) {
      const n = gunFor(w, mat).children.length;
      if (w === 'pistol') expect(n, w).toBeGreaterThanOrEqual(3);
      else if (w === 'lmg') expect(n, w).toBeGreaterThanOrEqual(6);
      else expect(n, w).toBeGreaterThanOrEqual(5);
    }
  });

  it('lmg carries distinct signature geometries (drum + bipod)', () => {
    const geos = gunFor('lmg', mat).children.map((c) => (c as THREE.Mesh).geometry);
    const uuids = new Set(geos.map((g) => g.uuid));
    expect(geos.length).toBeGreaterThanOrEqual(6);
    expect(uuids.size).toBeGreaterThanOrEqual(5);
  });
});
