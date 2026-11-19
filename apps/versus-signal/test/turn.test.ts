// node --test: the TURN spending guard (turn.ts) with the analytics and the TURN API faked: credentials
// go out only while this month's relayed bytes are known and under the cap.
import { test } from "node:test";
import assert from "node:assert/strict";
import {
  ANALYTICS_API, ANALYTICS_TIMEOUT_MS, TURN_API, TURN_TTL_S, USAGE_TTL_MS, monthOf, turnServers,
  type Fetcher, type UsageCache, type UsageReading,
} from "../src/turn.ts";

const SECRETS = {
  TURN_KEY_ID: "key123",
  TURN_KEY_API_TOKEN: "secret",
  TURN_ANALYTICS_TOKEN: "analytics",
  CLOUDFLARE_ACCOUNT_ID: "account",
};
const NOW = Date.UTC(2026, 10, 19, 12, 0, 0);
const RELAY = { urls: ["turn:turn.cloudflare.com:3478?transport=udp"], username: "user", credential: "pass" };

function usage(egressBytes: number): Response {
  return Response.json({ data: { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: [{ sum: { egressBytes } }] }] } } });
}

/** A fake Cloudflare API: the analytics answer `analytics()`, the TURN API one relay; every call kept. */
function api(analytics: (init?: RequestInit) => Response | Promise<Response>) {
  const calls: { url: string; body: Record<string, unknown> }[] = [];
  const fetcher: Fetcher = async (url, init) => {
    calls.push({ url, body: JSON.parse(String(init?.body)) });
    if (url === ANALYTICS_API) return analytics(init);
    return Response.json({ iceServers: [RELAY] }, { status: 201 });
  };
  return { fetcher, calls, analyticsCalls: () => calls.filter((call) => call.url === ANALYTICS_API).length };
}

function memoryCache(): UsageCache & { reading?: UsageReading } {
  return {
    async get() {
      return this.reading;
    },
    async put(reading) {
      this.reading = reading;
    },
  };
}

test("under the cap the credentials go out, tagged with the room, after one aggregate usage query", async () => {
  const fake = api(() => usage(799e9));
  const servers = await turnServers({ ...SECRETS, TURN_MONTHLY_CAP_GB: 800 }, "K7Q2", null, NOW, fake.fetcher);
  assert.deepEqual(servers, [RELAY]);
  assert.deepEqual(fake.calls.map((call) => call.url),
    [ANALYTICS_API, `${TURN_API}/key123/credentials/generate-ice-servers`]);
  const query = fake.calls[0].body as { query: string; variables: Record<string, string> };
  assert.match(query.query, /callsTurnUsageAdaptiveGroups\(filter: \{ date_geq: \$start, date_leq: \$end \}, limit: 1\)/);
  assert.match(query.query, /sum \{ egressBytes \}/);
  assert.doesNotMatch(query.query, /dimensions/);
  assert.deepEqual(query.variables, { accountTag: "account", start: "2026-11-01", end: "2026-11-19" });
  assert.deepEqual(fake.calls[1].body, { ttl: TURN_TTL_S, customIdentifier: "K7Q2" });
  assert.equal(TURN_TTL_S, 2 * 60 * 60);
});

test("nothing relayed yet this month (no group) counts as zero", async () => {
  const fake = api(() => Response.json({ data: { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: [] }] } } }));
  assert.deepEqual(await turnServers(SECRETS, "K7Q2", null, NOW, fake.fetcher), [RELAY]);
});

test("at or over the cap no credentials go out and the TURN API is not asked", async () => {
  for (const bytes of [800e9, 950e9]) {
    const fake = api(() => usage(bytes));
    assert.deepEqual(await turnServers(SECRETS, "K7Q2", null, NOW, fake.fetcher), []);
    assert.deepEqual(fake.calls.map((call) => call.url), [ANALYTICS_API]);
  }
  // The default cap is 800 GB; a lower one cuts earlier; one that is not a number cuts everything.
  assert.deepEqual(await turnServers({ ...SECRETS, TURN_MONTHLY_CAP_GB: "10" }, "K7Q2", null, NOW, api(() => usage(10e9)).fetcher), []);
  assert.deepEqual(await turnServers({ ...SECRETS, TURN_MONTHLY_CAP_GB: "lots" }, "K7Q2", null, NOW, api(() => usage(0)).fetcher), []);
});

