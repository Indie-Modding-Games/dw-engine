// The online versus' signaling server: a Cloudflare Worker with one Durable Object per room
// (README.md). Plain JavaScript once the types are stripped; no dependencies.
import { newCode, routeOf } from "./codes.ts";
import { Room, type Role, type RoomState, type Socket } from "./room.ts";

// The few Workers runtime types used here (no @cloudflare/workers-types dependency).
interface WebSocketLike extends Socket {
  accept(): void;
}
interface DurableObjectStateLike {
  acceptWebSocket(socket: WebSocketLike, tags: string[]): void;
  getWebSockets(tag?: string): WebSocketLike[];
  getTags(socket: WebSocketLike): string[];
  storage: {
    get(key: string): Promise<unknown>;
    put(key: string, value: unknown): Promise<void>;
    deleteAll(): Promise<void>;
    deleteAlarm(): Promise<void>;
    setAlarm(at: number): Promise<void>;
  };
}
interface Stub {
  fetch(request: Request): Promise<Response>;
}
interface Namespace {
  idFromName(name: string): unknown;
  get(id: unknown): Stub;
}
export interface Env {
  ROOMS: Namespace;
}
declare const WebSocketPair: { new (): { 0: WebSocketLike; 1: WebSocketLike } };

const HOST_ATTEMPTS = 8;

function upgraded(client: WebSocketLike): Response {
  return new Response(null, { status: 101, webSocket: client } as ResponseInit);
}

/** A socket that only hears why it cannot join, for a code that cannot be one. */
function refused(reason: string): Response {
  const pair = new WebSocketPair();
  pair[1].accept();
  pair[1].send(JSON.stringify({ type: "error", reason }));
  pair[1].close(1000, reason);
  return upgraded(pair[0]);
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("The versus signaling server speaks WebSocket only.\n", { status: 426 });
    }
    const route = routeOf(new URL(request.url).pathname);
    if (route.kind === "host") {
      for (let attempt = 0; attempt < HOST_ATTEMPTS; attempt++) {
        const code = newCode();
        const room = env.ROOMS.get(env.ROOMS.idFromName(code));
        const response = await room.fetch(new Request(`https://room/host?code=${code}`, request));
        if (response.status !== 409) return response;
      }
      return new Response("No free room code.\n", { status: 503 });
    }
    if (route.kind === "join") {
      const room = env.ROOMS.get(env.ROOMS.idFromName(route.code));
      return room.fetch(new Request("https://room/join", request));
    }
    if (route.kind === "bad-code") return refused("no-room");
    return new Response("Not found.\n", { status: 404 });
  },
};

/** One room (room.ts) on a Durable Object, its sockets kept across hibernation. */
export class RoomObject {
  #ctx: DurableObjectStateLike;
  #room: Room;

  constructor(ctx: DurableObjectStateLike) {
    this.#ctx = ctx;
    const state: RoomState = {
      accept: (socket, role) => ctx.acceptWebSocket(socket as WebSocketLike, [role]),
      sockets: (role) => ctx.getWebSockets(role),
      roleOf: (socket) => {
        const tag = ctx.getTags(socket as WebSocketLike)[0];
        return tag === "host" || tag === "guest" ? (tag as Role) : null;
      },
      get: (key) => ctx.storage.get(key),
      put: (key, value) => ctx.storage.put(key, value),
      clear: async () => {
        await ctx.storage.deleteAlarm();
        await ctx.storage.deleteAll();
      },
      setAlarm: (at) => ctx.storage.setAlarm(at),
    };
    this.#room = new Room(state);
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const pair = new WebSocketPair();
    if (url.pathname === "/host") {
      const opened = await this.#room.host(pair[1], url.searchParams.get("code") ?? "");
      if (!opened) return new Response(null, { status: 409 });
    } else {
      // A refused guest is never accepted by the object: its socket only hears why.
      const hosts = this.#ctx.getWebSockets("host").length;
      const guests = this.#ctx.getWebSockets("guest").length;
      if (hosts === 0 || guests > 0) pair[1].accept();
      await this.#room.join(pair[1]);
    }
    return upgraded(pair[0]);
  }

  async webSocketMessage(socket: WebSocketLike, message: string | ArrayBuffer): Promise<void> {
    await this.#room.message(socket, message);
  }

  async webSocketClose(socket: WebSocketLike): Promise<void> {
    await this.#room.closed(socket);
  }

  async webSocketError(socket: WebSocketLike): Promise<void> {
    await this.#room.closed(socket);
  }

  async alarm(): Promise<void> {
    await this.#room.alarm();
  }
}
