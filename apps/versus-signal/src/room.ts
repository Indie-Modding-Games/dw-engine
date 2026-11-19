// One room: a host, then a guest, and the messages between them until both are connected. The
// Durable Object (worker.ts) gives it the room's sockets and storage; the tests give it fakes.
import type { IceServer } from "./turn.ts";

/** How long a room lives at most: the host waits for a guest and both open their route. */
export const ROOM_LIFETIME_MS = 5 * 60 * 1000;
/** Once a peer says it is connected, how long the other one has to say so too. */
export const DONE_GRACE_MS = 10 * 1000;
/** A message over this is not an offer, an answer or a candidate. */
export const MAX_MESSAGE = 64 * 1024;

export type Role = "host" | "guest";

export interface Socket {
  send(text: string): void;
  close(code?: number, reason?: string): void;
}

/** What the directory answers a room that asks to be listed: listed, or refused (a limit). */
export type Published = "listed" | "refused";

/** The parts of a Durable Object's state the room uses. */
export interface RoomState {
  accept(socket: Socket, role: Role): void;
  sockets(role: Role): Socket[];
  roleOf(socket: Socket): Role | null;
  get(key: string): Promise<unknown>;
  put(key: string, value: unknown): Promise<void>;
  clear(): Promise<void>;
  setAlarm(at: number): Promise<void>;
  /** Lists, updates or (null) removes the room in the public directory. */
  publish?(code: string, listing: DirectoryEntry | null): Promise<Published | void>;
}

/**
 * The room's state as the directory and the game name it: open (waiting for a guest), connected
 * (both in, opening their route), ready (both picked what they play), playing (hidden from the
 * list). A closed room leaves the directory.
 */
export type RoomPhase = "open" | "connected" | "ready" | "playing";

/** A room as the public list shows it (GET /v1/rooms). */
export interface RoomListing {
  roomId: string;
  joinCode: string;
  name: string;
  message: string;
  players: number;
  capacity: number;
  createdAt: number;
  /** The first protocol's two states, kept for its readers: waiting (open) or full. */
  state: "waiting" | "full";
  phase: RoomPhase;
  /** The host's nickname and profile icon ("" when it gave none). */
  hostName: string;
  icon: string;
  /** Games only see rooms of their own compatibility key (protocol and content). */
  compat: string;
  mode: string;
  updatedAt: number;
}

/** A listing plus what only the directory keeps: the host's token and address. */
export interface DirectoryEntry extends RoomListing {
  token: string;
  ip: string;
}

/** What a host asks for its room besides the code (`/v1/host?...`, checked by the worker). */
export interface HostOptions {
  listed?: boolean;
  hostName?: string;
  icon?: string;
  compat?: string;
  mode?: string;
  ip?: string;
  /** Relay servers for this peer (turn.ts), sent with `room`; none without TURN. */
  iceServers?: IceServer[];
  /** The host's token for its state notices (POST /v1/rooms/<code>/state); listed rooms only. */
  token?: string;
}

export type ErrorReason = "no-room" | "full" | "expired" | "left" | "bad-code" | "limit" | "bad-name";

function message(value: object): string {
  return JSON.stringify(value);
}

function reject(socket: Socket, reason: ErrorReason): void {
  socket.send(message({ type: "error", reason }));
  socket.close(1000, reason);
}

/** Adds the relay servers to a message, only when there are some (the first protocol had none). */
function withIce(value: Record<string, unknown>, iceServers: IceServer[] | undefined): Record<string, unknown> {
  return iceServers && iceServers.length > 0 ? { ...value, iceServers } : value;
}

export class Room {
  #state: RoomState;
  #now: () => number;

  constructor(state: RoomState, now: () => number = Date.now) {
    this.#state = state;
    this.#now = now;
  }

