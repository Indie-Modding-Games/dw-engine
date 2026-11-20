// node --test: the public room directory, its limits and expiry, and the rooms' side of it.
import { test } from "node:test";
import assert from "node:assert/strict";
import { DirectoryObject, OPEN_MAX_AGE_MS, STATE_MAX_AGE_MS } from "../src/directory.ts";
import { MAX_OPEN_ROOMS, validCompat, validName } from "../src/limits.ts";
import { DONE_GRACE_MS, Room, type DirectoryEntry, type Role, type RoomState, type Socket } from "../src/room.ts";

class Storage {
  map = new Map<string, unknown>();
  async get(key: string): Promise<unknown> {
    return structuredClone(this.map.get(key));
  }
  async put(key: string, value: unknown): Promise<void> {
    this.map.set(key, structuredClone(value));
  }
}

function directory() {
  const clock = { now: 1_000_000 };
  const object = new DirectoryObject({ storage: new Storage() }, {}, () => clock.now);
  return { object, clock };
}

function entry(code: string, extra: Partial<DirectoryEntry> = {}): DirectoryEntry {
  return {
    roomId: code,
    joinCode: code,
    name: "Cooperative room",
    message: "Waiting for players",
    players: 1,
    capacity: 2,
    createdAt: extra.createdAt ?? 1_000_000,
    state: "waiting",
    phase: "open",
    hostName: "Mika",
    icon: "egg",
    compat: "coop-2-abc",
    mode: "campaign",
    updatedAt: extra.createdAt ?? 1_000_000,
    token: `token-${code}`,
    ip: `ip-${code}`,
    ...extra,
  };
}

async function register(object: DirectoryObject, code: string, body: unknown): Promise<number> {
  const response = await object.fetch(new Request(`https://directory/register?code=${code}`, {
    method: "POST",
    body: JSON.stringify(body),
  }));
  return response.status;
}

async function notice(object: DirectoryObject, code: string, token: string, state: string) {
  const response = await object.fetch(new Request(`https://directory/state?code=${code}`, {
    method: "POST",
    body: JSON.stringify({ token, state }),
  }));
  return { status: response.status, body: response.status === 204 ? null : await response.json() };
}

async function listed(object: DirectoryObject, query = ""): Promise<Record<string, unknown>[]> {
  const response = await object.fetch(new Request(`https://directory/rooms${query}`));
  return ((await response.json()) as { rooms: Record<string, unknown>[] }).rooms;
}

test("the list shows a room's public fields only, oldest first, by compatibility key", async () => {
  const { object } = directory();
  assert.equal(await register(object, "M4XP", entry("M4XP", { createdAt: 1_000_500 })), 204);
  assert.equal(await register(object, "K7Q2", entry("K7Q2")), 204);
  assert.equal(await register(object, "ZZ22", entry("ZZ22", { compat: "coop-2-other", createdAt: 1_000_900 })), 204);
  const all = await listed(object);
  assert.deepEqual(all.map((room) => room.joinCode), ["K7Q2", "M4XP", "ZZ22"]);
  assert.equal(all[0].hostName, "Mika");
  assert.equal(all[0].phase, "open");
  assert.equal("token" in all[0], false);
  assert.equal("ip" in all[0], false);
  assert.deepEqual((await listed(object, "?compat=coop-2-abc")).map((room) => room.joinCode), ["K7Q2", "M4XP"]);
  assert.deepEqual(await listed(object, "?compat=nothing"), []);
});

test("one open public room per address, and no more than the list's open rooms", async () => {
  const { object } = directory();
  assert.equal(await register(object, "K7Q2", entry("K7Q2", { ip: "1.2.3.4" })), 204);
  assert.equal(await register(object, "M4XP", entry("M4XP", { ip: "1.2.3.4" })), 409);
  // Its own room updates fine, and once it is not open any more the address may open another.
  assert.equal(await register(object, "K7Q2", entry("K7Q2", { ip: "1.2.3.4", phase: "connected", players: 2 })), 204);
  assert.equal(await register(object, "M4XP", entry("M4XP", { ip: "1.2.3.4" })), 204);

  const full = directory().object;
  const codes = Array.from({ length: MAX_OPEN_ROOMS }, (_, i) => `R${String(i).padStart(3, "0")}`);
  for (const code of codes) assert.equal(await register(full, code, entry(code)), 204);
  assert.equal(await register(full, "LAST", entry("LAST")), 409);
  assert.equal((await listed(full)).length, MAX_OPEN_ROOMS);
  // A room that leaves frees its place.
  assert.equal(await register(full, codes[0], null), 204);
  assert.equal(await register(full, "LAST", entry("LAST")), 204);
});

