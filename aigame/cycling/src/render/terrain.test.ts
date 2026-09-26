import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { buildTrack } from '../sim/trackData';
import { buildTerrain } from './terrain';

const track = buildTrack();

function surfaceAt(mesh: THREE.Mesh, wx: number, wz: number): number {
  const geo = mesh.geometry as THREE.PlaneGeometry;
  const pos = geo.attributes.position as THREE.BufferAttribute;
  const seg = geo.parameters.widthSegments as number;
  const cell = (geo.parameters.width as number) / seg;
  const cellZ = (geo.parameters.height as number) / seg;
  const h = (ix: number, iz: number) => pos.getY(iz * (seg + 1) + ix);
  const fx = (wx - mesh.position.x) / cell + seg / 2;
  const fz = (wz - mesh.position.z) / cellZ + seg / 2;
  const ix = Math.floor(fx);
  const iz = Math.floor(fz);
  const tx = fx - ix;
  const tz = fz - iz;
  return (
    h(ix, iz) * (1 - tx) * (1 - tz) +
    h(ix + 1, iz) * tx * (1 - tz) +
    h(ix, iz + 1) * (1 - tx) * tz +
    h(ix + 1, iz + 1) * tx * tz
  );
}

describe('terrain', () => {
  const mesh = buildTerrain(track);

  it('mesh is offset to track bbox center so world coords align', () => {
    const pts = track.densePoints();
    let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
    for (const [x, , z] of pts) {
      minX = Math.min(minX, x); maxX = Math.max(maxX, x);
      minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
    }
    expect(mesh.position.x).toBeCloseTo((minX + maxX) / 2, 6);
    expect(mesh.position.z).toBeCloseTo((minZ + maxZ) / 2, 6);
  });

  it('terrain surface stays below the road along the whole lap', () => {
    for (let d = 0; d < track.length; d += 5) {
      const s = track.sampleAt(d);
      expect(surfaceAt(mesh, s.x, s.z)).toBeLessThanOrEqual(s.y - 0.02);
    }
  });

  it('terrain hugs the road corridor', () => {
    for (let d = 0; d < track.length; d += 100) {
      const s = track.sampleAt(d);
      expect(surfaceAt(mesh, s.x, s.z)).toBeGreaterThan(s.y - 0.6);
    }
  });
});
