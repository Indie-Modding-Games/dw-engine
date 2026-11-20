// node --test: the rooms and the routes, with fake sockets and a fake Durable Object state.
import { test } from "node:test";
import assert from "node:assert/strict";
import { ALPHABET, newCode, normalizedCode, routeOf } from "../src/codes.ts";
import { DONE_GRACE_MS, ROOM_LIFETIME_MS, Room, type Role, type RoomState, type Socket } from "../src/room.ts";

class FakeSocket implements Socket {
  sent: unknown[] = [];
  closed: string | null = null;
  send(text: string): void {
    this.sent.push(JSON.parse(text));
  }
  close(_code?: number, reason?: string): void {
    this.closed = reason ?? "";
  }
  last(): unknown {
    return this.sent.at(-1);
  }
}

class FakeState implements RoomState {
  roles = new Map<Socket, Role>();
  storage = new Map<string, unknown>();
  alarm: number | null = null;
  accept(socket: Socket, role: Role): void {
    this.roles.set(socket, role);
  }
  sockets(role: Role): Socket[] {
    return [...this.roles].filter(([socket, r]) => r === role && !(socket as FakeSocket).closed).map(([s]) => s);
  }
  roleOf(socket: Socket): Role | null {
    return this.roles.get(socket) ?? null;
  }
  async get(key: string): Promise<unknown> {
    return this.storage.get(key);
  }
  async put(key: string, value: unknown): Promise<void> {
    this.storage.set(key, value);
  }
  async clear(): Promise<void> {
    this.storage.clear();
    this.alarm = null;
  }
  async setAlarm(at: number): Promise<void> {
    this.alarm = at;
  }
}

async function pairedRoom() {
  const state = new FakeState();
  const room = new Room(state, () => 1000);
  const host = new FakeSocket();
  const guest = new FakeSocket();
  assert.equal(await room.host(host, "K7Q2"), true);
  await room.join(guest);
  return { state, room, host, guest };
}

test("codes use the alphabet without look-alikes and read back without case or dashes", () => {
  const code = newCode();
  assert.equal(code.length, 4);
  for (const letter of code) assert.ok(ALPHABET.includes(letter));
  assert.equal(newCode((v) => v.fill(33)), "3333");
  assert.equal(normalizedCode("k7-q2"), "K7Q2");
  assert.equal(normalizedCode("K0Q2"), null);
  assert.equal(normalizedCode("K7Q"), null);
  assert.deepEqual(routeOf("/v1/host"), { kind: "host" });
  assert.deepEqual(routeOf("/v1/join/k7q2"), { kind: "join", code: "K7Q2" });
  assert.deepEqual(routeOf("/v1/join/OOPS"), { kind: "bad-code" });
  assert.deepEqual(routeOf("/v2/host"), { kind: "unknown" });
});

test("a host gets its code, a guest joins and each one's signals reach the other", async () => {
  const { state, room, host, guest } = await pairedRoom();
  assert.deepEqual(host.sent[0], { type: "room", code: "K7Q2" });
  assert.deepEqual(host.last(), { type: "peer" });
  assert.deepEqual(guest.last(), { type: "joined" });
  assert.equal(state.alarm, 1000 + ROOM_LIFETIME_MS);
  await room.message(host, JSON.stringify({ type: "signal", data: { description: "sdp", type: "offer" } }));
  assert.deepEqual(guest.last(), { type: "signal", data: { description: "sdp", type: "offer" } });
  await room.message(guest, JSON.stringify({ type: "signal", data: { candidate: "c", mid: "0" } }));
  assert.deepEqual(host.last(), { type: "signal", data: { candidate: "c", mid: "0" } });
  // Junk is ignored: not JSON, not an object, a signal without data, binary.
  await room.message(host, "{");
  await room.message(host, "3");
  await room.message(host, JSON.stringify({ type: "signal" }));
  await room.message(host, new ArrayBuffer(4));
  assert.equal(guest.sent.length, 2);
});

test("the room closes once both say they are connected, and its code is free again", async () => {
  const { state, room, host, guest } = await pairedRoom();
  await room.message(host, JSON.stringify({ type: "done" }));
  assert.equal(host.closed, null);
  assert.equal(state.alarm, 1000 + DONE_GRACE_MS);
  await room.message(guest, JSON.stringify({ type: "done" }));
  assert.equal(host.closed, "done");
  assert.equal(guest.closed, "done");
  assert.equal(state.storage.size, 0);
  assert.equal(await room.host(new FakeSocket(), "K7Q2"), true);
});

