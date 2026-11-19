// The online versus' signaling server: a Cloudflare Worker with one Durable Object per room and one
// for the public room directory (README.md). Plain JavaScript once the types are stripped; no
// dependencies.
import { newCode, normalizedCode, PROTOCOL, routeOf } from "./codes.ts";
import { Room, type HostOptions, type Role, type RoomState, type Socket } from "./room.ts";
import { DirectoryObject } from "./directory.ts";
import { allowed, type RateLimit, validCompat, validIcon, validMode, validName } from "./limits.ts";
import { turnEnabled, turnServers, type Fetcher, type IceServer, type TurnEnv, type UsageCache, type UsageReading } from "./turn.ts";

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
export interface Env extends TurnEnv {
  ROOMS: Namespace;
  DIRECTORY?: Namespace;
  /** Rooms one address may create, and how often it may read the list (wrangler.toml). */
  HOST_LIMIT?: RateLimit;
  LIST_LIMIT?: RateLimit;
  /** The TURN service's and the analytics' fetch (the tests' fake); the global fetch otherwise. */
  TURN_FETCH?: Fetcher;
  /** The clock (the tests' fake); Date.now otherwise. */
  NOW?: () => number;
}
declare const WebSocketPair: { new (): { 0: WebSocketLike; 1: WebSocketLike } };

const HOST_ATTEMPTS = 8;
/** How the worker hands a room the relay servers of its peer (never taken from a client). */
const ICE_HEADER = "X-Dwe-Ice-Servers";
const NO_STORE = { "Cache-Control": "no-store" };

function upgraded(client: WebSocketLike): Response {
  return new Response(null, { status: 101, webSocket: client } as ResponseInit);
}

function wantsSocket(request: Request): boolean {
  return request.headers.get("Upgrade") === "websocket";
}

/** One message on a socket that closes right after: the game's client speaks WebSocket only. */
function snapshot(text: string, reason: string): Response {
  const pair = new WebSocketPair();
  pair[1].accept();
  pair[1].send(text);
  pair[1].close(1000, reason);
  return upgraded(pair[0]);
}

/** A socket that only hears why it cannot go on (a code that cannot be one, a limit). */
function refused(reason: string): Response {
  return snapshot(JSON.stringify({ type: "error", reason }), reason);
}

/** The caller's address, as Cloudflare saw it ("local" with none: wrangler dev, the tests). */
function addressOf(request: Request): string {
  return request.headers.get("CF-Connecting-IP") ?? "local";
}

function directoryOf(env: Env): Stub | null {
  return env.DIRECTORY ? env.DIRECTORY.get(env.DIRECTORY.idFromName("public")) : null;
}

/** What this server can do, so a game offers only that (GET /v1/info). */
export function info(env: Env): { protocol: string; features: string[] } {
  const features = env.DIRECTORY ? ["public-rooms", "room-state"] : [];
  if (turnEnabled(env)) features.push("relay");
  return { protocol: PROTOCOL, features };
}

/** The month's TURN usage kept in the directory, so not every room asks the analytics. */
function usageCache(env: Env): UsageCache | null {
  const directory = directoryOf(env);
  if (!directory) return null;
  return {
    async get() {
      const response = await directory.fetch(new Request("https://directory/turn-usage"));
      return response.ok ? ((await response.json()) as UsageReading | null) ?? undefined : undefined;
    },
    async put(reading) {
      await directory.fetch(new Request("https://directory/turn-usage", { method: "POST", body: JSON.stringify(reading) }));
    },
  };
}

/** A request to a room `code`, with the worker's own relay servers header and no client's. */
async function roomRequest(url: string, request: Request, env: Env, code: string): Promise<Request> {
  const forwarded = new Request(url, request);
  forwarded.headers.delete(ICE_HEADER);
  const now = env.NOW ? env.NOW() : Date.now();
  const servers = await turnServers(env, code, usageCache(env), now, env.TURN_FETCH);
  if (servers.length > 0) forwarded.headers.set(ICE_HEADER, JSON.stringify(servers));
  return forwarded;
}

