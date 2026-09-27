import * as THREE from 'three';
import { createRace, standings, stepRace } from './sim/race';
import type { RaceState } from './sim/race';
import { buildTrack } from './sim/trackData';
import type { TrackSample } from './sim/track';
import { GEARS, RACE } from './sim/params';
import { cadence } from './sim/drivetrain';
import { createLoop } from './core/loop';
import { InputController } from './input';
import { createScene } from './render/scene';
import { buildTerrain } from './render/terrain';
import { buildTrackMesh } from './render/trackMesh';
import { buildRiderMesh, placeRider } from './render/riders';
import { ChaseCamera } from './render/camera';
import { Hud } from './ui/hud';
import type { Music } from './audio';

const JERSEYS = [0xffd54a, 0xe0533d, 0x4d8fd6, 0x8a5cd6, 0x4dbd8a, 0xd68a4d, 0xd64d9e, 0x5a6a7a];

export class Game {
  private track = buildTrack();
  private state: RaceState = createRace(this.track);
  private prev = this.state;
  private meshes: THREE.Object3D[] = [];
  private ctx: ReturnType<typeof createScene>;
  private cam: ChaseCamera;
  private loop: ReturnType<typeof createLoop>;
  private tmp = new THREE.Vector3();

  constructor(private hud: Hud, private input: InputController, private music: Music) {
    this.ctx = createScene(document.querySelector<HTMLElement>('#app')!);
    this.ctx.scene.add(buildTerrain(this.track));
    this.ctx.scene.add(buildTrackMesh(this.track));
    for (let i = 0; i < 8; i++) {
      const m = buildRiderMesh(JERSEYS[i]);
      this.meshes.push(m);
      this.ctx.scene.add(m);
    }
    this.cam = new ChaseCamera(this.ctx.camera);
    this.hud.setProfile(this.track, RACE.feedZones);
    this.loop = createLoop((dt) => this.update(dt), (a, fdt) => this.render(a, fdt));
    window.addEventListener('keydown', (e) => {
      if (e.key.toLowerCase() === 'r' && this.state.phase === 'finished') this.start();
    });
  }

  start(): void {
    this.state = createRace(this.track);
    this.prev = this.state;
    this.input.reset();
    this.hud.clearResults();
    const s = this.track.sampleAt(0);
    this.ctx.camera.position.set(s.x - Math.cos(s.heading) * 8.5, s.y + 3.2, s.z - Math.sin(s.heading) * 8.5);
    this.ctx.camera.lookAt(s.x, s.y + 1.2, s.z);
    this.loop.stop();
    this.loop.start();
  }

  private update(dt: number): void {
    this.prev = this.state;
    this.state = stepRace(this.state, this.track, this.input.command(), dt);
  }

  private render(alpha: number, frameDt: number): void {
    const s = this.state;
    const remaining = s.trackLength - s.riders[0].dist;
    this.music.setTier(s.phase === 'racing' ? (remaining > 800 ? 'intense' : 'sprint') : 'calm');
    for (let i = 0; i < s.riders.length; i++) {
      const p = this.prev.riders[i];
      const c = s.riders[i];
      placeRider(this.meshes[i], this.track.sampleAt(p.dist + (c.dist - p.dist) * alpha), p.lateral + (c.lateral - p.lateral) * alpha);
    }
    const ps = this.track.sampleAt(this.interp(this.prev.riders[0].dist, s.riders[0].dist, alpha));
    this.tmp.set(ps.x, ps.y, ps.z);
    this.cam.update(this.tmp, ps.heading, ps.gradient, frameDt);
    this.hud.update(this.view(ps));
    this.ctx.renderer.render(this.ctx.scene, this.ctx.camera);
  }

  private interp(a: number, b: number, alpha: number): number {
    return a + (b - a) * alpha;
  }

  private view(ps: TrackSample) {
    const s = this.state;
    const p = s.riders[0];
    return {
      speedKmh: p.speed * 3.6,
      gearLine: (() => {
        const cad = cadence(p.speed, p.cog);
        const target = Math.round(p.cadTarget);
        const rpm = Math.abs(cad - target) > 3 ? `${cad.toFixed(0)}/${target}` : cad.toFixed(0);
        return `52×${Math.round(p.cog)} · ${rpm}rpm · ${GEARS[p.gear]}`;
      })(),
      energyFrac: p.energy / p.type.maxEnergy,
      gradientPct: ps.gradient * 100,
      remainingKm: Math.max(0, s.trackLength - p.dist) / 1000,
      position: standings(s).findIndex((r) => r.isPlayer) + 1,
      fieldSize: s.riders.length,
      progress: Math.min(1, p.dist / s.trackLength),
      countdown: s.phase === 'countdown' ? Math.ceil(s.countdown) : s.time < 0.8 ? 0 : null,
      phase: s.phase,
      results: s.results,
    };
  }
}
