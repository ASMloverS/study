import { describe, expect, it } from 'vitest';
import { BTN, type InputMsg, type S2CMessage } from 'shared';
import { LocalSession } from '../src/transport';
import { createLocalGame } from 'server';

class LocalClient {
  session: LocalSession;
  messages: S2CMessage[] = [];

  constructor(opts: { name?: string; bots?: number; botDifficulty?: 'mixed' | 'easy' | 'normal' | 'hard'; killLimit?: number; durationSec?: number }) {
    this.session = new LocalSession(opts);
    this.session.onMessage((m) => this.messages.push(m));
  }

  send(m: unknown): void {
    this.session.send(m);
  }

  async wait(pred: (m: S2CMessage) => boolean, timeout = 8000): Promise<S2CMessage> {
    const start = Date.now();
    for (;;) {
      const found = this.messages.find(pred);
      if (found) return found;
      if (Date.now() - start > timeout) throw new Error('timeout waiting for message');
      await new Promise((r) => setTimeout(r, 20));
    }
  }

  dispose(): void {
    this.session.dispose();
  }
}

function latestSnapshot(msgs: S2CMessage[]): (S2CMessage & { kind: 'snapshot' }) | null {
  for (let i = msgs.length - 1; i >= 0; i--) {
    const m = msgs[i];
    if (m.kind === 'snapshot') return m as S2CMessage & { kind: 'snapshot' };
  }
  return null;
}

