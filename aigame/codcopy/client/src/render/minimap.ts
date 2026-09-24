import type { MapDef } from 'shared';

const COVER_COLORS: Record<string, string> = {
  wall: '#565c63',
  lowwall: '#4a5056',
  container: '#8a5230',
  crate: '#6b5233',
  barrel: '#8f7530',
};

export interface EnemyBlip {
  x: number;
  z: number;
  until: number;
}

export class Minimap {
  private readonly ctx: CanvasRenderingContext2D;
  private readonly canvas: HTMLCanvasElement;

  constructor(private map: MapDef) {
    this.canvas = document.getElementById('minimap') as HTMLCanvasElement;
    this.ctx = this.canvas.getContext('2d')!;
  }

  render(self: { x: number; z: number; yaw: number }, enemies: Map<number, EnemyBlip>, now: number, uavEnemies: { x: number; z: number }[] = []): void {
    const ctx = this.ctx;
    const size = this.canvas.width;
    const world = this.map.size;
    const s = size / world;
    ctx.clearRect(0, 0, size, size);
    ctx.fillStyle = 'rgba(20,24,28,0.6)';
    ctx.fillRect(0, 0, size, size);
    for (const c of this.map.covers) {
      ctx.fillStyle = COVER_COLORS[c.type] ?? '#555';
      const x = (c.pos[0] + world / 2) * s;
      const z = (c.pos[2] + world / 2) * s;
      ctx.fillRect(x - (c.size[0] * s) / 2, z - (c.size[2] * s) / 2, c.size[0] * s, c.size[2] * s);
    }
    for (const [id, b] of enemies) {
      if (now > b.until) {
        enemies.delete(id);
        continue;
      }
      ctx.fillStyle = 'rgba(255,70,60,0.9)';
      ctx.beginPath();
      ctx.arc((b.x + world / 2) * s, (b.z + world / 2) * s, 3.4, 0, Math.PI * 2);
      ctx.fill();
    }
    for (const e of uavEnemies) {
      ctx.fillStyle = 'rgba(255,120,80,0.95)';
      ctx.beginPath();
      ctx.arc((e.x + world / 2) * s, (e.z + world / 2) * s, 3.4, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = 'rgba(255,200,90,0.8)';
      ctx.stroke();
    }
    const sx = (self.x + world / 2) * s;
    const sz = (self.z + world / 2) * s;
    ctx.save();
    ctx.translate(sx, sz);
    ctx.rotate(-self.yaw);
    ctx.fillStyle = '#e8f4ff';
    ctx.beginPath();
    ctx.moveTo(0, -6);
    ctx.lineTo(4.5, 5);
    ctx.lineTo(-4.5, 5);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
  }
}