  /**
   * A host opens the room with its code: true when it opened, false when the code is taken (the
   * worker picks another), "limit" when the directory refused to list it (the socket is untouched
   * and the room forgotten). Only a room its host asked to be public goes in the directory: a
   * private room's code is never listed.
   */
  async host(socket: Socket, code: string, options: boolean | HostOptions = false): Promise<boolean | "limit"> {
    const asked: HostOptions = typeof options === "boolean" ? { listed: options } : options;
    if (await this.#state.get("opened")) return false;
    await this.#state.put("opened", this.#now());
    await this.#state.setAlarm(this.#now() + ROOM_LIFETIME_MS);
    let token: string | undefined;
    if (asked.listed) {
      token = asked.token ?? "";
      const entry = this.#entry(code, asked, token);
      if ((await this.#state.publish?.(code, entry)) === "refused") {
        await this.#state.clear();
        return "limit";
      }
      await this.#state.put("listing", entry);
    }
    this.#state.accept(socket, "host");
    const opened: Record<string, unknown> = { type: "room", code };
    if (token) opened.token = token;
    socket.send(message(withIce(opened, asked.iceServers)));
    return true;
  }

  #entry(code: string, asked: HostOptions, token: string): DirectoryEntry {
    const now = this.#now();
    return {
      roomId: code,
      joinCode: code,
      name: "Cooperative room",
      message: "Waiting for players",
      players: 1,
      capacity: 2,
      createdAt: now,
      state: "waiting",
      phase: "open",
      hostName: asked.hostName ?? "",
      icon: asked.icon ?? "",
      compat: asked.compat ?? "",
      mode: asked.mode ?? "campaign",
      updatedAt: now,
      token,
      ip: asked.ip ?? "",
    };
  }

  /** A guest joins: the room must have its host and no guest yet. */
  async join(socket: Socket, iceServers?: IceServer[]): Promise<void> {
    const hosts = this.#state.sockets("host");
    if (hosts.length === 0) {
      reject(socket, "no-room");
      return;
    }
    if (this.#state.sockets("guest").length > 0) {
      reject(socket, "full");
      return;
    }
    this.#state.accept(socket, "guest");
    const listing = (await this.#state.get("listing")) as DirectoryEntry | undefined;
    if (listing) {
      const joined: DirectoryEntry = {
        ...listing,
        players: 2,
        state: "full",
        phase: "connected",
        message: "Connected",
        updatedAt: this.#now(),
      };
      await this.#state.put("listing", joined);
      await this.#state.publish?.(listing.joinCode, joined);
    }
    socket.send(message(withIce({ type: "joined" }, iceServers)));
    for (const host of hosts) host.send(message({ type: "peer" }));
  }

  /** What a peer sends: something for the other one, or that it is connected. */
  async message(socket: Socket, text: string | ArrayBuffer): Promise<void> {
    if (typeof text !== "string" || text.length > MAX_MESSAGE) return;
    const role = this.#state.roleOf(socket);
    if (!role) return;
    let parsed: unknown;
    try {
      parsed = JSON.parse(text);
    } catch {
      return;
    }
    if (typeof parsed !== "object" || parsed === null) return;
    const { type, data } = parsed as { type?: unknown; data?: unknown };
    if (type === "signal" && typeof data === "object" && data !== null) {
      for (const other of this.#state.sockets(role === "host" ? "guest" : "host")) {
        other.send(message({ type: "signal", data }));
      }
    } else if (type === "done") {
      await this.#state.put(`done:${role}`, true);
      if (await this.#finished()) {
        await this.close();
      } else {
        await this.#state.setAlarm(this.#now() + DONE_GRACE_MS);
      }
    }
  }

  async #finished(): Promise<boolean> {
    return Boolean((await this.#state.get("done:host")) && (await this.#state.get("done:guest")));
  }

  /** A peer's socket closed: before both were connected, the other one hears it left. */
  async closed(socket: Socket): Promise<void> {
    const role = this.#state.roleOf(socket);
    if (!role) return;
    if (!(await this.#finished())) {
      for (const other of this.#state.sockets(role === "host" ? "guest" : "host")) reject(other, "left");
    }
    await this.close();
  }

  /** The room's time is up: whoever is still there hears it expired, unless both were connected. */
  async alarm(): Promise<void> {
    const connected = (await this.#state.get("done:host")) || (await this.#state.get("done:guest"));
    for (const role of ["host", "guest"] as const) {
      for (const socket of this.#state.sockets(role)) {
        if (connected) socket.close(1000, "done");
        else reject(socket, "expired");
      }
    }
    await this.#forget(false);
  }

  /**
   * Closes both sockets and forgets the room, so its code can be handed out again. A listed room
   * whose peers both connected stays in the directory (connected): from then on its host tells the
   * directory how it goes (POST /v1/rooms/<code>/state). Any other leaves the list.
   */
  async close(): Promise<void> {
    for (const role of ["host", "guest"] as const) {
      for (const socket of this.#state.sockets(role)) socket.close(1000, "done");
    }
    await this.#forget(await this.#finished());
  }

  async #forget(keepListing: boolean): Promise<void> {
    const listing = (await this.#state.get("listing")) as DirectoryEntry | undefined;
    await this.#state.clear();
    if (listing && !keepListing) await this.#state.publish?.(listing.joinCode, null);
  }
}