function newToken(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  return [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

/**
 * What a public room's host asks for (`/v1/host?public=1&name=&icon=&compat=&mode=`), checked; or
 * why it is refused. A private room takes nothing of this.
 */
function hostQuery(url: URL): { query: URLSearchParams; listed: boolean } | { refuse: string } {
  const query = new URLSearchParams();
  if (url.searchParams.get("public") !== "1") return { query, listed: false };
  const name = url.searchParams.get("name") ?? "";
  if (name && !validName(name)) return { refuse: "bad-name" };
  const icon = url.searchParams.get("icon") ?? "";
  const compat = url.searchParams.get("compat") ?? "";
  const mode = url.searchParams.get("mode") ?? "campaign";
  query.set("name", name);
  query.set("icon", icon && validIcon(icon) ? icon : "");
  query.set("compat", compat && validCompat(compat) ? compat : "");
  query.set("mode", validMode(mode) ? mode : "campaign");
  return { query, listed: true };
}

async function listRooms(request: Request, url: URL, env: Env): Promise<Response> {
  if (!(await allowed(env.LIST_LIMIT, addressOf(request)))) {
    if (wantsSocket(request)) return refused("limit");
    return Response.json({ error: "limit" }, { status: 429, headers: NO_STORE });
  }
  const directory = directoryOf(env);
  const result = directory
    ? await directory.fetch(new Request(`https://directory/rooms${url.search}`))
    : Response.json({ rooms: [] }, { headers: NO_STORE });
  if (!wantsSocket(request)) return result;
  return snapshot(await result.text(), "snapshot");
}

/**
 * The host's notice of how its room goes: POST with {"token","state"}, or (the game's client) a
 * WebSocket to the same path with them in the query, which hears {"type":"state"} or an error.
 */
async function noticeState(request: Request, url: URL, code: string, env: Env): Promise<Response> {
  let body: unknown = null;
  if (wantsSocket(request)) {
    body = { token: url.searchParams.get("token"), state: url.searchParams.get("state") };
  } else if (request.method === "POST") {
    try {
      body = await request.json();
    } catch {
      return Response.json({ error: "bad-state" }, { status: 400 });
    }
  } else {
    return new Response("Use POST.\n", { status: 405 });
  }
  const directory = directoryOf(env);
  const result = directory
    ? await directory.fetch(new Request(`https://directory/state?code=${code}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    }))
    : Response.json({ error: "no-room" }, { status: 404 });
  if (!wantsSocket(request)) return result;
  if (result.ok) return snapshot(JSON.stringify({ type: "state" }), "state");
  const { error } = (await result.json().catch(() => ({}))) as { error?: string };
  return refused(error ?? "no-room");
}

async function host(request: Request, url: URL, env: Env): Promise<Response> {
  if (!(await allowed(env.HOST_LIMIT, addressOf(request)))) return refused("limit");
  const asked = hostQuery(url);
  if ("refuse" in asked) return refused(asked.refuse);
  const { query, listed } = asked;
  if (listed) {
    query.set("ip", addressOf(request));
    query.set("token", newToken());
  }
  // The room reads its code first and whether it is public last, as in the first protocol.
  const extra = query.size > 0 ? `${query.toString()}&` : "";
  for (let attempt = 0; attempt < HOST_ATTEMPTS; attempt++) {
    const code = newCode();
    const room = env.ROOMS.get(env.ROOMS.idFromName(code));
    const asking = `https://room/host?code=${code}&${extra}public=${listed ? "1" : "0"}`;
    // The credentials name their room; a code already taken (rare) asks for new ones.
    const response = await room.fetch(await roomRequest(asking, request, env, code));
    if (response.status !== 409) return response;
  }
  return new Response("No free room code.\n", { status: 503 });
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === `/${PROTOCOL}/info`) {
      const text = JSON.stringify(info(env));
      if (wantsSocket(request)) return snapshot(text, "info");
      return new Response(text, { headers: { "Content-Type": "application/json", ...NO_STORE } });
    }
    if (request.method === "GET" && url.pathname === `/${PROTOCOL}/rooms`) return listRooms(request, url, env);
    const state = new RegExp(`^/${PROTOCOL}/rooms/([^/]+)/state$`).exec(url.pathname);
    if (state) {
      const code = normalizedCode(decodeURIComponent(state[1]));
      if (!code) return wantsSocket(request) ? refused("no-room") : Response.json({ error: "no-room" }, { status: 404 });
      return noticeState(request, url, code, env);
    }
    if (!wantsSocket(request)) {
      return new Response("The versus signaling server speaks WebSocket only.\n", { status: 426 });
    }
    const route = routeOf(url.pathname);
    if (route.kind === "host") return host(request, url, env);
    if (route.kind === "join") {
      const room = env.ROOMS.get(env.ROOMS.idFromName(route.code));
      return room.fetch(await roomRequest("https://room/join", request, env, route.code));
    }
    if (route.kind === "bad-code") return refused("no-room");
    return new Response("Not found.\n", { status: 404 });
  },
};

function iceOf(request: Request): IceServer[] | undefined {
  const header = request.headers.get(ICE_HEADER);
  if (!header) return undefined;
  try {
    const servers = JSON.parse(header);
    return Array.isArray(servers) ? servers as IceServer[] : undefined;
  } catch {
    return undefined;
  }
}

/** One room (room.ts) on a Durable Object, its sockets kept across hibernation. */
export class RoomObject {
  #ctx: DurableObjectStateLike;
  #room: Room;

  constructor(ctx: DurableObjectStateLike, env: Env) {
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
      publish: async (code, listing) => {
        const directory = directoryOf(env);
        if (!directory || !code) return;
        const response = await directory.fetch(new Request(`https://directory/register?code=${code}`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(listing),
        }));
        return response.ok ? "listed" : "refused";
      },
    };
    this.#room = new Room(state);
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    const pair = new WebSocketPair();
    const iceServers = iceOf(request);
    if (url.pathname === "/host") {
      const asked = url.searchParams;
      const options: HostOptions = {
        listed: asked.get("public") === "1",
        hostName: asked.get("name") ?? "",
        icon: asked.get("icon") ?? "",
        compat: asked.get("compat") ?? "",
        mode: asked.get("mode") ?? "campaign",
        ip: asked.get("ip") ?? "",
        token: asked.get("token") ?? "",
        iceServers,
      };
      const opened = await this.#room.host(pair[1], asked.get("code") ?? "", options);
      if (opened === false) return new Response(null, { status: 409 });
      if (opened === "limit") return refused("limit");
    } else {
      // A refused guest is never accepted by the object: its socket only hears why.
      const hosts = this.#ctx.getWebSockets("host").length;
      const guests = this.#ctx.getWebSockets("guest").length;
      if (hosts === 0 || guests > 0) pair[1].accept();
      await this.#room.join(pair[1], iceServers);
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

export { DirectoryObject };
