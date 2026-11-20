// node --test: the whole server as the game sees it, over a fake Workers runtime (WebSocketPair, 101
// responses, Durable Objects in memory): public rooms, the host's notices, the limits and TURN.
import { test } from "node:test";
import assert from "node:assert/strict";
import worker, { DirectoryObject, RoomObject, type Env } from "../src/worker.ts";
import { ANALYTICS_API, TURN_API, TURN_TTL_S } from "../src/turn.ts";

/** The four TURN secrets (the cap is the default 800 GB). */
const TURN_SECRETS = {
  TURN_KEY_ID: "key123",
  TURN_KEY_API_TOKEN: "secret",
  TURN_ANALYTICS_TOKEN: "analytics",
  CLOUDFLARE_ACCOUNT_ID: "account",
};

/** An analytics answer: this month's relayed bytes. */
function usage(egressBytes: number): Response {
  return Response.json({ data: { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: [{ sum: { egressBytes } }] }] } } });
}

class FakeWs {
  sent: string[] = [];
  closed: string | null = null;
  peer: FakeWs | null = null;
  accept(): void {}
  send(text: string): void {
    this.sent.push(text);
  }
  close(_code?: number, reason?: string): void {
    this.closed = reason ?? "";
  }
}

(globalThis as Record<string, unknown>).WebSocketPair = class {
  0 = new FakeWs();
  1 = new FakeWs();
  constructor() {
    this[0].peer = this[1];
    this[1].peer = this[0];
  }
};
const NodeResponse = globalThis.Response;
// Node's Response takes no 101: an upgrade answers 200 here, keeping its client socket.
class WorkerResponse extends NodeResponse {
  webSocket: FakeWs | null = null;
  constructor(body?: BodyInit | null, init?: ResponseInit & { webSocket?: FakeWs }) {
    const upgrade = init?.status === 101;
    super(body, upgrade ? { ...init, status: 200 } : init);
    if (upgrade) {
      this.webSocket = init!.webSocket ?? null;
      Object.defineProperty(this, "status", { value: 101 });
    }
  }
}
globalThis.Response = WorkerResponse as typeof Response;

/** A Durable Object namespace that makes each object once, by name. */
function namespace<T>(make: () => T) {
  const objects = new Map<string, T>();
  return {
    objects,
    idFromName: (name: string) => name,
    get(id: unknown) {
      const name = String(id);
      if (!objects.has(name)) objects.set(name, make());
      const object = objects.get(name) as { fetch(request: Request): Promise<Response> };
      return { fetch: (request: Request) => object.fetch(request) };
    },
  };
}

function roomContext() {
  const tags = new Map<FakeWs, string[]>();
  const storage = new Map<string, unknown>();
  return {
    tags,
    acceptWebSocket(socket: FakeWs, given: string[]) {
      tags.set(socket, given);
    },
    getWebSockets(tag?: string) {
      return [...tags].filter(([s, t]) => s.closed === null && (!tag || t.includes(tag))).map(([s]) => s);
    },
    getTags: (socket: FakeWs) => tags.get(socket) ?? [],
    storage: {
      get: async (key: string) => storage.get(key),
      put: async (key: string, value: unknown) => void storage.set(key, value),
      deleteAll: async () => storage.clear(),
      deleteAlarm: async () => {},
      setAlarm: async (_at: number) => {},
    },
  };
}

interface Limiter {
  left: number;
  limit(options: { key: string }): Promise<{ success: boolean }>;
}

function limiter(left: number): Limiter {
  return {
    left,
    async limit() {
      return { success: this.left-- > 0 };
    },
  };
}

function server(extra: Partial<Env> = {}) {
  const contexts: ReturnType<typeof roomContext>[] = [];
  const rooms = namespace(() => {
    const context = roomContext();
    contexts.push(context);
    return { context, object: new RoomObject(context as never, env) };
  });
  const roomsNamespace = {
    idFromName: rooms.idFromName,
    get: (id: unknown) => {
      rooms.get(id);
      const { object } = rooms.objects.get(String(id))!;
      return { fetch: (request: Request) => object.fetch(request) };
    },
  };
  const env: Env = {
    ROOMS: roomsNamespace,
    DIRECTORY: namespace(() => new DirectoryObject({ storage: memory() })),
    ...extra,
  };
  return { env, rooms };
}

function memory() {
  const map = new Map<string, unknown>();
  return { get: async (key: string) => structuredClone(map.get(key)), put: async (key: string, value: unknown) => void map.set(key, structuredClone(value)) };
}

