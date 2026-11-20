// A room of more than two seats (README.md, "Rooms of up to four"): one host and up to three
// guests. The host keeps one socket waiting for the next guest; each guest is paired with a waiting
// host socket, the two trade their WebRTC messages, and once both say they are connected their
// sockets close while the room stays for the next one. The host says how many guests it holds
// (`taken`), so a seat that freed (a guest left the lobby) is offered again.
import type { DirectoryEntry, ErrorReason, RoomState, Socket } from "./room.ts";
import type { IceServer } from "./turn.ts";

/** The most players a room seats, host included. */
export const MAX_SEATS = 4;
/** How often a seated room checks on its host (and refreshes its listing) while it waits. */
export const SEAT_CHECK_MS = 2 * 60 * 1000;
/** A seated room lives at most this long, however long its host keeps waiting. */
export const SEATED_LIFETIME_MS = 60 * 60 * 1000;

/** How many seats a host asked for: 2 to MAX_SEATS (2, the two-player room, for anything else). */
export function seatsOf(asked: string | null | undefined): number {
  const seats = Number(asked ?? "2");
  return Number.isInteger(seats) && seats >= 2 && seats <= MAX_SEATS ? seats : 2;
}

interface Pair {
  host: string;
  guest: string;
  done: { host: boolean; guest: boolean };
}

interface Seating {
  code: string;
  seats: number;
  token: string;
  openedAt: number;
  /** Guests the host holds, as it last said (or as finished pairs counted since). */
  taken: number;
  pairs: Pair[];
  /** Guests waiting for a host socket, oldest first. */
  queue: string[];
  /** Sockets of pairs that finished: their closing is not the host leaving. */
  finished: string[];
  next: number;
}

function message(value: object): string {
  return JSON.stringify(value);
}

function trySend(socket: Socket, text: string): void {
  try {
    socket.send(text);
  } catch {
    // Gone.
  }
}

function tryClose(socket: Socket, reason: string): void {
  try {
    socket.close(1000, reason);
  } catch {
    // Already closed.
  }
}

function reject(socket: Socket, reason: ErrorReason): void {
  trySend(socket, message({ type: "error", reason }));
  tryClose(socket, reason);
}

function withIce(value: Record<string, unknown>, iceServers: IceServer[] | undefined): Record<string, unknown> {
  return iceServers && iceServers.length > 0 ? { ...value, iceServers } : value;
}

/** The seated half of a room (room.ts delegates to it when its host asked for more than two seats). */
export class Seats {
  #state: RoomState;
  #now: () => number;

  constructor(state: RoomState, now: () => number) {
    this.#state = state;
    this.#now = now;
  }

  async #load(): Promise<Seating | null> {
    return ((await this.#state.get("seating")) as Seating | undefined) ?? null;
  }

  async #save(seating: Seating): Promise<void> {
    await this.#state.put("seating", seating);
  }