test("without the analytics token or the account id no credentials go out and nothing is asked", async () => {
  for (const missing of ["TURN_ANALYTICS_TOKEN", "CLOUDFLARE_ACCOUNT_ID", "TURN_KEY_ID"] as const) {
    const fake = api(() => usage(0));
    const env = { ...SECRETS, [missing]: undefined };
    assert.deepEqual(await turnServers(env, "K7Q2", null, NOW, fake.fetcher), [], missing);
    assert.equal(fake.calls.length, 0, missing);
  }
});

test("an analytics error, a refused token, an odd answer or a timeout means no credentials", async () => {
  const failing: (() => Response | Promise<Response>)[] = [
    () => Response.json({ data: null, errors: [{ message: "not authorized" }] }),
    () => new Response("forbidden", { status: 403 }),
    () => Response.json({ data: { viewer: { accounts: [] } } }),
    () => Response.json({ data: { viewer: { accounts: [{ callsTurnUsageAdaptiveGroups: [{ sum: {} }] }] } } }),
    () => new Response("not json"),
  ];
  for (const answer of failing) {
    const fake = api(answer);
    assert.deepEqual(await turnServers(SECRETS, "K7Q2", null, NOW, fake.fetcher), []);
    assert.equal(fake.calls.length, 1);
  }
  // An analytics that never answers: the request is aborted after ANALYTICS_TIMEOUT_MS.
  const hanging = api((init) => new Promise<Response>((_, reject) => {
    init?.signal?.addEventListener("abort", () => reject(init.signal!.reason));
  }));
  const started = Date.now();
  assert.deepEqual(await turnServers(SECRETS, "K7Q2", null, NOW, hanging.fetcher), []);
  assert.ok(Date.now() - started >= ANALYTICS_TIMEOUT_MS - 100);
  assert.equal(hanging.calls.length, 1);
});

test("a reading is reused for its TTL, then asked again; a new month or a failed reading is never reused", async () => {
  const cache = memoryCache();
  let bytes = 100e9;
  const fake = api(() => usage(bytes));
  assert.deepEqual(await turnServers(SECRETS, "AAAA", cache, NOW, fake.fetcher), [RELAY]);
  assert.deepEqual(cache.reading, { month: "2026-11", egressBytes: 100e9, at: NOW });
  // Within the TTL the cache answers, even if the month got over the cap meanwhile.
  bytes = 900e9;
  assert.deepEqual(await turnServers(SECRETS, "BBBB", cache, NOW + USAGE_TTL_MS - 1, fake.fetcher), [RELAY]);
  assert.equal(fake.analyticsCalls(), 1);
  // Past the TTL the analytics are asked again, and the cap holds.
  assert.deepEqual(await turnServers(SECRETS, "CCCC", cache, NOW + USAGE_TTL_MS, fake.fetcher), []);
  assert.equal(fake.analyticsCalls(), 2);
  assert.equal(cache.reading?.egressBytes, 900e9);
  // A new month starts from a fresh reading.
  bytes = 1e9;
  const december = Date.UTC(2026, 11, 1, 0, 1);
  assert.equal(monthOf(december).start, "2026-12-01");
  assert.deepEqual(await turnServers(SECRETS, "DDDD", cache, december, fake.fetcher), [RELAY]);
  assert.equal(fake.analyticsCalls(), 3);
  // A failed reading is not kept: the next request asks again.
  const failing = api(() => new Response("down", { status: 500 }));
  const fresh = memoryCache();
  assert.deepEqual(await turnServers(SECRETS, "EEEE", fresh, NOW, failing.fetcher), []);
  assert.deepEqual(await turnServers(SECRETS, "EEEE", fresh, NOW + 1, failing.fetcher), []);
  assert.equal(failing.analyticsCalls(), 2);
  assert.equal(fresh.reading, undefined);
});
