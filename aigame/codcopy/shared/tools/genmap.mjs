import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const covers = [];
const spawns = [];
const interestPoints = [];

const S = 24; // half map size
const T = 0.5; // wall thickness

// ---- boundary walls (height 4) ----
covers.push({ type: 'wall', pos: [0, 2, -S + T / 2], size: [2 * S, 4, T] });
covers.push({ type: 'wall', pos: [0, 2, S - T / 2], size: [2 * S, 4, T] });
covers.push({ type: 'wall', pos: [-S + T / 2, 2, 0], size: [T, 4, 2 * S - 2 * T] });
covers.push({ type: 'wall', pos: [S - T / 2, 2, 0], size: [T, 4, 2 * S - 2 * T] });

// ---- central warehouse (|x|<10, |z|<10), point-symmetric ----
// north/south walls with 6m center door gap
for (const sz of [-1, 1]) {
  covers.push({ type: 'wall', pos: [-6.5, 1.5, sz * 10], size: [7, 3, 0.4] });
  covers.push({ type: 'wall', pos: [6.5, 1.5, sz * 10], size: [7, 3, 0.4] });
}
// west/east walls with center gap + corner gaps
for (const sx of [-1, 1]) {
  covers.push({ type: 'wall', pos: [sx * 10, 1.5, -5], size: [0.4, 3, 4] });
  covers.push({ type: 'wall', pos: [sx * 10, 1.5, 5], size: [0.4, 3, 4] });
}
// container pair inside
covers.push({ type: 'container', pos: [-5, 1.3, 2], size: [2.4, 2.6, 6] });
covers.push({ type: 'container', pos: [5, 1.3, -2], size: [2.4, 2.6, 6] });
// central lift platform (dynamic, cycles 0..2m)
covers.push({ type: 'lowwall', pos: [0, 0.55, 0], size: [3, 1.1, 3], dynamic: 'lift' });
// auto sliding doors at warehouse north/south gates (dynamic)
covers.push({ type: 'wall', pos: [0, 1.5, -10], size: [6, 3, 0.3], dynamic: 'door' });
covers.push({ type: 'wall', pos: [0, 1.5, 10], size: [6, 3, 0.3], dynamic: 'door' });
// inner barrels (destructible later)
covers.push({ type: 'barrel', pos: [-8, 0.6, -8], size: [0.7, 1.2, 0.7], destructible: true, hp: 80 });
covers.push({ type: 'barrel', pos: [8, 0.6, 8], size: [0.7, 1.2, 0.7], destructible: true, hp: 80 });

// ---- side lanes (x = ±16) ----
for (const z of [-12, -6, 0, 6, 12]) {
  covers.push({ type: 'lowwall', pos: [-16, 0.55, z], size: [3, 1.1, 0.5] });
  covers.push({ type: 'lowwall', pos: [16, 0.55, z], size: [3, 1.1, 0.5] });
}
for (const sx of [-1, 1]) {
  // stacked crate pairs
  covers.push({ type: 'crate', pos: [sx * 14, 0.6, -9], size: [1.2, 1.2, 1.2], destructible: true, hp: 150 });
  covers.push({ type: 'crate', pos: [sx * 14, 1.8, -9], size: [1.2, 1.2, 1.2], destructible: true, hp: 150 });
  covers.push({ type: 'crate', pos: [sx * 18, 0.6, 3], size: [1.2, 1.2, 1.2], destructible: true, hp: 150 });
  covers.push({ type: 'crate', pos: [sx * 18, 1.8, 3], size: [1.2, 1.2, 1.2], destructible: true, hp: 150 });
// lane barrels (paired for chain reactions at (-13,-3)/(-13+?,...) — keep pairs within 3m)
covers.push({ type: 'barrel', pos: [sx * 13, 0.6, -3], size: [0.7, 1.2, 0.7], destructible: true, hp: 80 });
covers.push({ type: 'barrel', pos: [sx * 13.8, 0.6, -2.4], size: [0.7, 1.2, 0.7], destructible: true, hp: 80 });
covers.push({ type: 'barrel', pos: [sx * 19, 0.6, 9], size: [0.7, 1.2, 0.7], destructible: true, hp: 80 });
}

// ---- spawn corners: L-shaped low walls ----
for (const sx of [-1, 1]) {
  for (const sz of [-1, 1]) {
    const cx = sx * 20;
    const cz = sz * 20;
    covers.push({ type: 'lowwall', pos: [cx, 0.55, cz - sz * 2], size: [3, 1.1, 0.5] });
    covers.push({ type: 'lowwall', pos: [cx - sx * 2, 0.55, cz], size: [0.5, 1.1, 3] });
  }
}

// ---- spawns: 4 corners + 4 mid-edges ----
for (const sx of [-1, 1]) {
  for (const sz of [-1, 1]) spawns.push([sx * 20.5, 0, sz * 20.5]);
}
spawns.push([20.5, 0, 0], [-20.5, 0, 0], [0, 0, 20.5], [0, 0, -20.5]);

// ---- AI interest points ----
interestPoints.push([0, 0, 0], [7, 0, 7], [-7, 0, -7]);
for (const sx of [-1, 1]) {
  for (const sz of [-1, 1]) interestPoints.push([sx * 16, 0, sz * 8]);
}

const map = { name: 'warehouse', size: 48, covers, spawns, interestPoints };
const out = join(dirname(fileURLToPath(import.meta.url)), '..', 'src', 'map', 'warehouse.json');
writeFileSync(out, JSON.stringify(map, null, 2) + '\n');
console.log(`wrote ${out}: ${covers.length} covers, ${spawns.length} spawns, ${interestPoints.length} POIs`);