  #one(tag: string): Socket | undefined {
    return this.#state.tagged(tag)[0];
  }

  /** Seats in use: the guests the host holds, those connecting and those waiting for a host socket. */
  #used(seating: Seating): number {
    return seating.taken + seating.pairs.length + seating.queue.length;
  }

  /** The room opened with its first host socket. */
  async open(socket: Socket, code: string, seats: number, token: string, iceServers?: IceServer[]): Promise<void> {
    const seating: Seating = { code, seats, token, openedAt: this.#now(), taken: 0, pairs: [], queue: [], finished: [], next: 1 };
    const tag = `h${seating.next++}`;
    await this.#save(seating);
    await this.#state.setAlarm(this.#now() + SEAT_CHECK_MS);
    this.#state.accept(socket, "host", tag);
    trySend(socket, message(withIce({ type: "room", code, token, seats }, iceServers)));
  }

  /** Whether a host socket with `token` may wait for the next guest. */
  async admitsSeat(token: string): Promise<boolean> {
    const seating = await this.#load();
    return seating !== null && token === seating.token;
  }

  /** The host's next socket, waiting for the next guest; `taken`: the guests it holds now. */
  async seat(socket: Socket, token: string, taken: number, iceServers?: IceServer[]): Promise<void> {
    const seating = await this.#load();
    if (!seating) {
      reject(socket, "no-room");
      return;
    }
    if (token !== seating.token) {
      reject(socket, "bad-token");
      return;
    }
    seating.taken = Math.max(0, Math.min(seating.seats - 1, Math.trunc(taken) || 0));
    const tag = `h${seating.next++}`;
    this.#state.accept(socket, "host", tag);
    trySend(socket, message(withIce({ type: "room", code: seating.code, token, seats: seating.seats }, iceServers)));
    await this.#pairWaiting(seating);
    await this.#save(seating);
    await this.#publish(seating);
  }

  /** Whether a guest would get in (a seat is free and the host is there). */
  async admits(): Promise<boolean> {
    const seating = await this.#load();
    return seating !== null && this.#used(seating) < seating.seats - 1;
  }

  async join(socket: Socket, iceServers?: IceServer[]): Promise<void> {
    // Between one guest's pair finishing and the host's next seat the room has no host socket: the
    // guest waits in the queue for it.
    const seating = await this.#load();
    if (!seating) {
      reject(socket, "no-room");
      return;
    }
    if (this.#used(seating) >= seating.seats - 1) {
      reject(socket, "full");
      return;
    }
    const tag = `g${seating.next++}`;
    this.#state.accept(socket, "guest", tag);
    seating.queue.push(tag);
    // The guest hears it got in now (its relay servers with it); the offer comes once paired.
    trySend(socket, message(withIce({ type: "joined" }, iceServers)));
    await this.#pairWaiting(seating);
    await this.#save(seating);
    await this.#publish(seating);
  }

  /** Pairs the oldest waiting guests with the host sockets that wait for one. */
  async #pairWaiting(seating: Seating): Promise<void> {
    const paired = new Set(seating.pairs.map((pair) => pair.host));
    const free = this.#state.sockets("host")
      .map((socket) => this.#state.tagOf(socket))
      .filter((tag): tag is string => tag !== null && !paired.has(tag));
    while (free.length > 0 && seating.queue.length > 0) {
      const host = free.shift() as string;
      const guest = seating.queue.shift() as string;
      const guestSocket = this.#one(guest);
      if (!guestSocket) continue;
      seating.pairs.push({ host, guest, done: { host: false, guest: false } });
      const hostSocket = this.#one(host);
      if (hostSocket) trySend(hostSocket, message({ type: "peer" }));
    }
  }

  async message(socket: Socket, type: unknown, parsed: Record<string, unknown>): Promise<void> {
    const seating = await this.#load();
    const tag = this.#state.tagOf(socket);
    if (!seating || !tag) return;
    const role = this.#state.roleOf(socket);
    const pair = seating.pairs.find((p) => p.host === tag || p.guest === tag);
    if (type === "signal" && pair && typeof parsed.data === "object" && parsed.data !== null) {
      const other = this.#one(role === "host" ? pair.guest : pair.host);
      if (other) trySend(other, message({ type: "signal", data: parsed.data }));
    } else if (type === "done" && pair && role) {
      pair.done[role] = true;
      if (pair.done.host && pair.done.guest) {
        // This guest is the host's now; its sockets go and the room stays for the next one.
        seating.pairs = seating.pairs.filter((p) => p !== pair);
        seating.taken = Math.min(seating.seats - 1, seating.taken + 1);
        seating.finished.push(pair.host, pair.guest);
        for (const done of [pair.host, pair.guest]) {
          const gone = this.#one(done);
          if (gone) tryClose(gone, "done");
        }
        await this.#save(seating);
        await this.#publish(seating);
        return;
      }
      await this.#save(seating);
    } else if (type === "seats" && role === "host" && typeof parsed.taken === "number") {
      seating.taken = Math.max(0, Math.min(seating.seats - 1, Math.trunc(parsed.taken)));
      await this.#save(seating);
      await this.#publish(seating);
    }
  }

  /** A socket closed: its pair breaks (the other one hears it left); a room with no host closes. */
  async closed(socket: Socket): Promise<void> {
    const seating = await this.#load();
    const tag = this.#state.tagOf(socket);
    if (!seating || !tag) return;
    if (seating.finished.includes(tag)) {
      seating.finished = seating.finished.filter((gone) => gone !== tag);
      await this.#save(seating);
      return;
    }
    seating.queue = seating.queue.filter((queued) => queued !== tag);
    const pair = seating.pairs.find((p) => p.host === tag || p.guest === tag);
    if (pair) {
      seating.pairs = seating.pairs.filter((p) => p !== pair);
      // The other one hears it left and goes; its closing is not the host leaving (the host opens
      // its next seat).
      const otherTag = pair.host === tag ? pair.guest : pair.host;
      seating.finished.push(otherTag);
      await this.#save(seating);
      const other = this.#one(otherTag);
      if (other) reject(other, "left");
    }
    // The host's last socket closing (not a finished pair's) is the host leaving: the room closes.
    const hosts = this.#state.sockets("host").filter((host) => host !== socket);
    if (this.#state.roleOf(socket) === "host" && hosts.length === 0) {
      for (const role of ["guest"] as const) {
        for (const left of this.#state.sockets(role)) if (left !== socket) reject(left, "left");
      }
      await this.#forget(seating);
      return;
    }
    await this.#save(seating);
    await this.#publish(seating);
  }

  /** The room checks on itself: it goes on while its host waits (up to its lifetime), else expires. */
  async alarm(): Promise<void> {
    const seating = await this.#load();
    if (!seating) return;
    const now = this.#now();
    if (this.#state.sockets("host").length > 0 && now - seating.openedAt < SEATED_LIFETIME_MS) {
      await this.#state.setAlarm(now + SEAT_CHECK_MS);
      await this.#publish(seating);
      return;
    }
    for (const role of ["host", "guest"] as const) {
      for (const socket of this.#state.sockets(role)) reject(socket, "expired");
    }
    await this.#forget(seating);
  }

  /** Closes every socket and forgets the room. */
  async close(): Promise<void> {
    const seating = await this.#load();
    for (const role of ["host", "guest"] as const) {
      for (const socket of this.#state.sockets(role)) tryClose(socket, "done");
    }
    if (seating) await this.#forget(seating);
  }

  /**
   * A listed room leaves the list when it closes with nobody in, and stays (for its host's
   * notices, as a two-seat room does) when the host holds guests.
   */
  async #forget(seating: Seating): Promise<void> {
    const listing = (await this.#state.get("listing")) as DirectoryEntry | undefined;
    await this.#state.clear();
    if (listing && seating.taken === 0) await this.#state.publish?.(listing.joinCode, null);
  }

  async #publish(seating: Seating): Promise<void> {
    const listing = (await this.#state.get("listing")) as DirectoryEntry | undefined;
    if (!listing) return;
    const players = Math.min(seating.seats, 1 + this.#used(seating));
    const full = players >= seating.seats;
    const updated: DirectoryEntry = {
      ...listing,
      players,
      capacity: seating.seats,
      state: full ? "full" : "waiting",
      phase: full ? "connected" : "open",
      message: full ? "Connected" : "Waiting for players",
      updatedAt: this.#now(),
    };
    await this.#state.put("listing", updated);
    await this.#state.publish?.(listing.joinCode, updated);
  }
}