/** A WebSocket request to the server; what it heard, and its server side to talk on. */
async function connect(env: Env, path: string, ip = "1.2.3.4", headers: Record<string, string> = {}) {
  const response = await worker.fetch(new Request(`https://signal.test${path}`, {
    headers: { Upgrade: "websocket", "CF-Connecting-IP": ip, ...headers },
  }), env) as WorkerResponse;
  assert.equal(response.status, 101, `${path} upgrades`);
  const client = response.webSocket!;
  return { heard: () => client.peer!.sent.map((text) => JSON.parse(text)), server: client.peer! };
}

/** What a peer sends its room (the room object's webSocketMessage). */
async function say(env: ReturnType<typeof server>, code: string, socket: FakeWs, value: object) {
  await env.rooms.objects.get(code)!.object.webSocketMessage(socket as never, JSON.stringify(value));
}

async function list(env: Env, query = "", ip = "1.2.3.4") {
  const response = await worker.fetch(new Request(`https://signal.test/v1/rooms${query}`, {
    headers: { "CF-Connecting-IP": ip },
  }), env);
  return { status: response.status, body: await response.json() as { rooms?: Record<string, unknown>[]; error?: string } };
}

async function post(env: Env, code: string, body: unknown) {
  const response = await worker.fetch(new Request(`https://signal.test/v1/rooms/${code}/state`, {
    method: "POST",
    body: JSON.stringify(body),
  }), env);
  return response.status;
}

test("/v1/info says what the server can do, over HTTP and as a WebSocket snapshot", async () => {
  const plain = server();
  const answer = await worker.fetch(new Request("https://signal.test/v1/info"), plain.env);
  assert.deepEqual(await answer.json(), { protocol: "v1", features: ["public-rooms", "room-state", "seats"] });
  const keyOnly = await worker.fetch(new Request("https://signal.test/v1/info"),
    server({ TURN_KEY_ID: "key", TURN_KEY_API_TOKEN: "secret" }).env);
  assert.deepEqual((await keyOnly.json() as { features: string[] }).features, ["public-rooms", "room-state", "seats"]);
  const relayed = server(TURN_SECRETS);
  const { heard } = await connect(relayed.env, "/v1/info");
  assert.deepEqual(heard(), [{ protocol: "v1", features: ["public-rooms", "room-state", "seats", "relay"] }]);
});

test("a public room is listed with its host's name until it plays, then its host closes it", async () => {
  const signal = server();
  const host = await connect(signal.env, "/v1/host?public=1&name=Mika&icon=egg&compat=coop-2-abc&mode=campaign");
  const opened = host.heard()[0] as { type: string; code: string; token: string };
  assert.equal(opened.type, "room");
  assert.match(opened.token, /^[0-9a-f]{32}$/);
  let rooms = (await list(signal.env, "?compat=coop-2-abc")).body.rooms!;
  assert.equal(rooms.length, 1);
  assert.deepEqual([rooms[0].joinCode, rooms[0].hostName, rooms[0].icon, rooms[0].phase], [opened.code, "Mika", "egg", "open"]);
  assert.deepEqual((await list(signal.env, "?compat=coop-9-zzz")).body.rooms, []);

  const guest = await connect(signal.env, `/v1/join/${opened.code}`, "5.6.7.8");
  assert.deepEqual(guest.heard(), [{ type: "joined" }]);
  assert.equal((await list(signal.env)).body.rooms![0].phase, "connected");
  await say(signal, opened.code, host.server, { type: "done" });
  await say(signal, opened.code, guest.server, { type: "done" });
  assert.equal(host.server.closed, "done");
  // The room closed, its entry stays: the host tells how it goes.
  assert.equal(await post(signal.env, opened.code, { token: "nope", state: "ready" }), 403);
  assert.equal(await post(signal.env, opened.code, { token: opened.token, state: "ready" }), 204);
  assert.equal((await list(signal.env)).body.rooms![0].phase, "ready");
  const notice = await connect(signal.env, `/v1/rooms/${opened.code}/state?token=${opened.token}&state=playing`);
  assert.deepEqual(notice.heard(), [{ type: "state" }]);
  assert.deepEqual((await list(signal.env)).body.rooms, []);
  assert.equal(await post(signal.env, opened.code, { token: opened.token, state: "closed" }), 204);
  const late = await connect(signal.env, `/v1/rooms/${opened.code}/state?token=${opened.token}&state=closed`);
  assert.deepEqual(late.heard(), [{ type: "error", reason: "no-room" }]);
});