test("a listing that is not one is refused", async () => {
  const { object } = directory();
  assert.equal(await register(object, "K7Q2", entry("M4XP")), 400);
  assert.equal(await register(object, "K7Q2", { ...entry("K7Q2"), capacity: 9 }), 400);
  assert.equal(await register(object, "K7Q2", { ...entry("K7Q2"), phase: "spectating" }), 400);
  assert.equal(await register(object, "K7Q2", { ...entry("K7Q2"), name: "x".repeat(49) }), 400);
  assert.equal((await object.fetch(new Request("https://directory/register?code=K7Q2", { method: "POST", body: "{" }))).status, 400);
  assert.deepEqual(await listed(object), []);
});

test("the host's notices need its token; playing hides the room and closed removes it", async () => {
  const { object } = directory();
  await register(object, "K7Q2", entry("K7Q2"));
  // Ready only once a guest is in.
  assert.equal((await notice(object, "K7Q2", "token-K7Q2", "ready")).status, 409);
  await register(object, "K7Q2", entry("K7Q2", { phase: "connected", players: 2, state: "full" }));
  assert.deepEqual(await notice(object, "K7Q2", "wrong", "ready"), { status: 403, body: { error: "bad-token" } });
  assert.deepEqual(await notice(object, "K7Q2", "token-K7Q2", "lost"), { status: 400, body: { error: "bad-state" } });
  assert.deepEqual(await notice(object, "NONE", "token-K7Q2", "ready"), { status: 404, body: { error: "no-room" } });
  assert.equal((await notice(object, "K7Q2", "token-K7Q2", "ready")).status, 204);
  assert.equal((await listed(object))[0].phase, "ready");
  assert.equal((await notice(object, "K7Q2", "token-K7Q2", "playing")).status, 204);
  assert.deepEqual(await listed(object), []);
  assert.equal((await notice(object, "K7Q2", "token-K7Q2", "closed")).status, 204);
  assert.equal((await notice(object, "K7Q2", "token-K7Q2", "playing")).status, 404);
});

test("entries expire by themselves: open ones with their room, the others without notices", async () => {
  const { object, clock } = directory();
  await register(object, "K7Q2", entry("K7Q2"));
  await register(object, "M4XP", entry("M4XP", { phase: "connected", players: 2 }));
  clock.now += OPEN_MAX_AGE_MS;
  assert.deepEqual((await listed(object)).map((room) => room.joinCode), ["M4XP"]);
  clock.now += STATE_MAX_AGE_MS - OPEN_MAX_AGE_MS - 1;
  assert.equal((await notice(object, "M4XP", "token-M4XP", "playing")).status, 204);
  // The notice put its clock back.
  clock.now += STATE_MAX_AGE_MS - 1;
  assert.equal((await notice(object, "M4XP", "token-M4XP", "closed")).status, 204);
  await register(object, "ZZ22", entry("ZZ22", { phase: "connected", players: 2, createdAt: clock.now }));
  assert.deepEqual((await listed(object)).map((room) => room.joinCode), ["ZZ22"]);
  clock.now += STATE_MAX_AGE_MS;
  assert.equal((await notice(object, "ZZ22", "token-ZZ22", "ready")).status, 404);
});

test("nicknames and compatibility keys from a limited alphabet", () => {
  for (const name of ["Mika", "José", "Ana-María", "Player 2", "O'Neil", "x.y_z"]) assert.ok(validName(name), name);
  for (const name of ["", " Mika", "Mika ", "a".repeat(17), "<b>", "Mika\n", "😀", "a×b", "http://x"]) {
    assert.equal(validName(name), false, name);
  }
  assert.ok(validCompat("coop-2-0123abcd"));
  assert.equal(validCompat("coop 2"), false);
  assert.equal(validCompat("x".repeat(65)), false);
});

// The rooms' side: what a listed room publishes as it goes.
class FakeSocket implements Socket {
  sent: Record<string, unknown>[] = [];
  closed: string | null = null;
  send(text: string): void {
    this.sent.push(JSON.parse(text));
  }
  close(_code?: number, reason?: string): void {
    this.closed = reason ?? "";
  }
}

