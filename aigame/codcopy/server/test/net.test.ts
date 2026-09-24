import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import type { InputMsg, S2CMessage } from 'shared';
import { Room } from '../src';
import { startNetServer, type NetServerHandle } from '../src/net';

class TestClient {
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

  async wait(pred: (m: S2CMessage) => boolean, timeout = 4000): Promise<S2CMessage> {
    const start = Date.now();
    for (;;) {
      const found = this.messages.find(pred);
      if (found) return found;
      if (Date.now() - start > timeout) throw new Error('timeout waiting for message');
      await new Promise((r) => setTimeout(r, 20));
    }
  }

  async close(): Promise<void> {
    const closed = new Promise<void>((r) => this.ws.once('close', r));
    this.ws.close();
    await closed;
  }
}

describe('input validation', () => {
  it('drops duplicate/stale seq and non-finite fields, clamps values', () => {
    const room = new Room(undefined, { seed: 1, bots: 0 });
    const p = room.addPlayer('H', false);
    const mk = (seq: number, patch: Partial<InputMsg> = {}): InputMsg => ({
      seq,
      moveX: 0,
      moveZ: 0,
      yaw: 0,
      pitch: 0,
      buttons: 0,
      slot: 0,
      ...patch,
    });
    room.enqueueInput(p.id, mk(5, { moveX: 5, pitch: 9 }));
    room.enqueueInput(p.id, mk(5));
    room.enqueueInput(p.id, mk(4));
    room.enqueueInput(p.id, mk(6, { moveX: Number.NaN }));
    room.enqueueInput(p.id, mk(7, { buttons: 999 }));
    expect(p.inputQueue.length).toBe(2);
    const first = p.inputQueue[0];
    expect(first.seq).toBe(5);
    expect(first.moveX).toBe(1);
    expect(first.pitch).toBe(1.55);
    expect(p.inputQueue[1].buttons).toBe(999 & 511);
  });
});

describe('room seat management', () => {
  it('human replaces a bot when full, leaving refills with a bot', () => {
    const room = new Room(undefined, { seed: 2, bots: 8 });
    expect(room.players.length).toBe(8);
    const h1 = room.addHuman('A');
    expect(room.players.length).toBe(8);
    expect(room.players.filter((p) => p.isBot).length).toBe(7);
    expect(room.players.some((p) => p.id === h1.id)).toBe(true);
    const h2 = room.addHuman('B');
    expect(room.players.filter((p) => p.isBot).length).toBe(6);
    room.playerLeft(h1.id);
    expect(room.players.length).toBe(8);
    expect(room.players.filter((p) => p.isBot).length).toBe(7);
    expect(room.players.some((p) => p.id === h2.id)).toBe(true);
  });
});

describe('ws server', () => {
  let handle: NetServerHandle;
  let c1: TestClient;

  beforeAll(async () => {
    handle = startNetServer({ port: 23500 + Math.floor(Math.random() * 500), bots: 7 });
    c1 = new TestClient(handle.port);
    await c1.open();
  });

  afterAll(() => {
    handle.close();
  });

  it('join, welcome, snapshots flow and inputs are acked', async () => {
    c1.send({ kind: 'join', name: 'Tester' });
    const welcome = await c1.wait((m) => m.kind === 'welcome');
    expect(welcome).toBeDefined();
    if (welcome.kind !== 'welcome') return;
    const snap1 = await c1.wait((m) => m.kind === 'snapshot');
    if (snap1.kind !== 'snapshot') return;
    expect(snap1.players.length).toBe(8);
    expect(snap1.players.some((p) => p.name === 'Tester')).toBe(true);

    const mk = (seq: number): InputMsg => ({ seq, moveX: 0, moveZ: 1, yaw: 0, pitch: 0, buttons: 0, slot: 0 });
    c1.send({ kind: 'input', input: mk(1) });
    c1.send({ kind: 'input', input: mk(2) });
    const acked = await c1.wait((m) => m.kind === 'snapshot' && (m as any).acks[welcome.playerId] >= 2);
    expect(acked).toBeDefined();
  }, 10000);

  it('second human replaces a bot; disconnect refills', async () => {
    const c2 = new TestClient(handle.port);
    await c2.open();
    c2.send({ kind: 'join', name: 'Second' });
    await c2.wait((m) => m.kind === 'welcome');
    const snap = await c2.wait((m) => m.kind === 'snapshot' && m.players.some((p) => p.name === 'Second'));
    if (snap.kind !== 'snapshot') return;
    expect(snap.players.length).toBe(8);
    expect(snap.players.filter((p) => p.name.startsWith('BOT-')).length).toBe(6);
    await c2.close();
    const refilled = await c1.wait((m) => {
      if (m.kind !== 'snapshot') return false;
      return m.players.filter((p) => p.name.startsWith('BOT-')).length === 7 && !m.players.some((p) => p.name === 'Second');
    });
    expect(refilled).toBeDefined();
  }, 10000);
});
