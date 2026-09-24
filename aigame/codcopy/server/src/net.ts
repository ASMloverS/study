import { createServer } from 'node:http';
import { createReadStream, existsSync, statSync } from 'node:fs';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer, WebSocket } from 'ws';
import { SNAPSHOT_EVERY_TICKS, TICK_RATE, type S2CMessage } from 'shared';
import type { BotDifficulty } from './ai/controller';
import { Room } from './game/world';

export interface NetServerOptions {
  port: number;
  bots?: number;
  difficulty?: BotDifficulty | 'mixed';
  roomResetMs?: number;
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
}

export function startNetServer(opts: NetServerOptions): NetServerHandle {
  let room = new Room(undefined, { bots: opts.bots ?? 7, botDifficulty: opts.difficulty ?? 'mixed' });
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
        const p = room.addHuman(name);
        client = { ws, playerId: p.id, name };
        clients.add(client);
        send(ws, { kind: 'welcome', playerId: p.id, mapName: room.map.name });
      } else if (msg.kind === 'input' && client) {
        room.enqueueInput(client.playerId, msg.input);
      } else if (msg.kind === 'ping') {
        if (client) room.setRtt(client.playerId, Number(msg.rtt ?? 0));
        send(ws, { kind: 'pong', t: msg.t });
      }
    });
    ws.on('close', () => {
      if (client) {
        room.playerLeft(client.playerId);
        clients.delete(client);
      }
    });
    ws.on('error', () => ws.close());
  });

  const resetRoom = () => {
    const old = [...clients];
    room = new Room(undefined, { bots: Math.max(0, opts.bots ?? 7 - old.length), botDifficulty: opts.difficulty ?? 'mixed' });
    for (const c of old) {
      const p = room.addHuman(c.name);
      c.playerId = p.id;
      send(c.ws, { kind: 'welcome', playerId: p.id, mapName: room.map.name });
    }
    overSince = 0;
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
  const handle = startNetServer({ port, bots: Number(process.env.BOTS ?? 7) });
  console.log(`[codcopy] ws server listening on ws://localhost:${handle.port}`);
}