test("sockets closing after the room closed are not written to, and the room stays forgotten", async () => {
  // The runtime lists a closing socket a while longer and throws on a send to it; each socket's
  // close event comes after the room closed them both.
  class ClosingSocket extends FakeSocket {
    override send(text: string): void {
      if (this.closed !== null) throw new Error("Can't call WebSocket send() after close()");
      super.send(text);
    }
  }
  class LingeringState extends FakeState {
    override sockets(role: Role): Socket[] {
      return [...this.roles].filter(([, r]) => r === role).map(([s]) => s);
    }
  }
  const published: [string, unknown][] = [];
  const state = new LingeringState();
  (state as RoomState).publish = async (code, listing) => {
    published.push([code, listing === null ? null : "listed"]);
  };
  const room = new Room(state, () => 1000);
  const host = new ClosingSocket();
  const guest = new ClosingSocket();
  assert.equal(await room.host(host, "K7Q2", true), true);
  await room.join(guest);
  await room.message(host, JSON.stringify({ type: "done" }));
  await room.message(guest, JSON.stringify({ type: "done" }));
  assert.equal(host.closed, "done");
  const sentBefore = host.sent.length + guest.sent.length;
  await room.closed(host);
  await room.closed(guest);
  assert.equal(host.sent.length + guest.sent.length, sentBefore);
  // The connected room's listing is the host's to update; its sockets closing do not drop it.
  assert.deepEqual(published, [["K7Q2", "listed"], ["K7Q2", "listed"]]);
  // A signal to a peer that just closed is dropped, not thrown.
  const other = new Room(new LingeringState(), () => 1000);
  const lone = new ClosingSocket();
  const gone = new ClosingSocket();
  await other.host(lone, "M4XP");
  await other.join(gone);
  gone.close(1001, "gone");
  await other.message(lone, JSON.stringify({ type: "signal", data: { sdp: "x" } }));
  await other.closed(gone);
  assert.deepEqual(lone.last(), { type: "error", reason: "left" });
});

test("a taken code, a room without host and a full room are refused", async () => {
  const { room } = await pairedRoom();
  assert.equal(await room.host(new FakeSocket(), "K7Q2"), false);
  const third = new FakeSocket();
  await room.join(third);
  assert.deepEqual(third.last(), { type: "error", reason: "full" });
  assert.equal(third.closed, "full");

  const empty = new Room(new FakeState());
  const lost = new FakeSocket();
  await empty.join(lost);
  assert.deepEqual(lost.last(), { type: "error", reason: "no-room" });
});

test("a peer that leaves before both are connected tells the other, and the room expires", async () => {
  const { room, host, guest } = await pairedRoom();
  guest.close(1001, "gone");
  await room.closed(guest);
  assert.deepEqual(host.last(), { type: "error", reason: "left" });
  assert.equal(host.closed, "left");

  const state = new FakeState();
  const waiting = new Room(state);
  const alone = new FakeSocket();
  await waiting.host(alone, "M4XP");
  await waiting.alarm();
  assert.deepEqual(alone.last(), { type: "error", reason: "expired" });
  assert.equal(state.storage.size, 0);
});

test("only a room its host asked to list goes in the directory, never a private one", async () => {
  const published: [string, unknown][] = [];
  const state = new FakeState();
  (state as RoomState).publish = async (code, listing) => {
    published.push([code, listing === null ? null : { ...(listing as object) }]);
  };
  const quiet = new Room(state, () => 1000);
  assert.equal(await quiet.host(new FakeSocket(), "K7Q2"), true);
  await quiet.join(new FakeSocket());
  await quiet.close();
  assert.deepEqual(published, []);

  const listedState = new FakeState();
  (listedState as RoomState).publish = async (code, listing) => {
    published.push([code, listing === null ? null : { ...(listing as object) }]);
  };
  const listed = new Room(listedState, () => 1000);
  assert.equal(await listed.host(new FakeSocket(), "M4XP", true), true);
  await listed.join(new FakeSocket());
  await listed.close();
  assert.deepEqual(published.map(([code, listing]) => [code, listing === null ? null : (listing as { players: number }).players]),
    [["M4XP", 1], ["M4XP", 2], ["M4XP", null]]);
});