class FakeState implements RoomState {
  roles = new Map<Socket, Role>();
  storage = new Map<string, unknown>();
  published: [string, DirectoryEntry | null][] = [];
  answer: "listed" | "refused" = "listed";
  tags = new Map<Socket, string>();
  accept(socket: Socket, role: Role, tag?: string): void {
    this.roles.set(socket, role);
    if (tag) this.tags.set(socket, tag);
  }
  tagOf(socket: Socket): string | null {
    return this.tags.get(socket) ?? null;
  }
  tagged(tag: string): Socket[] {
    return [...this.tags].filter(([s, t]) => t === tag && !(s as FakeSocket).closed).map(([s]) => s);
  }
  sockets(role: Role): Socket[] {
    return [...this.roles].filter(([s, r]) => r === role && !(s as FakeSocket).closed).map(([s]) => s);
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
  }
  async setAlarm(_at: number): Promise<void> {}
  async publish(code: string, listing: DirectoryEntry | null) {
    this.published.push([code, listing === null ? null : { ...listing }]);
    return this.answer;
  }
}

const relay = [{ urls: ["turn:turn.example:3478?transport=udp"], username: "u", credential: "c" }];

test("a listed room tells its host its token, and stays listed once both peers connected", async () => {
  const state = new FakeState();
  const room = new Room(state, () => 5000);
  const host = new FakeSocket();
  const guest = new FakeSocket();
  assert.equal(await room.host(host, "K7Q2", {
    listed: true, hostName: "Mika", icon: "egg", compat: "coop-2-abc", ip: "1.2.3.4", token: "t0k3n",
  }), true);
  assert.deepEqual(host.sent[0], { type: "room", code: "K7Q2", token: "t0k3n" });
  await room.join(guest);
  await room.message(host, JSON.stringify({ type: "done" }));
  await room.message(guest, JSON.stringify({ type: "done" }));
  assert.equal(host.closed, "done");
  assert.deepEqual(state.published.map(([code, e]) => [code, e?.phase ?? null, e?.players ?? 0]),
    [["K7Q2", "open", 1], ["K7Q2", "connected", 2]]);
  const first = state.published[0][1]!;
  assert.equal(first.hostName, "Mika");
  assert.equal(first.token, "t0k3n");
  assert.equal(first.ip, "1.2.3.4");
  assert.equal(state.storage.size, 0);
});

test("a listed room that fails before connecting, or expires, leaves the list", async () => {
  const state = new FakeState();
  const room = new Room(state, () => 5000);
  await room.host(new FakeSocket(), "K7Q2", { listed: true, token: "t" });
  const guest = new FakeSocket();
  await room.join(guest);
  await room.message(guest, JSON.stringify({ type: "done" }));
  assert.ok(DONE_GRACE_MS > 0);
  await room.alarm();
  assert.deepEqual(state.published.map(([, e]) => e?.phase ?? null), ["open", "connected", null]);

  const lonely = new FakeState();
  const waiting = new Room(lonely, () => 5000);
  await waiting.host(new FakeSocket(), "M4XP", { listed: true, token: "t" });
  await waiting.alarm();
  assert.deepEqual(lonely.published.map(([, e]) => e?.phase ?? null), ["open", null]);
});

test("a room the directory refuses to list is not opened: the host hears the limit", async () => {
  const state = new FakeState();
  state.answer = "refused";
  const room = new Room(state, () => 5000);
  const host = new FakeSocket();
  assert.equal(await room.host(host, "K7Q2", { listed: true, token: "t" }), "limit");
  assert.deepEqual(host.sent, []);
  assert.equal(state.storage.size, 0);
  assert.equal(state.roles.size, 0);
});

test("the relay servers go with room and joined, only when there are some", async () => {
  const state = new FakeState();
  const room = new Room(state, () => 5000);
  const host = new FakeSocket();
  const guest = new FakeSocket();
  await room.host(host, "K7Q2", { iceServers: relay });
  await room.join(guest, relay);
  assert.deepEqual(host.sent[0], { type: "room", code: "K7Q2", iceServers: relay });
  assert.deepEqual(guest.sent[0], { type: "joined", iceServers: relay });

  const plain = new Room(new FakeState(), () => 5000);
  const alone = new FakeSocket();
  await plain.host(alone, "M4XP", { iceServers: [] });
  assert.deepEqual(alone.sent[0], { type: "room", code: "M4XP" });
});
