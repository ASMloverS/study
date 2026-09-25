import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import type { S2CMessage } from 'shared';
import { Room } from '../src';
import { startNetServer, type NetServerHandle } from '../src/net';

class LobbyClient {
  ws: WebSocket;
  messages: S2CMessage[] = [];

  constructor(port: number) {
    this.ws = new WebSocket(`ws://localhost:${port}`);
    this.ws.on('message', (d) => this.messages.push(JSON.parse(String(d))));
  }

  async open(): Promise<void> {
    await new Promise<void>((r) => this.ws.once('open', r));
  }

  send(m: unknown): void {
    this.ws.send(JSON.stringify(m));
  }

  /** 等待第 from 条之后首个满足 pred 的消息（避免匹配历史消息） */
  async wait(pred: (m: S2CMessage) => boolean, timeout = 4000, from = 0): Promise<S2CMessage> {
    const start = Date.now();
    for (;;) {
      const found = this.messages.slice(from).find(pred);
      if (found) return found;
      if (Date.now() - start > timeout) throw new Error('timeout waiting for message');
      await new Promise((r) => setTimeout(r, 20));
    }
  }

  latest<K extends S2CMessage['kind']>(kind: K): Extract<S2CMessage, { kind: K }> | undefined {
    for (let i = this.messages.length - 1; i >= 0; i--) {
      const m = this.messages[i];
      if (m.kind === kind) return m as Extract<S2CMessage, { kind: K }>;
    }
    return undefined;
  }

  close(): void {
    this.ws.close();
  }

  async closed(): Promise<void> {
    if (this.ws.readyState === WebSocket.CLOSED) return;
    await new Promise<void>((r) => this.ws.once('close', r));
  }
}

describe('room lobby API', () => {
  it('setBotCount adjusts bots with humans preserved and seat cap', () => {
    const room = new Room(undefined, { seed: 1, bots: 8 });
    expect(room.botCount()).toBe(8);
    room.addHuman('A');
    room.addHuman('B');
    expect(room.botCount()).toBe(6);
    expect(room.setBotCount(7)).toBe(6);
    expect(room.setBotCount(3)).toBe(3);
    expect(room.players.filter((p) => !p.isBot).length).toBe(2);
    expect(room.setBotCount(0)).toBe(0);
    expect(room.setBotCount(5)).toBe(5);
  });

  it('setBotDifficulty updates existing and future bots', () => {
    const room = new Room(undefined, { seed: 2, bots: 4, botDifficulty: 'easy' });
    room.setBotDifficulty('hard');
    for (const p of room.players) {
      expect(p.bot?.difficulty).toBe('hard');
    }
    room.setBotCount(6);
    for (const p of room.players) {
      expect(p.bot?.difficulty).toBe('hard');
    }
    expect(room.botDifficulty).toBe('hard');
  });

  it('setRules clamps and applies immediately; lobbyState shape', () => {
    const room = new Room(undefined, { seed: 3, bots: 0, killLimit: 30, durationSec: 600 });
    room.setRules(15, 6);
    expect(room.killLimit).toBe(15);
    expect(room.durationSec).toBe(360);
    expect(room.timeLeft).toBeLessThanOrEqual(360);
    const st = room.lobbyState(7, { killLimit: 20 });
    expect(st.kind).toBe('lobbyState');
    expect(st.hostId).toBe(7);
    expect(st.killLimit).toBe(15);
    expect(st.matchMinutes).toBe(6);
    expect(st.pendingKillLimit).toBe(20);
  });
});

