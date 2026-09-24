import { MAPS, SNAPSHOT_EVERY_TICKS, TICK_RATE } from 'shared';
import type { BotDifficulty } from './ai/controller';
import { Room } from './game/world';
import { createLocalPair, type Transport } from './transport/local';

export interface LocalGameHandle {
  transport: Transport;
  room: Room;
  dispose: () => void;
}

export function createLocalGame(
  opts: { bots?: number; seed?: number; botDifficulty?: BotDifficulty | 'mixed'; killLimit?: number; durationSec?: number } = {},
): LocalGameHandle {
  const room = new Room(MAPS.warehouse, {
    bots: opts.bots ?? 7,
    seed: opts.seed,
    botDifficulty: opts.botDifficulty,
    killLimit: opts.killLimit,
    durationSec: opts.durationSec,
  });
  const { client, server } = createLocalPair();
  let joinedId = -1;

  server.onMessage((msg: any) => {
    if (msg.kind === 'join') {
      const p = room.addPlayer(String(msg.name || 'Player').slice(0, 16) || 'Player', false);
      joinedId = p.id;
      server.send({ kind: 'welcome', playerId: p.id, mapName: room.map.name, cfg: { killLimit: room.killLimit, durationSec: room.durationSec } });
    } else if (msg.kind === 'input') {
      room.enqueueInput(joinedId, msg.input);
    }
  });

  let last = Date.now();
  let acc = 0;
  const timer = setInterval(() => {
    const now = Date.now();
    acc += Math.min(now - last, 250);
    last = now;
    while (acc >= 1000 / TICK_RATE) {
      acc -= 1000 / TICK_RATE;
      room.step();
      if (room.tick % SNAPSHOT_EVERY_TICKS === 0) server.send(room.snapshot());
      const evs = room.drainEvents();
      if (evs.length > 0) server.send({ kind: 'events', events: evs });
    }
  }, 5);

  return {
    transport: client,
    room,
    dispose: () => clearInterval(timer),
  };
}
