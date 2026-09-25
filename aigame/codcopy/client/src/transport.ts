import { createLocalGame, type LocalGameHandle } from 'server';
import { DEFAULT_LOADOUT, WEAPON_LIST, type Loadout } from 'shared';

export interface Session {
  send(msg: unknown): void;
  onMessage(cb: (msg: any) => void): void;
  onDisconnect(cb: () => void): void;
  readonly rtt: number | null;
  dispose(): void;
}

export function sanitizeLoadout(raw: unknown): Loadout {
  const l = raw as Loadout | undefined;
  if (l && WEAPON_LIST.includes(l.primary) && WEAPON_LIST.includes(l.secondary)) return { primary: l.primary, secondary: l.secondary };
  return { ...DEFAULT_LOADOUT };
}

export class LocalSession implements Session {
  private handle: LocalGameHandle;
  private cb: ((msg: any) => void) | null = null;
  private inbox: unknown[] = [];
  readonly rtt: number | null = null;

  constructor(opts: { name?: string; loadout?: Loadout; bots?: number; botDifficulty?: 'mixed' | 'easy' | 'normal' | 'hard'; killLimit?: number; durationSec?: number }) {
    this.handle = createLocalGame(opts);
    this.handle.transport.onMessage((m) => {
      if (this.cb) this.cb(m);
      else this.inbox.push(m);
    });
    this.send({ kind: 'join', name: opts.name ?? '玩家', loadout: sanitizeLoadout(opts.loadout) });
  }

  send(msg: unknown): void {
    this.handle.transport.send(msg);
  }

  onMessage(cb: (msg: any) => void): void {
    this.cb = cb;
    const queued = this.inbox;
    this.inbox = [];
    for (const m of queued) cb(m);
  }

  onDisconnect(): void {
    void 0;
  }

  pause(): void {
    this.handle.pause();
  }

  resume(): void {
    this.handle.resume();
  }

  dispose(): void {
    this.handle.dispose();
  }
}

export class NetSession implements Session {
  private ws: WebSocket;
  private cb: ((msg: any) => void) | null = null;
  private disconnectCb: (() => void) | null = null;
  private pingTimer: number | null = null;
  private rttValue: number | null = null;

  constructor(url: string, name: string, loadout: Loadout = { ...DEFAULT_LOADOUT }) {
    this.ws = new WebSocket(url);
    this.ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(String(ev.data));
        if (msg?.kind === 'pong') {
          this.rttValue = Math.round(performance.now() - msg.t);
          return;
        }
        this.cb?.(msg);
      } catch {
        void 0;
      }
    };
    this.ws.onopen = () => {
      this.send({ kind: 'join', name, loadout: sanitizeLoadout(loadout) });
      this.pingTimer = window.setInterval(() => {
        if (this.ws.readyState === WebSocket.OPEN) this.send({ kind: 'ping', t: performance.now(), rtt: this.rttValue ?? 0 });
      }, 2000);
    };
    this.ws.onclose = () => {
      if (this.pingTimer !== null) window.clearInterval(this.pingTimer);
      this.pingTimer = null;
      this.disconnectCb?.();
    };
  }

  get rtt(): number | null {
    return this.rttValue;
  }

  send(msg: unknown): void {
    if (this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify(msg));
  }

  onMessage(cb: (msg: any) => void): void {
    this.cb = cb;
  }

  onDisconnect(cb: () => void): void {
    this.disconnectCb = cb;
  }

  dispose(): void {
    if (this.pingTimer !== null) window.clearInterval(this.pingTimer);
    this.pingTimer = null;
    this.disconnectCb = null;
    this.ws.onclose = null;
    this.ws.close();
  }
}
