import { createServer } from 'node:http';
import { createReadStream, existsSync, statSync } from 'node:fs';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer, WebSocket } from 'ws';
import { DEFAULT_LOADOUT, SNAPSHOT_EVERY_TICKS, TICK_RATE, WEAPON_LIST, sanitizeMagConfig, type Loadout, type MagConfig, type S2CMessage } from 'shared';
import type { BotDifficulty } from './ai/controller';
import { Room } from './game/world';

export interface NetServerOptions {
  port: number;
  bots?: number;
  difficulty?: BotDifficulty | 'mixed';
  roomResetMs?: number;
  killLimit?: number;
  durationSec?: number;
  /** [M14] 初始弹匣配置（env MAGS 或缺省默认） */
  magConfig?: MagConfig;
}

export interface NetServerHandle {
  room: Room;
  port: number;
  close: () => void;
}

interface Client {
  ws: WebSocket;
  playerId: number;
  name: string;
  loadout: Loadout;
}

export function startNetServer(opts: NetServerOptions): NetServerHandle {
  let lobbyBots = opts.bots ?? 7;
  let lobbyDifficulty = opts.difficulty ?? 'mixed';
  let pendingKillLimit: number | undefined;
  let pendingMatchMinutes: number | undefined;
  let pendingMags: MagConfig | undefined;
  const roomOpts = () => ({
    bots: lobbyBots,
    botDifficulty: lobbyDifficulty,
    killLimit: pendingKillLimit ?? opts.killLimit,
    durationSec: pendingMatchMinutes != null ? pendingMatchMinutes * 60 : opts.durationSec,
    magConfig: pendingMags ?? opts.magConfig,
  });
  let room = new Room(undefined, roomOpts());
  const distDir = fileURLToPath(new URL('../../client/dist/', import.meta.url));
  const mime: Record<string, string> = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.png': 'image/png',
    '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon',
    '.woff2': 'font/woff2',
  };
  const httpServer = createServer((req, res) => {
    if (!existsSync(distDir)) {
      res.writeHead(404);
      res.end('client/dist not built. Run: npm run build -w client');
      return;
    }
    const urlPath = decodeURIComponent((req.url ?? '/').split('?')[0]);
    let filePath = normalize(join(distDir, urlPath === '/' ? 'index.html' : urlPath.replace(/^\//, '')));
    if (!filePath.startsWith(distDir)) filePath = join(distDir, 'index.html');
    if (!existsSync(filePath) || statSync(filePath).isDirectory()) filePath = join(distDir, 'index.html');
    res.writeHead(200, { 'content-type': mime[extname(filePath)] ?? 'application/octet-stream' });
    createReadStream(filePath).pipe(res);
  });
  const wss = new WebSocketServer({ server: httpServer });
  const clients = new Set<Client>();
  let overSince = 0;

  const send = (ws: WebSocket, msg: S2CMessage) => {
    if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
  };
  const broadcast = (msg: S2CMessage) => {
    for (const c of clients) send(c.ws, msg);
  };

  /** [M10] 房主：首个真人；退出移交最早加入者 */
  const hostClient = (): Client | null => (clients.size > 0 ? [...clients][0] : null);
  const broadcastLobby = () => {
    broadcast(room.lobbyState(hostClient()?.playerId ?? null, { killLimit: pendingKillLimit, matchMinutes: pendingMatchMinutes, mags: pendingMags }));
  };

  wss.on('connection', (ws) => {
    let client: Client | null = null;
    ws.on('message', (data) => {
      let msg: any;
      try {
        msg = JSON.parse(String(data));
      } catch {
        return;
      }
      if (msg.kind === 'join' && !client) {
        const name = String(msg.name ?? 'Player').slice(0, 16) || 'Player';
        const loadout: Loadout =
          msg.loadout && WEAPON_LIST.includes(msg.loadout.primary) && WEAPON_LIST.includes(msg.loadout.secondary)
            ? { primary: msg.loadout.primary, secondary: msg.loadout.secondary }
            : DEFAULT_LOADOUT;
        const p = room.addHuman(name, loadout);
        client = { ws, playerId: p.id, name, loadout: { ...p.loadout } };
        clients.add(client);
        send(ws, { kind: 'welcome', playerId: p.id, mapName: room.map.name, cfg: { killLimit: room.killLimit, durationSec: room.durationSec, mags: room.magConfig }, loadout: { ...p.loadout } });
        send(ws, room.lobbyState(hostClient()?.playerId ?? null, { killLimit: pendingKillLimit, matchMinutes: pendingMatchMinutes, mags: pendingMags }));
      } else if (msg.kind === 'input' && client) {
        room.enqueueInput(client.playerId, msg.input);
      } else if (msg.kind === 'loadout' && client) {
        const applied = room.setLoadout(client.playerId, msg.loadout);
        client.loadout = { ...applied };
        send(ws, { kind: 'loadoutAck', loadout: applied });
      } else if (msg.kind === 'lobby' && client) {
        if (hostClient() !== client) return;
        if (typeof msg.bots === 'number') {
          lobbyBots = room.setBotCount(msg.bots);
        }
        if (typeof msg.difficulty === 'string' && ['mixed', 'easy', 'normal', 'hard'].includes(msg.difficulty)) {
          lobbyDifficulty = msg.difficulty;
          room.setBotDifficulty(msg.difficulty);
        }
        if (typeof msg.killLimit === 'number' && msg.killLimit >= 1) pendingKillLimit = Math.round(msg.killLimit);
        if (typeof msg.matchMinutes === 'number' && msg.matchMinutes >= 1) pendingMatchMinutes = Math.round(msg.matchMinutes);
        if (msg.mags && typeof msg.mags === 'object') pendingMags = sanitizeMagConfig(msg.mags);
        broadcastLobby();
      } else if (msg.kind === 'ping') {
        if (client) room.setRtt(client.playerId, Number(msg.rtt ?? 0));
        send(ws, { kind: 'pong', t: msg.t });
      }
    });
    ws.on('close', () => {
      if (client) {
        room.playerLeft(client.playerId);
        clients.delete(client);
        broadcastLobby();
      }
    });
    ws.on('error', () => ws.close());
  });

  const resetRoom = () => {
    const old = [...clients];
    room = new Room(undefined, { ...roomOpts(), bots: Math.max(0, lobbyBots - old.length) });
    for (const c of old) {
      const p = room.addHuman(c.name, c.loadout);
      c.playerId = p.id;
      send(c.ws, { kind: 'welcome', playerId: p.id, mapName: room.map.name, cfg: { killLimit: room.killLimit, durationSec: room.durationSec, mags: room.magConfig }, loadout: { ...p.loadout } });
    }
    pendingKillLimit = undefined;
    pendingMatchMinutes = undefined;
    pendingMags = undefined;
    overSince = 0;
    broadcastLobby();
  };

  let last = Date.now();
  let acc = 0;
  httpServer.listen(opts.port);
  const timer = setInterval(() => {
    const now = Date.now();
    acc += Math.min(now - last, 250);
    last = now;
    while (acc >= 1000 / TICK_RATE) {
      acc -= 1000 / TICK_RATE;
      if (room.over) {
        if (overSince === 0) overSince = now;
        else if (now - overSince > (opts.roomResetMs ?? 8000)) resetRoom();
        continue;
      }
      room.step();
      if (room.tick % SNAPSHOT_EVERY_TICKS === 0) broadcast(room.snapshot());
      const evs = room.drainEvents();
      if (evs.length > 0) broadcast({ kind: 'events', events: evs });
    }
  }, 5);

  return {
    get room() {
      return room;
    },
    port: opts.port,
    close: () => {
      clearInterval(timer);
      for (const c of clients) c.ws.close();
      wss.close();
      httpServer.close();
    },
  };
}

if (process.argv[1] && process.argv[1].endsWith('net.ts')) {
  const port = Number(process.env.PORT ?? 8080);
  const difficultyEnv = String(process.env.DIFFICULTY ?? 'mixed');
  const difficulty = (['mixed', 'easy', 'normal', 'hard'] as const).includes(difficultyEnv as any)
    ? (difficultyEnv as 'mixed' | 'easy' | 'normal' | 'hard')
    : 'mixed';
  // [M14] MAGS="30,32,75,15,6,5,12"（按 WEAPON_LIST 顺序，非法项回落默认）
  let magConfig: MagConfig | undefined;
  if (process.env.MAGS) {
    const parts = String(process.env.MAGS).split(',').map((s) => Number(s.trim()));
    const cfg = {} as MagConfig;
    WEAPON_LIST.forEach((id, i) => (cfg[id] = parts[i]));
    magConfig = sanitizeMagConfig(cfg);
  }
  const handle = startNetServer({
    port,
    bots: Number(process.env.BOTS ?? 7),
    difficulty,
    killLimit: Number(process.env.KILL_LIMIT ?? 30),
    durationSec: Number(process.env.MATCH_MINUTES ?? 10) * 60,
    magConfig,
  });
  console.log(`[codcopy] ws server listening on ws://localhost:${handle.port}`);
}
