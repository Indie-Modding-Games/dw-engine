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