describe('local session (single player path)', () => {
  it('auto-joins: welcome arrives without manual join', async () => {
    const c = new LocalClient({ name: '测试者', bots: 2, killLimit: 30, durationSec: 300 });
    try {
      const welcome = await c.wait((m) => m.kind === 'welcome');
      if (welcome.kind !== 'welcome') throw new Error('unexpected');
      expect(welcome.playerId).toBeGreaterThanOrEqual(0);
      expect(welcome.cfg?.killLimit).toBe(30);
      const snap = await c.wait((m) => m.kind === 'snapshot');
      if (snap.kind !== 'snapshot') throw new Error('unexpected');
      const me = snap.players.find((p) => p.id === welcome.playerId);
      expect(me?.name).toBe('测试者');
      expect(me?.a).toBe(true);
      expect(snap.players.length).toBe(3);
    } finally {
      c.dispose();
    }
  }, 10000);

  it('inputs are acked through the authoritative loop', async () => {
    const c = new LocalClient({ bots: 0, killLimit: 30, durationSec: 300 });
    try {
      const welcome = await c.wait((m) => m.kind === 'welcome');
      if (welcome.kind !== 'welcome') throw new Error('unexpected');
      const pid = welcome.playerId;
      const mk = (seq: number): InputMsg => ({ seq, moveX: 0, moveZ: 1, yaw: 0, pitch: 0, buttons: 0, slot: 0 });
      c.send({ kind: 'input', input: mk(1) });
      c.send({ kind: 'input', input: mk(2) });
      c.send({ kind: 'input', input: mk(3) });
      const acked = await c.wait((m) => m.kind === 'snapshot' && (m as any).acks[pid] >= 3);
      expect(acked).toBeDefined();
    } finally {
      c.dispose();
    }
  }, 10000);

  it('firing produces server hit events (join→input→combat→event closure)', async () => {
    const handle = createLocalGame({ bots: 0, killLimit: 30, durationSec: 300 });
    const messages: S2CMessage[] = [];
    handle.transport.onMessage((m) => messages.push(m));
    handle.transport.send({ kind: 'join', name: '枪手' });
    try {
      const welcome = await new Promise<S2CMessage & { kind: 'welcome' }>((resolve, reject) => {
        const start = Date.now();
        const t = setInterval(() => {
          const found = messages.find((m): m is S2CMessage & { kind: 'welcome' } => m.kind === 'welcome');
          if (found) {
            clearInterval(t);
            resolve(found);
          } else if (Date.now() - start > 5000) {
            clearInterval(t);
            reject(new Error('timeout waiting for welcome'));
          }
        }, 10);
      });
      const pid = welcome.playerId;
      const me = handle.room.players.find((p) => p.id === pid);
      expect(me).toBeDefined();
      if (!me) return;

      // 布置静止人形靶（无输入源 → 站定不反击），扫描一个 LOS 通畅的 6m 方位
      const dummy = handle.room.addPlayer('靶子', false);
      const placed = (() => {
        const my = me.st;
        for (let i = 0; i < 24; i++) {
          const ang = (i * Math.PI) / 12;
          dummy.st.x = my.x + Math.cos(ang) * 6;
          dummy.st.z = my.z + Math.sin(ang) * 6;
          dummy.st.y = my.y;
          if (handle.room.hasLineOfSight(me, dummy)) return true;
        }
        return false;
      })();
      expect(placed).toBe(true);
      const protTicksAtPlace = handle.room.tick;

      // 等待靶子重生保护自然过期（2s = 60 tick）
      await new Promise<void>((resolve) => {
        const t = setInterval(() => {
          if (handle.room.tick >= protTicksAtPlace + 61) {
            clearInterval(t);
            resolve();
          }
        }, 20);
      });

      let seq = 0;
      const fireTimer = setInterval(() => {
        const dx = dummy.st.x - me.st.x;
        const dy = dummy.st.y + dummy.st.height * 0.7 - (me.st.y + me.st.height * 0.9);
        const dz = dummy.st.z - me.st.z;
        const yaw = Math.atan2(-dx, -dz);
        const pitch = Math.atan2(dy, Math.hypot(dx, dz));
        handle.transport.send({
          kind: 'input',
          input: { seq: ++seq, moveX: 0, moveZ: 0, yaw, pitch, buttons: BTN.FIRE | BTN.ADS, slot: 0 },
        });
      }, 33);
      try {
        const hit = await new Promise<S2CMessage & { kind: 'events' }>((resolve, reject) => {
          const start = Date.now();
          const t = setInterval(() => {
            const found = messages.find(
              (m): m is S2CMessage & { kind: 'events' } =>
                m.kind === 'events' && m.events.some((e) => e.type === 'hit' && e.attackerId === pid),
            );
            if (found) {
              clearInterval(t);
              resolve(found);
            } else if (Date.now() - start > 8000) {
              clearInterval(t);
              reject(new Error('timeout waiting for hit event'));
            }
          }, 10);
        });
        expect(hit).toBeDefined();
      } finally {
        clearInterval(fireTimer);
      }
    } finally {
      handle.dispose();
    }
  }, 15000);

  it('pause freezes the world; resume continues snapshots', async () => {
    const c = new LocalClient({ bots: 0, killLimit: 30, durationSec: 300 });
    try {
      await c.wait((m) => m.kind === 'snapshot');
      c.session.pause();
      const count = c.messages.length;
      await new Promise((r) => setTimeout(r, 300));
      expect(c.messages.length).toBe(count);
      c.session.resume();
      const resumed = await c.wait(
        (m) => m.kind === 'snapshot' && c.messages.length > count,
        3000,
      );
      expect(resumed).toBeDefined();
    } finally {
      c.dispose();
    }
  }, 10000);

  it('headless stops stepping when the match is over', async () => {
    const handle = createLocalGame({ bots: 2, killLimit: 30, durationSec: 300 });
    const msgs: S2CMessage[] = [];
    handle.transport.onMessage((m) => msgs.push(m));
    try {
      await new Promise<void>(async (resolve) => {
        const start = Date.now();
        while (!msgs.some((m) => m.kind === 'snapshot')) {
          if (Date.now() - start > 5000) throw new Error('timeout waiting for snapshot');
          await new Promise((r) => setTimeout(r, 20));
        }
        resolve();
      });
      handle.room.over = true;
      const count = msgs.length;
      await new Promise((r) => setTimeout(r, 250));
      expect(msgs.length).toBe(count);
    } finally {
      handle.dispose();
    }
  }, 10000);
});
