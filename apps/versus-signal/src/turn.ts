// Relay (TURN) servers for networks with no direct route: short-lived credentials from Cloudflare's
// TURN service, asked for with a key only the server has (README.md, "TURN relay"). The relay must
// never cost money, so credentials go out only while this month's relayed bytes, read from the
// account's analytics, stay under a cap well inside the free tier. Anything missing or failing
// (a secret, the analytics query) means no relay: the game connects as before.

/** A relay server as WebRTC takes it: its URLs and the credentials they share. */
export interface IceServer {
  urls: string[];
  username: string;
  credential: string;
}

/**
 * What turns TURN on: the TURN key's two secrets, a token that reads the account's analytics, the
 * account's id, and the monthly cap (a wrangler.toml var). With any secret missing there is no relay.
 */
export interface TurnEnv {
  TURN_KEY_ID?: string;
  TURN_KEY_API_TOKEN?: string;
  TURN_ANALYTICS_TOKEN?: string;
  CLOUDFLARE_ACCOUNT_ID?: string;
  /** Relayed gigabytes a month past which no credentials go out (wrangler.toml; 800 if unset). */
  TURN_MONTHLY_CAP_GB?: string | number;
}

/** How long a credential lasts: a long session; once it expires the relay stops serving it. */
export const TURN_TTL_S = 2 * 60 * 60;
export const TURN_API = "https://rtc.live.cloudflare.com/v1/turn/keys";
export const ANALYTICS_API = "https://api.cloudflare.com/client/v4/graphql";
/** The free tier is 1000 GB a month, shared with the SFU: the default cap leaves room for the
 * analytics' delay and the sessions already relaying. */
export const DEFAULT_CAP_GB = 800;
/** How long one reading of the month's usage is trusted before the analytics are asked again. */
export const USAGE_TTL_MS = 10 * 60 * 1000;
/** How long the analytics may take; slower counts as failed (no relay). */
export const ANALYTICS_TIMEOUT_MS = 3000;

export type Fetcher = (input: string, init?: RequestInit) => Promise<Response>;

/** The month's relayed bytes as last read, and when (the cache the directory keeps). */
export interface UsageReading {
  month: string;
  egressBytes: number;
  at: number;
}

/** Where a reading is kept between requests (the directory's storage); none: every time asked. */
export interface UsageCache {
  get(): Promise<UsageReading | undefined>;
  put(reading: UsageReading): Promise<void>;
}

export function turnEnabled(env: TurnEnv): boolean {
  return Boolean(env.TURN_KEY_ID && env.TURN_KEY_API_TOKEN && env.TURN_ANALYTICS_TOKEN && env.CLOUDFLARE_ACCOUNT_ID);
}

/** The cap in bytes (decimal gigabytes, the smaller reading), or null if the var is not a number. */
export function capBytes(env: TurnEnv): number | null {
  const raw = env.TURN_MONTHLY_CAP_GB;
  const gb = raw === undefined || raw === "" ? DEFAULT_CAP_GB : Number(raw);
  return Number.isFinite(gb) && gb >= 0 ? gb * 1e9 : null;
}

const USAGE_QUERY = `query TurnUsage($accountTag: string!, $start: Date!, $end: Date!) {
  viewer {
    accounts(filter: { accountTag: $accountTag }) {
      callsTurnUsageAdaptiveGroups(filter: { date_geq: $start, date_leq: $end }, limit: 1) {
        sum { egressBytes }
      }
    }
  }
}`;

/** The UTC month of `now` ("2026-11") and its first day and today, as the analytics take dates. */
export function monthOf(now: number): { month: string; start: string; end: string } {
  const today = new Date(now).toISOString().slice(0, 10);
  return { month: today.slice(0, 7), start: `${today.slice(0, 7)}-01`, end: today };
}

/**
 * The account's relayed bytes this month, one aggregate with no dimensions (sampling makes the sums
 * of groups inexact), or null when it cannot be known: no token, an error, a timeout, an odd answer.
 */
