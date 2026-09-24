import type { GameEvent, PlayerSnap } from 'shared';

interface Frame {
  at: number;
  players: PlayerSnap[];
}

interface EventEntry {
  at: number;
  e: GameEvent;
}

const WINDOW = 12;
const NUM_KEYS = ['x', 'y', 'z', 'vx', 'vy', 'vz', 'yaw', 'pitch', 'h', 'hp', 'm', 'rs', 'rl'] as const;

function lerp(a: number, b: number, k: number): number {
  return a + (b - a) * k;
}

/** 死亡回放：客户端快照 + 事件环形缓冲（~12s），按时间采样插值帧 */
export class Killcam {
  private frames: Frame[] = [];
  private events: EventEntry[] = [];

  pushSnapshot(players: PlayerSnap[], at: number): void {
    this.frames.push({ at, players: players.map((p) => ({ ...p })) });
    while (this.frames.length > 0 && at - this.frames[0].at > WINDOW) this.frames.shift();
  }

  pushEvent(e: GameEvent, at: number): void {
    this.events.push({ at, e });
    while (this.events.length > 0 && at - this.events[0].at > WINDOW) this.events.shift();
  }

  /** viewTime ∈ [deathAt - span, deathAt]；返回该时刻的插值玩家状态 */
  sample(deathAt: number, viewTime: number, span: number): PlayerSnap[] | null {
    const t = Math.max(deathAt - span, Math.min(deathAt, viewTime));
    let i0 = -1;
    for (let i = this.frames.length - 1; i >= 0; i--) {
      if (this.frames[i].at <= t) {
        i0 = i;
        break;
      }
    }
    if (i0 < 0) return null;
    const f0 = this.frames[i0];
    const f1 = this.frames[Math.min(i0 + 1, this.frames.length - 1)];
    const spanS = Math.max(1e-6, f1.at - f0.at);
    const k = f1 === f0 ? 1 : Math.max(0, Math.min(1, (t - f0.at) / spanS));
    const byId0 = new Map(f0.players.map((p) => [p.id, p]));
    return f1.players.map((p1) => {
      const p0 = byId0.get(p1.id) ?? p1;
      const out = { ...p1 };
      for (const key of NUM_KEYS) {
        const a = p0[key] as number;
        const b = p1[key] as number;
        (out as unknown as Record<string, number>)[key] = key === 'yaw' ? lerpYaw(a, b, k) : lerp(a, b, k);
      }
      out.a = k > 0.5 ? p1.a : p0.a;
      out.sl = k > 0.5 ? p1.sl : p0.sl;
      return out;
    });
  }

  get size(): number {
    return this.frames.length;
  }

  /** 返回 (fromAt, toAt] 区间内尚未消费的事件 */
  drainEvents(fromAt: number, toAt: number): GameEvent[] {
    return this.events.filter((en) => en.at > fromAt && en.at <= toAt).map((en) => en.e);
  }
}

function lerpYaw(a: number, b: number, k: number): number {
  let d = b - a;
  if (d > Math.PI) d -= Math.PI * 2;
  if (d < -Math.PI) d += Math.PI * 2;
  return a + d * k;
}
