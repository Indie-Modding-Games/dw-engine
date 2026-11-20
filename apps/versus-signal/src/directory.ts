// The public room directory: one Durable Object that keeps the listed rooms (README.md, "Public
// rooms"). The rooms register, update and remove themselves; their hosts tell it how the room goes
// once its peers are connected (POST /v1/rooms/<code>/state, with the token the host got).
import { MAX_OPEN_ROOMS, OPEN_ROOMS_PER_IP } from "./limits.ts";
import type { DirectoryEntry, RoomListing, RoomPhase } from "./room.ts";
import { MAX_SEATS } from "./seats.ts";

/** An open room leaves the list with its room (5 minutes); this is only the fallback. */
export const OPEN_MAX_AGE_MS = 5 * 60 * 1000;
/** A room past open lives this long after its host's last notice, then leaves by itself. */
export const STATE_MAX_AGE_MS = 10 * 60 * 1000;
/** The most entries kept at all (open or not); the oldest go first. */
const MAX_ENTRIES = 200;

/** What the host may tell the directory: how the room goes, or that it closed. */
export const NOTICES = ["ready", "playing", "closed"] as const;
export type Notice = (typeof NOTICES)[number];

interface DirectoryState {
  storage: {
    get(key: string): Promise<unknown>;
    put(key: string, value: unknown): Promise<void>;
  };
}

function live(entry: DirectoryEntry, now: number): boolean {
  // A room of more than two seats stays open while its host waits, refreshing its entry.
  if (entry.phase === "open") return now - (entry.updatedAt ?? entry.createdAt) < OPEN_MAX_AGE_MS;
  return now - (entry.updatedAt ?? entry.createdAt) < STATE_MAX_AGE_MS;
}

/** A listing without what only the directory keeps (the host's token and address). */
function shown(entry: DirectoryEntry): RoomListing {
  const { token: _token, ip: _ip, ...listing } = entry;
  return listing;
}

function validEntry(entry: unknown, code: string): entry is DirectoryEntry {
  if (typeof entry !== "object" || entry === null) return false;
  const e = entry as DirectoryEntry;
  return e.joinCode === code && e.roomId === code &&
    Number.isInteger(e.capacity) && e.capacity >= 2 && e.capacity <= MAX_SEATS &&
    Number.isInteger(e.players) && e.players >= 1 && e.players <= e.capacity &&
    typeof e.name === "string" && typeof e.message === "string" &&
    e.name.length <= 48 && e.message.length <= 120 &&
    ["open", "connected", "ready", "playing"].includes(e.phase) &&
    typeof e.token === "string" && typeof e.ip === "string" &&
    typeof e.hostName === "string" && typeof e.compat === "string";
}

/** Serializes the rooms' registration and the hosts' notices; answers only public metadata. */
export class DirectoryObject {
  #ctx: DirectoryState;
  #now: () => number;

  constructor(ctx: DirectoryState, _env?: unknown, now: () => number = Date.now) {
    this.#ctx = ctx;
    this.#now = now;
  }