test("a private room is never listed and gets no token, as in the first protocol", async () => {
  const signal = server();
  const host = await connect(signal.env, "/v1/host?name=Mika");
  const opened = host.heard()[0] as Record<string, unknown>;
  assert.deepEqual(Object.keys(opened), ["type", "code"]);
  assert.deepEqual((await list(signal.env)).body.rooms, []);
});

test("one open public room per address; a bad nickname is refused; private rooms are not counted", async () => {
  const signal = server();
  const first = await connect(signal.env, "/v1/host?public=1&name=Mika");
  assert.equal(first.heard()[0].type, "room");
  const second = await connect(signal.env, "/v1/host?public=1&name=Mika");
  assert.deepEqual(second.heard(), [{ type: "error", reason: "limit" }]);
  assert.equal(second.server.closed, "limit");
  const other = await connect(signal.env, "/v1/host?public=1&name=Ana", "9.9.9.9");
  assert.equal(other.heard()[0].type, "room");
  const privately = await connect(signal.env, "/v1/host");
  assert.equal(privately.heard()[0].type, "room");
  const named = await connect(signal.env, `/v1/host?public=1&name=${encodeURIComponent("<script>")}`, "7.7.7.7");
  assert.deepEqual(named.heard(), [{ type: "error", reason: "bad-name" }]);
  assert.equal((await list(signal.env)).body.rooms!.length, 2);
});

test("per-address rate limits on creating rooms and reading the list", async () => {
  const signal = server({ HOST_LIMIT: limiter(1), LIST_LIMIT: limiter(1) });
  assert.equal((await connect(signal.env, "/v1/host")).heard()[0].type, "room");
  assert.deepEqual((await connect(signal.env, "/v1/host")).heard(), [{ type: "error", reason: "limit" }]);
  assert.equal((await list(signal.env)).status, 200);
  assert.deepEqual(await list(signal.env), { status: 429, body: { error: "limit" } });
  assert.deepEqual((await connect(signal.env, "/v1/rooms")).heard(), [{ type: "error", reason: "limit" }]);
});

test("the list read over a WebSocket is one snapshot of the same rooms", async () => {
  const signal = server();
  await connect(signal.env, "/v1/host?public=1&name=Mika&compat=k");
  const snapshot = await connect(signal.env, "/v1/rooms?compat=k");
  const [answer] = snapshot.heard() as { rooms: Record<string, unknown>[] }[];
  assert.equal(answer.rooms.length, 1);
  assert.equal(snapshot.server.closed, "snapshot");
});

test("with the TURN secrets and the month under its cap both peers get short-lived relay servers", async () => {
  const asked: { url: string; auth: string | null; body: unknown }[] = [];
  const answer = {
    iceServers: [
      { urls: ["stun:stun.cloudflare.com:3478", "stun:stun.cloudflare.com:53"] },
      {
        urls: ["turn:turn.cloudflare.com:3478?transport=udp", "turn:turn.cloudflare.com:53?transport=udp",
          "turns:turn.cloudflare.com:443?transport=tcp"],
        username: "user",
        credential: "pass/word+=",
      },
    ],
  };
  const signal = server({
    ...TURN_SECRETS,
    TURN_FETCH: async (url, init) => {
      asked.push({ url, auth: new Headers(init?.headers).get("Authorization"), body: JSON.parse(String(init?.body)) });
      return url === ANALYTICS_API ? usage(5e9) : Response.json(answer, { status: 201 });
    },
  });
  const relay = [{
    urls: ["turn:turn.cloudflare.com:3478?transport=udp", "turns:turn.cloudflare.com:443?transport=tcp"],
    username: "user",
    credential: "pass/word+=",
  }];
  // A client cannot hand its own relay servers to the other peer.
  const forged = JSON.stringify([{ urls: ["turn:evil:3478"], username: "x", credential: "y" }]);
  const host = await connect(signal.env, "/v1/host", "1.2.3.4", { "X-Dwe-Ice-Servers": forged });
  const opened = host.heard()[0] as { code: string; iceServers: unknown };
  assert.deepEqual(opened.iceServers, relay);
  const guest = await connect(signal.env, `/v1/join/${opened.code}`);
  assert.deepEqual(guest.heard(), [{ type: "joined", iceServers: relay }]);
  // The analytics once (the directory keeps the reading), then the TURN API once per peer.
  assert.deepEqual(asked.map((call) => call.url), [
    ANALYTICS_API,
    `${TURN_API}/key123/credentials/generate-ice-servers`,
    `${TURN_API}/key123/credentials/generate-ice-servers`,
  ]);
  assert.equal(asked[0].auth, "Bearer analytics");
  assert.equal(asked[1].auth, "Bearer secret");
  assert.deepEqual(asked[1].body, { ttl: TURN_TTL_S, customIdentifier: opened.code });
  assert.deepEqual(asked[2].body, { ttl: TURN_TTL_S, customIdentifier: opened.code });
});

