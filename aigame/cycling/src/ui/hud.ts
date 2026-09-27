import type { Phase, ResultRow } from '../sim/types';
import type { Track } from '../sim/track';

export interface HudView {
  speedKmh: number;
  gearLine: string;
  cadence: number;
  cadTarget: number;
  finalSprint: boolean;
  energyFrac: number;
  gradientPct: number;
  remainingKm: number;
  position: number;
  fieldSize: number;
  progress: number;
  countdown: number | null;
  phase: Phase;
  results: ResultRow[];
}

export class Hud {
  private el = new Map<string, HTMLElement>();
  private heights: number[] = [];
  private zones: readonly number[] = [];
  private profileLength = 1;
  private prevKmh = 0;
  private punchT: ReturnType<typeof setTimeout> | null = null;

  constructor() {
    for (const id of ['speed', 'gear', 'cad-gauge', 'gradient', 'position', 'remaining', 'energy-bar', 'energy-text', 'elev', 'countdown', 'results']) {
      this.el.set(id, document.querySelector(`#${id}`) as HTMLElement);
    }
  }

  setProfile(track: Track, zones: readonly number[]): void {
    const n = 240;
    this.heights = [];
    for (let i = 0; i < n; i++) this.heights.push(track.sampleAt((i / (n - 1)) * track.length).y);
    this.zones = zones;
    this.profileLength = track.length;
  }

  flashPickup(text: string): void {
    const host = document.querySelector('#hud')!;
    const el = document.createElement('div');
    el.className = 'pickup-float';
    el.textContent = text;
    host.appendChild(el);
    setTimeout(() => el.remove(), 900);
    const bar = this.el.get('energy-bar')!;
    bar.classList.add('flash');
    setTimeout(() => bar.classList.remove('flash'), 250);
  }

  clearResults(): void {
    this.el.get('results')!.style.display = 'none';
  }

  update(v: HudView): void {
    const g = (id: string) => this.el.get(id)!;
    g('speed').textContent = `${v.speedKmh.toFixed(0)} km/h`;
    const acc = v.speedKmh - this.prevKmh;
    this.prevKmh = v.speedKmh;
    if (acc > 0.8) {
      g('speed').classList.add('punch');
      if (this.punchT) clearTimeout(this.punchT);
      this.punchT = setTimeout(() => g('speed').classList.remove('punch'), 160);
    }
    g('gear').textContent = v.gearLine;
    g('gradient').textContent = `坡度 ${v.gradientPct.toFixed(1)}%`;
    g('position').textContent = `第 ${v.position} / ${v.fieldSize} 位`;
    g('remaining').textContent = `${v.finalSprint ? '最后' : '剩余'} ${v.remainingKm.toFixed(0)} km`;
    g('remaining').classList.toggle('final', v.finalSprint);
    g('energy-bar').style.width = `${Math.max(0, v.energyFrac * 100).toFixed(1)}%`;
    g('energy-text').textContent = `体力 ${(v.energyFrac * 100).toFixed(0)}%`;
    this.drawGauge(v.cadence, v.cadTarget);
    this.drawElev(v.progress);
    g('countdown').textContent = v.countdown === null ? '' : v.countdown > 0 ? String(v.countdown) : 'GO!';
    if (v.phase === 'finished') this.showResults(v.results);
  }

  private drawGauge(cad: number, target: number): void {
    const canvas = this.el.get('cad-gauge') as HTMLCanvasElement;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const w = canvas.width, h = canvas.height;
    const cx = w / 2, cy = h - 10, r = Math.min(w / 2 - 8, h - 16);
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.lineWidth = 7;
    ctx.beginPath();
    ctx.arc(cx, cy, r, Math.PI, 0);
    ctx.stroke();
    const MAX = 240;
    const ang = (v: number) => Math.PI + (Math.min(Math.max(v, 0), MAX) / MAX) * Math.PI;
    ctx.strokeStyle = '#4dd2ff';
    ctx.lineWidth = 3;
    for (let t = 0; t <= MAX; t += 60) {
      const a = ang(t);
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(a) * (r - 6), cy + Math.sin(a) * (r - 6));
      ctx.lineTo(cx + Math.cos(a) * (r + 4), cy + Math.sin(a) * (r + 4));
      ctx.stroke();
    }
    const ta = ang(target);
    ctx.strokeStyle = '#ff5a5a';
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(ta) * (r - 8), cy + Math.sin(ta) * (r - 8));
    ctx.lineTo(cx + Math.cos(ta) * (r + 6), cy + Math.sin(ta) * (r + 6));
    ctx.stroke();
    const ca = ang(cad);
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(ca) * (r - 10), cy + Math.sin(ca) * (r - 10));
    ctx.stroke();
    ctx.fillStyle = '#ffffff';
    ctx.font = 'bold 13px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(`${Math.round(cad)} / ${Math.round(target)}`, cx, cy - 4);
  }

  private drawElev(progress: number): void {
    const canvas = this.el.get('elev') as HTMLCanvasElement;
    const ctx = canvas.getContext('2d');
    if (!ctx || this.heights.length === 0) return;
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    const min = Math.min(...this.heights);
    const span = Math.max(...this.heights) - min || 1;
    ctx.strokeStyle = '#8fd0ff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    this.heights.forEach((yv, i) => {
      const x = (i / (this.heights.length - 1)) * w;
      const y = h - 5 - ((yv - min) / span) * (h - 10);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();
    ctx.fillStyle = '#4dd2ff';
    for (const lo of this.zones) {
      ctx.fillRect((lo / this.profileLength) * w - 1, 0, 2, h);
    }
    ctx.fillStyle = '#ffd54a';
    ctx.fillRect(progress * w - 1, 0, 2, h);
  }

  private showResults(rows: ResultRow[]): void {
    const box = this.el.get('results')!;
    box.style.display = 'block';
    box.innerHTML = `<h2>比赛结果</h2>` + rows.map((r, i) =>
      `<div class="row${r.id === 0 ? ' me' : ''}"><span>${i + 1}. ${r.name}</span><span>${r.time.toFixed(1)}s</span><span>${r.avgPower.toFixed(0)}W</span></div>`
    ).join('') + `<p>按 R 重新开始</p>`;
  }
}
