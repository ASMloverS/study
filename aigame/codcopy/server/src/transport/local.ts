export interface Transport {
  send(msg: unknown): void;
  onMessage(cb: (msg: any) => void): void;
}

export function createLocalPair(): { client: Transport; server: Transport } {
  let clientCb: ((msg: any) => void) | null = null;
  let serverCb: ((msg: any) => void) | null = null;
  const client: Transport = {
    send(msg) {
      if (serverCb) serverCb(msg);
    },
    onMessage(cb) {
      clientCb = cb;
    },
  };
  const server: Transport = {
    send(msg) {
      if (clientCb) clientCb(msg);
    },
    onMessage(cb) {
      serverCb = cb;
    },
  };
  return { client, server };
}
