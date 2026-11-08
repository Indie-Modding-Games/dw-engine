// One room: a host, then a guest, and the messages between them until both are connected. The
// Durable Object (worker.ts) gives it the room's sockets and storage; the tests give it fakes.

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

/** The parts of a Durable Object's state the room uses. */
export interface RoomState {
  accept(socket: Socket, role: Role): void;
  sockets(role: Role): Socket[];
  roleOf(socket: Socket): Role | null;
  get(key: string): Promise<unknown>;
  put(key: string, value: unknown): Promise<void>;
  clear(): Promise<void>;
  setAlarm(at: number): Promise<void>;
}

export type ErrorReason = "no-room" | "full" | "expired" | "left" | "bad-code";

function message(value: object): string {
  return JSON.stringify(value);
}

function reject(socket: Socket, reason: ErrorReason): void {
  socket.send(message({ type: "error", reason }));
  socket.close(1000, reason);
}

export class Room {
  #state: RoomState;
  #now: () => number;

  constructor(state: RoomState, now: () => number = Date.now) {
    this.#state = state;
    this.#now = now;
  }

  /** A host opens the room with its code; false when the code is taken (the worker picks another). */
  async host(socket: Socket, code: string): Promise<boolean> {
    if (await this.#state.get("opened")) return false;
    await this.#state.put("opened", this.#now());
    await this.#state.setAlarm(this.#now() + ROOM_LIFETIME_MS);
    this.#state.accept(socket, "host");
    socket.send(message({ type: "room", code }));
    return true;
  }

  /** A guest joins: the room must have its host and no guest yet. */
  async join(socket: Socket): Promise<void> {
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
    socket.send(message({ type: "joined" }));
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
      if ((await this.#state.get("done:host")) && (await this.#state.get("done:guest"))) {
        await this.close();
      } else {
        await this.#state.setAlarm(this.#now() + DONE_GRACE_MS);
      }
    }
  }

  /** A peer's socket closed: before both were connected, the other one hears it left. */
  async closed(socket: Socket): Promise<void> {
    const role = this.#state.roleOf(socket);
    if (!role) return;
    const finished = (await this.#state.get("done:host")) && (await this.#state.get("done:guest"));
    if (!finished) {
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
    await this.#state.clear();
  }

  /** Closes both sockets and forgets the room, so its code can be handed out again. */
  async close(): Promise<void> {
    for (const role of ["host", "guest"] as const) {
      for (const socket of this.#state.sockets(role)) socket.close(1000, "done");
    }
    await this.#state.clear();
  }
}
