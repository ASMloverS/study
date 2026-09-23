import * as THREE from 'three';
import type { PlayerSnap } from 'shared';

const PALETTE = [0xc94f3f, 0x3f7fc9, 0x8fca4f, 0xc9a23a, 0x9a5fc9, 0x3fc9b0, 0xd06f9e, 0x8a8f3f];
const INTERP_DELAY = 0.066;

interface SnapEntry {
  time: number;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  h: number;
  alive: boolean;
  speed: number;
}

export interface ActorPose {
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch: number;
  h: number;
  speed: number;
}

class RemoteView {
  readonly group = new THREE.Group();
  private buffer: SnapEntry[] = [];
  private mats: THREE.MeshLambertMaterial[] = [];
  private deathAt: number | null = null;
  pose: ActorPose = { x: 0, y: 0, z: 0, yaw: 0, pitch: 0, h: 1.8, speed: 0 };

  constructor(color: number) {
    const bodyMat = new THREE.MeshLambertMaterial({ color, transparent: true });
    const headMat = new THREE.MeshLambertMaterial({ color: 0x2b2b2b, transparent: true });
    const gunMat = new THREE.MeshLambertMaterial({ color: 0x30343a, transparent: true });
    this.mats = [bodyMat, headMat, gunMat];
    const body = new THREE.Mesh(new THREE.CapsuleGeometry(0.35, 1.1, 6, 12), bodyMat);
    body.position.y = 0.85;
    body.castShadow = true;
    const head = new THREE.Mesh(new THREE.BoxGeometry(0.36, 0.35, 0.36), headMat);
    head.position.y = 1.63;
    head.castShadow = true;
    const gun = new THREE.Mesh(new THREE.BoxGeometry(0.12, 0.14, 0.75), gunMat);
    gun.position.set(0.3, 1.35, -0.3);
    this.group.add(body, head, gun);
  }

  push(s: PlayerSnap, time: number): void {
    if (!s.a && this.buffer.length > 0 && this.buffer[this.buffer.length - 1].alive) {
      this.deathAt = time;
    }
    if (s.a) this.deathAt = null;
    this.buffer.push({
      time,
      x: s.x,
      y: s.y,
      z: s.z,
      yaw: s.yaw,
      pitch: s.pitch,
      h: s.h,
      alive: s.a,
      speed: Math.hypot(s.vx, s.vz),
    });
    if (this.buffer.length > 10) this.buffer.shift();
  }

  render(now: number): void {
    const target = now - INTERP_DELAY;
    while (this.buffer.length > 2 && this.buffer[1].time < target) this.buffer.shift();
    const b = this.buffer;
    if (b.length === 0) return;
    const latest = b[b.length - 1];
    if (this.deathAt !== null) {
      const age = now - this.deathAt;
      if (age > 0.85) {
        this.group.visible = false;
        return;
      }
      this.group.visible = true;
      const fall = Math.min(1, age / 0.35);
      this.group.rotation.x = (fall * Math.PI) / 2;
      const fade = age < 0.35 ? 1 : Math.max(0, 1 - (age - 0.35) / 0.5);
      for (const m of this.mats) m.opacity = fade;
      return;
    }
    for (const m of this.mats) m.opacity = 1;
    this.group.rotation.x = 0;
    this.group.visible = latest.alive;
    if (!latest.alive) return;
    let e: SnapEntry;
    if (b.length === 1 || b[0].time >= target) {
      e = b[0];
    } else {
      const s0 = b[0];
      const s1 = b[1];
      const a = Math.max(0, Math.min(1, (target - s0.time) / Math.max(1e-6, s1.time - s0.time)));
      let dyaw = s1.yaw - s0.yaw;
      if (dyaw > Math.PI) dyaw -= Math.PI * 2;
      if (dyaw < -Math.PI) dyaw += Math.PI * 2;
      e = {
        ...s1,
        x: s0.x + (s1.x - s0.x) * a,
        y: s0.y + (s1.y - s0.y) * a,
        z: s0.z + (s1.z - s0.z) * a,
        yaw: s0.yaw + dyaw * a,
        speed: s0.speed + (s1.speed - s0.speed) * a,
      };
    }
    this.pose = { x: e.x, y: e.y, z: e.z, yaw: e.yaw, pitch: e.pitch, h: e.h, speed: e.speed };
    this.group.position.set(e.x, e.y, e.z);
    this.group.rotation.y = e.yaw;
    this.group.scale.y = e.h / 1.8;
  }
}

export class RemoteViews {
  private views = new Map<number, RemoteView>();

  constructor(private scene: THREE.Scene) {}

  pushAll(players: PlayerSnap[], selfId: number, time: number): void {
    for (const p of players) {
      if (p.id === selfId) continue;
      let v = this.views.get(p.id);
      if (!v) {
        v = new RemoteView(PALETTE[p.id % PALETTE.length]);
        this.views.set(p.id, v);
        this.scene.add(v.group);
      }
      v.push(p, time);
    }
  }

  render(now: number): void {
    for (const v of this.views.values()) v.render(now);
  }

  getPose(id: number): ActorPose | null {
    const v = this.views.get(id);
    return v && v.group.visible ? v.pose : null;
  }

  forEach(cb: (id: number, pose: ActorPose) => void): void {
    for (const [id, v] of this.views) {
      if (v.group.visible) cb(id, v.pose);
    }
  }

  clear(): void {
    for (const v of this.views.values()) this.scene.remove(v.group);
    this.views.clear();
  }
}
