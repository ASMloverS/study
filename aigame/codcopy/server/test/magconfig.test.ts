import { afterAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import { BTN, WEAPONS, defaultMagConfig, type MagConfig, type S2CMessage } from 'shared';
import { Room } from '../src';
import { startNetServer, type NetServerHandle } from '../src/net';

function hold(room: Room, id: number, seqStart: number, buttons: number, ticks: number): number {
  for (let i = 0; i < ticks; i++) {
    room.enqueueInput(id, { seq: seqStart + i, moveX: 0, moveZ: 0, yaw: 0, pitch: 0, buttons, slot: 0 });
    room.step();
  }
  return seqStart + ticks;
}

describe('[M14] room mag config', () => {
  it('initial mags and proportional reserve from magConfig', () => {
    const magConfig: MagConfig = { ...defaultMagConfig(), ar: 60, sg: 12 };
    const room = new Room(undefined, { seed: 41, bots: 0, magConfig });
    const p = room.addPlayer('H', false);
    expect(room.effectiveMag('ar')).toBe(60);
    expect(p.mags.ar).toBe(60);
    expect(p.reserve.ar).toBe(180);
    expect(p.mags.sg).toBe(12);
    expect(p.reserve.sg).toBe(48);
    expect(p.mags.smg).toBe(WEAPONS.smg.magSize);
    expect(p.reserve.smg).toBe(WEAPONS.smg.reserve);
  });

  it('reload caps at effectiveMag with proportional reserve', () => {
    const room = new Room(undefined, { seed: 42, bots: 0, magConfig: { ...defaultMagConfig(), ar: 10 } });
    const p = room.addPlayer('H', false);
    expect(p.reserve.ar).toBe(30);
    p.mags.ar = 2;
    hold(room, p.id, 1, BTN.RELOAD, 1);
    for (let i = 0; i < 61; i++) room.step();
    expect(p.mags.ar).toBe(10);
    expect(p.reserve.ar).toBe(22);
  });

  it('setMagConfig immediate: current mag untouched, reload cap and next spawn use new value', () => {
    const room = new Room(undefined, { seed: 43, bots: 0 });
    const p = room.addPlayer('H', false);
    expect(p.mags.ar).toBe(30);
    room.setMagConfig({ ...defaultMagConfig(), ar: 99 });
    expect(p.mags.ar).toBe(30);
    expect(room.effectiveMag('ar')).toBe(99);
    p.mags.ar = 95;
    hold(room, p.id, 1, BTN.RELOAD, 1);
    for (let i = 0; i < 80; i++) room.step();
    expect(p.mags.ar).toBe(99);
    room.spawn(p);
    expect(p.mags.ar).toBe(99);
    expect(p.reserve.ar).toBe(297);
  });

  it('bots share the same effectiveMag rule', () => {
    const room = new Room(undefined, { seed: 45, bots: 2, magConfig: { ...defaultMagConfig(), ar: 10 } });
    expect(room.players.length).toBe(2);
    for (const b of room.players) {
      expect(b.isBot).toBe(true);
      expect(b.mags.ar).toBe(10);
      expect(b.reserve.ar).toBe(30);
    }
  });

  it('lobbyState carries mags and pendingMags', () => {
    const room = new Room(undefined, { seed: 44, bots: 0 });
    const st = room.lobbyState(null, { mags: { ...defaultMagConfig(), sr: 20 } });
    expect(st.mags).toEqual(defaultMagConfig());
    expect(st.pendingMags?.sr).toBe(20);
  });
});

describe('[M14] net lobby mags (host pending, next match)', () => {
  let handle: NetServerHandle;
  const messages: S2CMessage[] = [];
  let ws: WebSocket;

  afterAll(() => {
    handle?.close();
    ws?.close();
  });

  it('host mags stay pending until resetRoom; welcome cfg carries applied mags', async () => {
    handle = startNetServer({ port: 25300 + Math.floor(Math.random() * 400), bots: 0, roomResetMs: 120, killLimit: 5, durationSec: 600 });
    ws = new WebSocket(`ws://localhost:${handle.port}`);
    ws.on('message', (d) => messages.push(JSON.parse(String(d))));
    await new Promise<void>((r) => ws.once('open', r));
    ws.send(JSON.stringify({ kind: 'join', name: 'Host' }));
    const wait = async (pred: (m: S2CMessage) => boolean, timeout = 4000): Promise<S2CMessage> => {
      const start = Date.now();
      for (;;) {
        const found = messages.find(pred);
        if (found) return found;
        if (Date.now() - start > timeout) throw new Error('timeout waiting for message');
        await new Promise((r) => setTimeout(r, 20));
      }
    };
    await wait((m) => m.kind === 'welcome');
    ws.send(JSON.stringify({ kind: 'lobby', mags: { ...defaultMagConfig(), ar: 60 } }));
    const st = await wait((m) => m.kind === 'lobbyState' && m.pendingMags?.ar === 60, 4000);
    if (st.kind !== 'lobbyState') throw new Error('unexpected');
    expect(st.mags?.ar).toBe(WEAPONS.ar.magSize);
    expect(handle.room.effectiveMag('ar')).toBe(WEAPONS.ar.magSize);

    handle.room.over = true;
    const welcome = await wait((m) => m.kind === 'welcome' && m.cfg?.mags?.ar === 60, 6000);
    expect(welcome.kind).toBe('welcome');
    expect(handle.room.effectiveMag('ar')).toBe(60);
  }, 15000);
});