  async #entries(): Promise<DirectoryEntry[]> {
    const stored = await this.#ctx.storage.get("rooms");
    const now = this.#now();
    return (Array.isArray(stored) ? stored as DirectoryEntry[] : []).filter((entry) => live(entry, now));
  }

  /** The month's TURN usage as the worker last read it (turn.ts, UsageCache): kept, not judged. */
  async #usage(request: Request): Promise<Response> {
    if (request.method === "GET") return Response.json((await this.#ctx.storage.get("turn-usage")) ?? null);
    if (request.method !== "POST") return new Response("Not found.\n", { status: 404 });
    const reading = await request.json().catch(() => null) as Record<string, unknown> | null;
    if (!reading || typeof reading.month !== "string" || typeof reading.egressBytes !== "number" ||
      typeof reading.at !== "number") {
      return new Response("Invalid reading.\n", { status: 400 });
    }
    await this.#ctx.storage.put("turn-usage", { month: reading.month, egressBytes: reading.egressBytes, at: reading.at });
    return new Response(null, { status: 204 });
  }

  async #save(entries: DirectoryEntry[]): Promise<void> {
    await this.#ctx.storage.put("rooms", entries.slice(-MAX_ENTRIES));
  }

  async fetch(request: Request): Promise<Response> {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/rooms") return this.#list(url);
    if (url.pathname === "/turn-usage") return this.#usage(request);
    if (request.method !== "POST") return new Response("Not found.\n", { status: 404 });
    const code = url.searchParams.get("code");
    if (!code) return new Response("Missing room code.\n", { status: 400 });
    let body: unknown;
    try {
      body = await request.json();
    } catch {
      return new Response("Invalid JSON.\n", { status: 400 });
    }
    if (url.pathname === "/register") return this.#register(code, body);
    if (url.pathname === "/state") return this.#notice(code, body);
    return new Response("Not found.\n", { status: 404 });
  }

  /**
   * The rooms a game may join or see: of its compatibility key (all without one), not playing
   * unless their game takes players (a seated room's game in progress with a free seat, `waiting`),
   * oldest first, MAX_OPEN_ROOMS at most. Connected and ready rooms show as full.
   */
  async #list(url: URL): Promise<Response> {
    const stored = await this.#ctx.storage.get("rooms");
    const entries = await this.#entries();
    if (Array.isArray(stored) && entries.length !== stored.length) await this.#save(entries);
    const compat = url.searchParams.get("compat");
    const rooms = entries
      .filter((entry) =>
        (entry.phase !== "playing" || entry.state === "waiting") && (!compat || entry.compat === compat)
      )
      .sort((a, b) => a.createdAt - b.createdAt)
      .slice(0, MAX_OPEN_ROOMS)
      .map(shown);
    return Response.json({ rooms }, { headers: { "Cache-Control": "no-store" } });
  }

  /**
   * A room lists itself (a new open one), updates its entry, or (null) leaves. A new room is
   * refused (409) while its host's address has an open room already, or the list is full.
   */
  async #register(code: string, body: unknown): Promise<Response> {
    const entries = await this.#entries();
    const current = entries.find((entry) => entry.joinCode === code);
    const others = entries.filter((entry) => entry.joinCode !== code);
    if (body === null) {
      await this.#save(others);
      return new Response(null, { status: 204 });
    }
    if (!validEntry(body, code)) return new Response("Invalid room listing.\n", { status: 400 });
    if (!current) {
      const open = others.filter((entry) => entry.phase === "open");
      if (open.length >= MAX_OPEN_ROOMS) return new Response("The list is full.\n", { status: 409 });
      if (body.ip && open.filter((entry) => entry.ip === body.ip).length >= OPEN_ROOMS_PER_IP) {
        return new Response("This address has an open room already.\n", { status: 409 });
      }
    }
    // A game under way stays one: a seated room's later counts do not take it back to the lobby
    // (only the room's own word that its game takes players lists it again).
    others.push(
      current?.phase === "playing" && body.phase !== "playing" ? { ...body, phase: "playing", state: "full" } : body,
    );
    await this.#save(others);
    return new Response(null, { status: 204 });
  }

  /** The host's notice: its token must be the room's. Ready and playing update it; closed removes it. */
  async #notice(code: string, body: unknown): Promise<Response> {
    const { token, state } = (typeof body === "object" && body !== null ? body : {}) as {
      token?: unknown;
      state?: unknown;
    };
    if (typeof state !== "string" || !(NOTICES as readonly string[]).includes(state)) {
      return Response.json({ error: "bad-state" }, { status: 400 });
    }
    const entries = await this.#entries();
    const entry = entries.find((room) => room.joinCode === code);
    if (!entry) return Response.json({ error: "no-room" }, { status: 404 });
    if (typeof token !== "string" || !token || token !== entry.token) {
      return Response.json({ error: "bad-token" }, { status: 403 });
    }
    // Ready and playing come after a guest arrived (the room counts it).
    if (state !== "closed" && entry.players < 2) return Response.json({ error: "bad-state" }, { status: 409 });
    const others = entries.filter((room) => room !== entry);
    if (state !== "closed") {
      const phase: RoomPhase = state === "ready" ? "ready" : "playing";
      // A game its seated room already lists as one to join keeps that (the two may cross).
      const keep = phase === "playing" && entry.phase === "playing";
      others.push({ ...entry, phase, state: keep ? entry.state : "full", updatedAt: this.#now() });
    }
    await this.#save(others);
    return new Response(null, { status: 204 });
  }
}