describe('net lobby (host authority)', () => {
  let handle: NetServerHandle;
  let c1: LobbyClient;
  let c2: LobbyClient;

  beforeAll(async () => {
    handle = startNetServer({ port: 24500 + Math.floor(Math.random() * 500), bots: 7, roomResetMs: 150, killLimit: 30, durationSec: 600 });
    c1 = new LobbyClient(handle.port);
    await c1.open();
  });

  afterAll(() => {
    handle.close();
  });

  it('first joiner becomes host; state broadcast', async () => {
    c1.send({ kind: 'join', name: 'Host' });
    const welcome = await c1.wait((m) => m.kind === 'welcome');
    if (welcome.kind !== 'welcome') throw new Error('unexpected');
    const st = await c1.wait((m) => m.kind === 'lobbyState');
    if (st.kind !== 'lobbyState') throw new Error('unexpected');
    expect(st.hostId).toBe(welcome.playerId);
    expect(st.bots).toBe(7);
  }, 10000);

  it('non-host lobby messages are rejected', async () => {
    c2 = new LobbyClient(handle.port);
    await c2.open();
    c2.send({ kind: 'join', name: 'Guest' });
    await c2.wait((m) => m.kind === 'welcome');
    await c2.wait((m) => m.kind === 'lobbyState');
    const before = c2.messages.length;
    c2.send({ kind: 'lobby', bots: 0, difficulty: 'easy', killLimit: 50, matchMinutes: 15 });
    await new Promise((r) => setTimeout(r, 400));
    const st = c2.latest('lobbyState');
    expect(st?.bots).toBe(6);
    expect(st?.difficulty).toBe('mixed');
    expect(st?.killLimit).toBe(30);
    expect(c2.messages.slice(before).some((m) => m.kind === 'lobbyState')).toBe(false);
  }, 10000);

  it('host lobby: bots/difficulty immediate, rules pending; reset applies pending rules', async () => {
    const mark = c1.messages.length;
    c1.send({ kind: 'lobby', bots: 3, difficulty: 'hard', killLimit: 15, matchMinutes: 6 });
    const st = await c1.wait(
      (m) => m.kind === 'lobbyState' && m.bots === 3 && m.difficulty === 'hard' && m.pendingKillLimit === 15,
      4000,
      mark,
    );
    if (st.kind !== 'lobbyState') throw new Error('unexpected');
    expect(st.pendingMatchMinutes).toBe(6);
    expect(st.killLimit).toBe(30);

    // AI 实时增删：快照内 BOT 数 = 3（另有 2 真人）
    const snap = await c1.wait(
      (m) => m.kind === 'snapshot' && m.players.filter((p) => p.name.startsWith('BOT-')).length === 3,
    );
    if (snap.kind !== 'snapshot') throw new Error('unexpected');
    expect(snap.players.length).toBe(5);

    // 触发对局结束 → resetRoom（150ms 后）应用待生效规则并重发 welcome
    handle.room.over = true;
    const mark2 = c1.messages.length;
    const newWelcome = await c1.wait(
      (m) => m.kind === 'welcome' && m.cfg?.killLimit === 15 && m.cfg?.durationSec === 360,
      6000,
      mark2,
    );
    expect(newWelcome).toBeDefined();
    const stAfter = await c1.wait(
      (m) => m.kind === 'lobbyState' && m.hostId !== null,
      4000,
      c1.messages.findIndex((m) => m === newWelcome) + 1,
    );
    if (stAfter.kind !== 'lobbyState') throw new Error('unexpected');
    expect(stAfter.difficulty).toBe('hard');
    expect(stAfter.matchMinutes).toBe(6);
    expect(stAfter.pendingKillLimit).toBeUndefined();
  }, 15000);

  it('host transfers to earliest joiner when host leaves', async () => {
    const c2LatestWelcome = c2.latest('welcome');
    if (!c2LatestWelcome) throw new Error('unexpected');
    const mark = c2.messages.length;
    c1.close();
    await c1.closed();
    const st = await c2.wait(
      (m) => m.kind === 'lobbyState' && m.hostId === c2LatestWelcome.playerId,
      4000,
      mark,
    );
    if (st.kind !== 'lobbyState') throw new Error('unexpected');
    expect(st.hostId).toBe(c2LatestWelcome.playerId);
    c2.close();
  }, 10000);
});