export async function monthEgressBytes(env: TurnEnv, now: number, fetcher: Fetcher = fetch): Promise<number | null> {
  if (!env.TURN_ANALYTICS_TOKEN || !env.CLOUDFLARE_ACCOUNT_ID) return null;
  const { start, end } = monthOf(now);
  try {
    const response = await fetcher(ANALYTICS_API, {
      method: "POST",
      headers: { Authorization: `Bearer ${env.TURN_ANALYTICS_TOKEN}`, "Content-Type": "application/json" },
      body: JSON.stringify({ query: USAGE_QUERY, variables: { accountTag: env.CLOUDFLARE_ACCOUNT_ID, start, end } }),
      signal: AbortSignal.timeout(ANALYTICS_TIMEOUT_MS),
    });
    if (!response.ok) {
      console.warn(`TURN usage: the analytics answered ${response.status}`);
      return null;
    }
    const answer = await response.json() as {
      errors?: unknown[] | null;
      data?: { viewer?: { accounts?: { callsTurnUsageAdaptiveGroups?: { sum?: { egressBytes?: unknown } }[] }[] } };
    };
    if (Array.isArray(answer.errors) && answer.errors.length > 0) {
      console.warn(`TURN usage: ${JSON.stringify(answer.errors).slice(0, 300)}`);
      return null;
    }
    const accounts = answer.data?.viewer?.accounts;
    if (!Array.isArray(accounts) || accounts.length !== 1) return null;
    const groups = accounts[0].callsTurnUsageAdaptiveGroups;
    if (!Array.isArray(groups)) return null;
    // No group at all: nothing was relayed this month yet.
    if (groups.length === 0) return 0;
    const bytes = Number(groups[0].sum?.egressBytes);
    return Number.isFinite(bytes) && bytes >= 0 ? bytes : null;
  } catch (error) {
    console.warn(`TURN usage: ${String(error)}`);
    return null;
  }
}

/** Whether relay credentials may go out now: TURN set up, and this month's usage known and under the cap. */
export async function underCap(env: TurnEnv, cache: UsageCache | null, now: number, fetcher: Fetcher = fetch): Promise<boolean> {
  if (!turnEnabled(env)) return false;
  const cap = capBytes(env);
  if (cap === null) return false;
  const { month } = monthOf(now);
  let reading = await cache?.get().catch(() => undefined);
  if (!reading || reading.month !== month || now - reading.at >= USAGE_TTL_MS || now < reading.at) {
    const egressBytes = await monthEgressBytes(env, now, fetcher);
    if (egressBytes === null) return false;
    reading = { month, egressBytes, at: now };
    await cache?.put(reading).catch(() => undefined);
  }
  if (reading.egressBytes >= cap) {
    console.warn(`TURN usage: ${reading.egressBytes} bytes this month, at the cap; no relay until next month`);
    return false;
  }
  return true;
}

/** The relay servers of one answer of the TURN service: entries with credentials and turn(s): URLs. */
export function relayServers(answer: unknown): IceServer[] {
  if (typeof answer !== "object" || answer === null) return [];
  const listed = (answer as { iceServers?: unknown }).iceServers;
  const entries = Array.isArray(listed) ? listed : [listed];
  const servers: IceServer[] = [];
  for (const entry of entries) {
    if (typeof entry !== "object" || entry === null) continue;
    const { urls, username, credential } = entry as { urls?: unknown; username?: unknown; credential?: unknown };
    if (typeof username !== "string" || typeof credential !== "string" || !username || !credential) continue;
    const all = typeof urls === "string" ? [urls] : Array.isArray(urls) ? urls : [];
    // Port 53 is the DNS port: many networks block it, and it only adds a slow candidate.
    const relays = all.filter((url): url is string =>
      typeof url === "string" && /^turns?:/.test(url) && !/:53(\?|$)/.test(url));
    if (relays.length > 0) servers.push({ urls: relays, username, credential });
  }
  return servers;
}

/**
 * Fresh relay credentials for one peer of room `room` (their custom identifier, to trace abuse), or
 * none: TURN not set up, the month at its cap or unknown, or the service failed (logged, not fatal).
 */
export async function turnServers(env: TurnEnv, room: string, cache: UsageCache | null, now: number,
  fetcher: Fetcher = fetch): Promise<IceServer[]> {
  if (!(await underCap(env, cache, now, fetcher))) return [];
  try {
    const response = await fetcher(`${TURN_API}/${env.TURN_KEY_ID}/credentials/generate-ice-servers`, {
      method: "POST",
      headers: { Authorization: `Bearer ${env.TURN_KEY_API_TOKEN}`, "Content-Type": "application/json" },
      body: JSON.stringify({ ttl: TURN_TTL_S, customIdentifier: room }),
    });
    if (!response.ok) {
      console.warn(`TURN credentials: the service answered ${response.status}`);
      return [];
    }
    return relayServers(await response.json());
  } catch (error) {
    console.warn(`TURN credentials: ${String(error)}`);
    return [];
  }
}
