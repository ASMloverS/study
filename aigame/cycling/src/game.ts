import * as THREE from 'three';
import { createRace, standings, stepRace } from './sim/race';
import type { RaceState } from './sim/race';
import { buildTrack } from './sim/trackData';
import type { TrackSample } from './sim/track';
import { GEARS, ITEM_BOXES, RACE } from './sim/params';
import { cadence } from './sim/drivetrain';
import { createLoop } from './core/loop';
import { InputController } from './input';
import { createScene } from './render/scene';
import { buildTerrain } from './render/terrain';
import { buildTrackMesh, buildItemBoxes } from './render/trackMesh';
import { buildRiderMesh, placeRider } from './render/riders';
import { ChaseCamera } from './render/camera';
import { SpeedLines } from './render/speedLines';
import { Minimap } from './render/minimap';
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
  private speedLines!: SpeedLines;
  private minimap!: Minimap;
  private loop: ReturnType<typeof createLoop>;
  private tmp = new THREE.Vector3();
  private boxMeshes: THREE.Mesh[] = [];
  private pickedCount = 0;

  constructor(private hud: Hud, private input: InputController, private music: Music) {
    this.ctx = createScene(document.querySelector<HTMLElement>('#app')!);
    this.ctx.scene.add(buildTerrain(this.track));
    this.ctx.scene.add(buildTrackMesh(this.track));
    const boxes = buildItemBoxes();
    this.boxMeshes = boxes.meshes;
    this.ctx.scene.add(boxes.group);
    this.speedLines = new SpeedLines(this.ctx.scene);
    this.minimap = new Minimap(document.querySelector('#minimap') as HTMLCanvasElement, this.track);
    for (let i = 0; i < 8; i++) {
      const m = buildRiderMesh(JERSEYS[i], i === 0);
      this.meshes.push(m);
      this.ctx.scene.add(m);
    }
    this.cam = new ChaseCamera(this.ctx.camera);
    this.hud.setProfile(this.track, ITEM_BOXES.filter((_, i) => i % RACE.boxOffsets.length === 1).map((b) => b.d));
    this.loop = createLoop((dt) => this.update(dt), (a, fdt) => this.render(a, fdt));
    window.addEventListener('keydown', (e) => {
      if (e.key.toLowerCase() === 'r' && this.state.phase === 'finished') this.start();
    });
  }

  start(): void {
    this.state = createRace(this.track);
    this.prev = this.state;
    this.pickedCount = 0;
    this.input.reset();
    this.hud.clearResults();
    const s = this.track.sampleAt(0);
    this.ctx.camera.position.set(s.x - Math.cos(s.heading) * 6, s.y + 2.4, s.z - Math.sin(s.heading) * 6);
    this.cam.reset(this.ctx.camera.position);
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
    this.music.setTier(s.phase === 'racing' ? (remaining > s.trackLength * 0.04 ? 'intense' : 'sprint') : 'calm');
    this.music.setWind(s.riders[0].speed);
    for (let i = 0; i < s.riders.length; i++) {
      const p = this.prev.riders[i];
      const c = s.riders[i];
      const lean = Math.max(-0.35, Math.min(0.35, (c.lateral - p.lateral) * 6));
      placeRider(this.meshes[i], this.track.sampleAt(p.dist + (c.dist - p.dist) * alpha), p.lateral + (c.lateral - p.lateral) * alpha, lean);
      const blurOn = cadence(c.speed, c.cog) > 120;
      (this.meshes[i].userData.spokes as THREE.Object3D[]).forEach((sp) => (sp.visible = !blurOn));
      (this.meshes[i].userData.blur as THREE.Mesh[]).forEach((d) => (d.visible = blurOn));
    }
    for (let i = 0; i < this.boxMeshes.length; i++) {
      const m = this.boxMeshes[i];
      const smp = this.track.sampleAt(m.userData.d as number);
      const nx = -Math.sin(smp.heading);
      const nz = Math.cos(smp.heading);
      m.position.set(smp.x + nx * (m.userData.lat as number), smp.y + 1.2, smp.z + nz * (m.userData.lat as number));
      m.rotation.y += frameDt * 2;
      m.rotation.x += frameDt;
      m.visible = !s.riders[0].collected[i];
    }
    const picked = s.riders[0].collected.filter(Boolean).length;
    if (picked > this.pickedCount) {
      this.pickedCount = picked;
      this.hud.flashPickup('+10%');
      this.music.blip();
    }
    const ps = this.track.sampleAt(this.interp(this.prev.riders[0].dist, s.riders[0].dist, alpha));
    this.tmp.set(ps.x, ps.y, ps.z);
    this.cam.update(this.tmp, ps.heading, ps.gradient, frameDt, s.riders[0].speed);
    this.speedLines.update(s.riders[0].speed, frameDt, this.ctx.camera);
    this.hud.update(this.view(ps), frameDt);
    this.minimap.draw(s.riders, this.track);
    this.ctx.renderer.render(this.ctx.scene, this.ctx.camera);
  }

  private interp(a: number, b: number, alpha: number): number {
    return a + (b - a) * alpha;
  }

  private view(ps: TrackSample) {
    const s = this.state;
    const p = s.riders[0];
    const remaining = s.trackLength - p.dist;
    return {
      speedKmh: p.speed * 3.6,
      gearLine: `52×${Math.round(p.cog)} · ${GEARS[p.gear]}`,
      cadence: cadence(p.speed, p.cog),
      cadTarget: p.cadTarget,
      finalSprint: s.phase === 'racing' && remaining <= s.trackLength * 0.04,
      energyFrac: p.energy / p.type.maxEnergy,
      gradientPct: ps.gradient * 100,
      remainingKm: Math.max(0, remaining) / 1000,
      position: standings(s).findIndex((r) => r.isPlayer) + 1,
      fieldSize: s.riders.length,
      progress: Math.min(1, p.dist / s.trackLength),
      countdown: s.phase === 'countdown' ? Math.ceil(s.countdown) : s.time < 0.8 ? 0 : null,
      phase: s.phase,
      results: s.results,
    };
  }
}
