import { describe, expect, it } from 'vitest';
import { SHOT_PARAMS, distanceCutoff } from '../src/audio';

const IDS = ['ar_shot', 'smg_shot', 'lmg_shot', 'dmr_shot', 'sg_shot', 'sr_shot', 'pistol_shot'] as const;

describe('[M15] shot synth params', () => {
  it('every weapon has a complete, finite, positive layer set', () => {
    for (const id of IDS) {
      for (const [k, v] of Object.entries(SHOT_PARAMS[id])) {
        expect(Number.isFinite(v), `${id}.${k}`).toBe(true);
        expect(v as number, `${id}.${k}`).toBeGreaterThan(0);
      }
    }
  });

  it('weapon layers differ: each pair differs in >=3 fields', () => {
    for (let i = 0; i < IDS.length; i++) {
      for (let j = i + 1; j < IDS.length; j++) {
        const a = SHOT_PARAMS[IDS[i]];
        const b = SHOT_PARAMS[IDS[j]];
        const diff = Object.keys(a).filter((k) => a[k as keyof typeof a] !== b[k as keyof typeof b]);
        expect(diff.length, `${IDS[i]} vs ${IDS[j]}`).toBeGreaterThanOrEqual(3);
      }
    }
  });

  it('distanceCutoff falls with distance and clamps to [500, 8000]', () => {
    expect(distanceCutoff(0)).toBeGreaterThan(distanceCutoff(40));
    expect(distanceCutoff(40)).toBeGreaterThan(distanceCutoff(80));
    expect(distanceCutoff(0)).toBeLessThanOrEqual(8000);
    expect(distanceCutoff(1000)).toBe(500);
  });
});
