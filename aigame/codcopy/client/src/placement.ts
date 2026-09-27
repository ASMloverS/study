import { AIRSTRIKE_COUNT, AIRSTRIKE_SPACING, CLUSTER_SCATTER, type MapDef } from 'shared';
import { COVER_COLORS } from './render/minimap';

/** [M15] CoD 式连杀放置：北向朝上战术地图，虚拟光标选点，空袭可旋转航线 */

export const HEADING_STEP = Math.PI / 12; // 15°/档

export function clampToMap(v: number, mapSize: number): number {
  const lim = mapSize / 2 - 1;
  return Math.max(-lim, Math.min(lim, v));
}

export interface PlacementResult {
  streak: 2 | 3;
  streakTarget: { x: number; z: number };
  streakYaw: number;
}

export class Placement {
  active = false;
  tier: 2 | 3 = 2;
  cursor = { x: 0, z: 0 };
  heading = 0;
  private self = { x: 0, z: 0, yaw: 0 };
  private uavEnemies: { x: number; z: number }[] = [];
  private enemyRgb = '255,70,60';
  private ctx: CanvasRenderingContext2D | null = null;

  constructor(private map: MapDef, private canvas: HTMLCanvasElement) {}

  open(tier: 2 | 3, self: { x: number; z: number; yaw: number }, uavEnemies: { x: number; z: number }[]): void {
    this.tier = tier;
    this.self = self;
    this.uavEnemies = uavEnemies;
    this.cursor = { x: self.x, z: self.z };
    this.heading = self.yaw;
    this.active = true;
    this.canvas.style.display = 'block';
    (globalThis.document?.getElementById('tacmaphint') as HTMLElement | null)?.style.setProperty('display', 'block');
  }

  close(): void {
    this.active = false;
    this.canvas.style.display = 'none';
    (globalThis.document?.getElementById('tacmaphint') as HTMLElement | null)?.style.setProperty('display', 'none');
  }

  setEnemyColor(rgb: string): void {
    this.enemyRgb = rgb;
  }

  updateSelf(self: { x: number; z: number; yaw: number }): void {
    this.self = self;
  }

  moveCursor(dxPx: number, dyPx: number): void {
    if (!this.active) return;
    const worldPerPx = this.map.size / this.canvas.width;
    this.cursor.x = clampToMap(this.cursor.x + dxPx * worldPerPx, this.map.size);
    this.cursor.z = clampToMap(this.cursor.z + dyPx * worldPerPx, this.map.size);
  }

  rotate(dir: 1 | -1): void {
    if (!this.active) return;
    this.heading += dir * HEADING_STEP;
  }

  confirm(): PlacementResult | null {
    if (!this.active) return null;
    const r: PlacementResult = { streak: this.tier, streakTarget: { x: this.cursor.x, z: this.cursor.z }, streakYaw: this.heading };
    this.close();
    return r;
  }

  render(): void {
    if (!this.active) return;
    if (!this.ctx) this.ctx = this.canvas.getContext('2d');
    const ctx = this.ctx;
    if (!ctx) return;
    const size = this.canvas.width;
    const world = this.map.size;
    const s = size / world;
    ctx.clearRect(0, 0, size, size);
    ctx.fillStyle = 'rgba(12,16,20,0.92)';
    ctx.fillRect(0, 0, size, size);
    for (const c of this.map.covers) {
      ctx.fillStyle = COVER_COLORS[c.type] ?? '#555';
      ctx.fillRect((c.pos[0] + world / 2) * s - (c.size[0] * s) / 2, (c.pos[2] + world / 2) * s - (c.size[2] * s) / 2, c.size[0] * s, c.size[2] * s);
    }
    const px = (v: number) => (v + world / 2) * s;
    // UAV 敌人（UAV 激活期间显示）
    for (const e of this.uavEnemies) {
      ctx.fillStyle = `rgba(${this.enemyRgb},0.95)`;
      ctx.beginPath();
      ctx.arc(px(e.x), px(e.z), 4, 0, Math.PI * 2);
      ctx.fill();
    }
    // 自己（白色箭头，指向朝向）
    const sx = px(this.self.x);
    const sz = px(this.self.z);
    ctx.save();
    ctx.translate(sx, sz);
    ctx.rotate(-this.self.yaw);
    ctx.fillStyle = '#e8f4ff';
    ctx.beginPath();
    ctx.moveTo(0, -8);
    ctx.lineTo(6, 7);
    ctx.lineTo(-6, 7);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
    // 落点预览：空袭=沿航线 5 弹 + 航线虚线；集束=散布圆
    const cx = px(this.cursor.x);
    const cz = px(this.cursor.z);
    ctx.strokeStyle = '#ff5040';
    ctx.lineWidth = 2;
    if (this.tier === 2) {
      const dx = -Math.sin(this.heading);
      const dz = -Math.cos(this.heading);
      for (let i = 0; i < AIRSTRIKE_COUNT; i++) {
        const off = (i - (AIRSTRIKE_COUNT - 1) / 2) * AIRSTRIKE_SPACING;
        ctx.beginPath();
        ctx.arc(px(clampToMap(this.cursor.x + dx * off, this.map.size)), px(clampToMap(this.cursor.z + dz * off, this.map.size)), 4, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.beginPath();
      ctx.moveTo(cx - dx * 40 * s, cz - dz * 40 * s);
      ctx.lineTo(cx + dx * 40 * s, cz + dz * 40 * s);
      ctx.setLineDash([6, 6]);
      ctx.stroke();
      ctx.setLineDash([]);
    } else {
      ctx.beginPath();
      ctx.arc(cx, cz, CLUSTER_SCATTER * s, 0, Math.PI * 2);
      ctx.stroke();
    }
    // 光标
    ctx.strokeStyle = '#ffd60a';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(cx - 10, cz);
    ctx.lineTo(cx + 10, cz);
    ctx.moveTo(cx, cz - 10);
    ctx.lineTo(cx, cz + 10);
    ctx.stroke();
  }
}