test("without the TURN secrets, at the cap, or with a service down, rooms work exactly as before", async () => {
  let calls = 0;
  const fetcher = async () => {
    calls++;
    return new Response("down", { status: 500 });
  };
  // Only the TURN key: no analytics token, so nothing is asked and nothing relayed.
  const off = server({ TURN_FETCH: fetcher, TURN_KEY_ID: "key123", TURN_KEY_API_TOKEN: "secret" });
  const host = await connect(off.env, "/v1/host", "1.2.3.4", { "X-Dwe-Ice-Servers": "[]" });
  assert.deepEqual(Object.keys(host.heard()[0]), ["type", "code"]);
  assert.equal(calls, 0);

  // The analytics down: no relay (and no TURN request).
  const down = server({ ...TURN_SECRETS, TURN_FETCH: fetcher });
  const opened = (await connect(down.env, "/v1/host")).heard()[0] as { code: string };
  assert.deepEqual(Object.keys(opened), ["type", "code"]);
  const guest = await connect(down.env, `/v1/join/${opened.code}`);
  assert.deepEqual(guest.heard(), [{ type: "joined" }]);
  assert.equal(calls, 2);

  // The month at its cap: the analytics are read, the TURN API is never asked.
  const turnCalls: string[] = [];
  const capped = server({
    ...TURN_SECRETS,
    TURN_MONTHLY_CAP_GB: "800",
    TURN_FETCH: async (url) => {
      if (url === ANALYTICS_API) return usage(800e9);
      turnCalls.push(url);
      return Response.json({ iceServers: [] }, { status: 201 });
    },
  });
  const full = (await connect(capped.env, "/v1/host")).heard()[0] as { code: string };
  assert.deepEqual(Object.keys(full), ["type", "code"]);
  assert.deepEqual((await connect(capped.env, `/v1/join/${full.code}`)).heard(), [{ type: "joined" }]);
  assert.deepEqual(turnCalls, []);

  // The analytics fine, the TURN service down.
  const turnDown = server({
    ...TURN_SECRETS,
    TURN_FETCH: async (url) => {
      if (url === ANALYTICS_API) return usage(0);
      throw new Error("offline");
    },
  });
  assert.deepEqual(Object.keys((await connect(turnDown.env, "/v1/host")).heard()[0]), ["type", "code"]);
});

test("a state notice for a code that cannot be one, or a GET, is refused", async () => {
  const signal = server();
  assert.equal(await post(signal.env, "OOPS", { token: "t", state: "ready" }), 404);
  const get = await worker.fetch(new Request("https://signal.test/v1/rooms/K7Q2/state"), signal.env);
  assert.equal(get.status, 405);
  assert.equal(await post(signal.env, "K7Q2", { token: "t", state: "dancing" }), 400);
});

/** A peer's socket closing (the room object's webSocketClose). */
async function gone(env: ReturnType<typeof server>, code: string, socket: FakeWs) {
  socket.closed ??= "gone";
  await env.rooms.objects.get(code)!.object.webSocketClose(socket as never);
}

