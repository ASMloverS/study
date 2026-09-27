import { DEFAULT_LOADOUT, MAPS, SNAPSHOT_EVERY_TICKS, TICK_RATE, WEAPON_LIST, type Loadout, type MagConfig } from 'shared';
import type { BotDifficulty } from './ai/controller';
import { Room } from './game/world';
import { createLocalPair, type Transport } from './transport/local';

export interface LocalGameHandle {
  transport: Transport;
  room: Room;
  pause: () => void;
  resume: () => void;
  dispose: () => void;
}

export function createLocalGame(
  opts: { bots?: number; seed?: number; botDifficulty?: BotDifficulty | 'mixed'; killLimit?: number; durationSec?: number; magConfig?: MagConfig } = {},
): LocalGameHandle {
  const room = new Room(MAPS.warehouse, {
    bots: opts.bots ?? 7,
    seed: opts.seed,
    botDifficulty: opts.botDifficulty,
    killLimit: opts.killLimit,
    durationSec: opts.durationSec,
    magConfig: opts.magConfig,
  });
  const { client, server } = createLocalPair();
  let joinedId = -1;

  server.onMessage((msg: any) => {
    if (msg.kind === 'join') {
      const loadout: Loadout =
        msg.loadout && WEAPON_LIST.includes(msg.loadout.primary) && WEAPON_LIST.includes(msg.loadout.secondary)
          ? { primary: msg.loadout.primary, secondary: msg.loadout.secondary }
          : DEFAULT_LOADOUT;
      const p = room.addPlayer(String(msg.name || 'Player').slice(0, 16) || 'Player', false, 'normal', loadout);
      joinedId = p.id;
      server.send({ kind: 'welcome', playerId: p.id, mapName: room.map.name, cfg: { killLimit: room.killLimit, durationSec: room.durationSec, mags: room.magConfig }, loadout: { ...p.loadout } });
      server.send(room.lobbyState(joinedId));
    } else if (msg.kind === 'input') {
      room.enqueueInput(joinedId, msg.input);
    } else if (msg.kind === 'loadout' && joinedId >= 0) {
      server.send({ kind: 'loadoutAck', loadout: room.setLoadout(joinedId, msg.loadout) });
    } else if (msg.kind === 'lobby' && joinedId >= 0) {
      if (typeof msg.bots === 'number') room.setBotCount(msg.bots);
      if (typeof msg.difficulty === 'string') room.setBotDifficulty(msg.difficulty);
      if (typeof msg.killLimit === 'number' && typeof msg.matchMinutes === 'number') {
        room.setRules(msg.killLimit, msg.matchMinutes);
      }
      if (msg.mags && typeof msg.mags === 'object') room.setMagConfig(msg.mags);
      server.send(room.lobbyState(joinedId));
    }
  });

  let last = Date.now();
  let acc = 0;
  const tickOnce = () => {
    const now = Date.now();
    acc += Math.min(now - last, 250);
    last = now;
    while (acc >= 1000 / TICK_RATE) {
      acc -= 1000 / TICK_RATE;
      if (room.over) continue;
      room.step();
      if (room.tick % SNAPSHOT_EVERY_TICKS === 0) server.send(room.snapshot());
      const evs = room.drainEvents();
      if (evs.length > 0) server.send({ kind: 'events', events: evs });
    }
  };
  let timer: ReturnType<typeof setInterval> | null = setInterval(tickOnce, 5);

  return {
    transport: client,
    room,
    pause: () => {
      if (timer !== null) {
        clearInterval(timer);
        timer = null;
      }
    },
    resume: () => {
      if (timer === null) {
        last = Date.now();
        acc = 0;
        timer = setInterval(tickOnce, 5);
      }
    },
    dispose: () => {
      if (timer !== null) clearInterval(timer);
      timer = null;
    },
  };
}