test("a room of four seats pairs each guest with the host's waiting socket and counts its seats", async () => {
  const signal = server();
  const host = await connect(signal.env, "/v1/host?public=1&name=Mika&compat=coop-5-abc&seats=4");
  const opened = host.heard()[0] as { type: string; code: string; token: string; seats: number };
  assert.equal(opened.seats, 4);
  assert.match(opened.token, /^[0-9a-f]{32}$/);
  const shown = () => (list(signal.env, "?compat=coop-5-abc")).then((answer) => answer.body.rooms![0]);
  assert.deepEqual([(await shown()).players, (await shown()).capacity, (await shown()).state], [1, 4, "waiting"]);

  // The first guest: paired with the host's first socket, signals go between the two only.
  const first = await connect(signal.env, `/v1/join/${opened.code}`, "5.6.7.8");
  assert.deepEqual(first.heard(), [{ type: "joined" }]);
  assert.deepEqual(host.heard().at(-1), { type: "peer" });
  await say(signal, opened.code, host.server, { type: "signal", data: { description: "offer", type: "offer" } });
  assert.deepEqual(first.heard().at(-1), { type: "signal", data: { description: "offer", type: "offer" } });
  await say(signal, opened.code, first.server, { type: "done" });
  await say(signal, opened.code, host.server, { type: "done" });
  assert.equal(host.server.closed, "done");
  await gone(signal, opened.code, host.server);
  await gone(signal, opened.code, first.server);
  // Its pair is done and the room stays: the host holds that guest, three of four seats are free.
  assert.equal((await shown()).players, 2);

  // A second guest waits for the host's next seat, which a wrong token cannot take.
  const second = await connect(signal.env, `/v1/join/${opened.code}`, "5.6.7.9");
  assert.deepEqual(second.heard(), [{ type: "joined" }]);
  const intruder = await connect(signal.env, `/v1/host/${opened.code}?token=nope&taken=1`);
  assert.deepEqual(intruder.heard(), [{ type: "error", reason: "bad-token" }]);
  const seat = await connect(signal.env, `/v1/host/${opened.code}?token=${opened.token}&taken=1`);
  assert.deepEqual(seat.heard(), [{ type: "room", code: opened.code, token: opened.token, seats: 4 }, { type: "peer" }]);
  await say(signal, opened.code, second.server, { type: "signal", data: { candidate: "c", mid: "0" } });
  assert.deepEqual(seat.heard().at(-1), { type: "signal", data: { candidate: "c", mid: "0" } });

  // A third guest queues for the next seat; the room is full, a fourth one hears so.
  const third = await connect(signal.env, `/v1/join/${opened.code}`, "5.6.7.10");
  assert.deepEqual(third.heard(), [{ type: "joined" }]);
  assert.deepEqual([(await shown()).players, (await shown()).state], [4, "full"]);
  const late = await connect(signal.env, `/v1/join/${opened.code}`, "5.6.7.11");
  assert.deepEqual(late.heard(), [{ type: "error", reason: "full" }]);

  // The second guest leaves before connecting: its host socket hears it, the room stays.
  await gone(signal, opened.code, second.server);
  assert.deepEqual(seat.heard().at(-1), { type: "error", reason: "left" });
  await gone(signal, opened.code, seat.server);
  assert.equal((await shown()).players, 3);
  // The host's next seat takes the queued third guest.
  const next = await connect(signal.env, `/v1/host/${opened.code}?token=${opened.token}&taken=1`);
  assert.deepEqual(next.heard().at(-1), { type: "peer" });

  // The host says a guest left its lobby: that seat is offered again.
  await say(signal, opened.code, next.server, { type: "seats", taken: 0 });
  assert.equal((await shown()).players, 2);

  // The host's last socket goes: the queued and paired guests hear it, the room closes.
  await gone(signal, opened.code, next.server);
  assert.deepEqual(third.heard().at(-1), { type: "error", reason: "left" });
  assert.deepEqual((await list(signal.env, "?compat=coop-5-abc")).body.rooms, []);
});

test("a room asks for two to four seats; anything else is the two-seat room of the first protocol", async () => {
  const signal = server();
  for (const asked of ["seats=9", "seats=1", "seats=x", ""]) {
    const host = await connect(signal.env, `/v1/host?${asked}`);
    assert.deepEqual(Object.keys(host.heard()[0] as object), ["type", "code"], asked);
  }
  const three = await connect(signal.env, "/v1/host?seats=3");
  assert.equal((three.heard()[0] as { seats: number }).seats, 3);
  const code = (three.heard()[0] as { code: string }).code;
  await connect(signal.env, `/v1/join/${code}`, "5.6.7.8");
  await connect(signal.env, `/v1/join/${code}`, "5.6.7.9");
  const late = await connect(signal.env, `/v1/join/${code}`, "5.6.7.10");
  assert.deepEqual(late.heard(), [{ type: "error", reason: "full" }]);
  const nowhere = await connect(signal.env, "/v1/host/K7Q2?token=x&taken=0");
  assert.deepEqual(nowhere.heard(), [{ type: "error", reason: "no-room" }]);
});
