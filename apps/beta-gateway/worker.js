// dwe closed-beta gateway (apps/beta-gateway/README.md): a Cloudflare Worker between the
// launcher and the private repository's releases. It answers the few GitHub REST API requests the
// launcher and its updater make, for one repository only, to whoever sends the shared beta key:
//
//   GET /repos/<owner>/<repo>/releases[?per_page=N]     the releases (no drafts)
//   GET /repos/<owner>/<repo>/releases/latest           the newest non-prerelease release
//   GET /repos/<owner>/<repo>/releases/tags/<tag>       one release
//   GET /repos/<owner>/<repo>/releases/assets/<id>      a release file (Accept: application/octet-stream)
//   GET /repos/<owner>/<repo>/releases/download/<tag>/<name>   the same, by release and file name
//
// Every file URL it answers points back at the gateway. A small JSON file (game-manifest.json,
// the updater's latest.json) is served by the gateway with the URLs inside it pointed back at
// the gateway too; any other file is a 302 to GitHub's short-lived signed download URL, which
// carries no token. The GitHub token never leaves the Worker.
//
// Settings: secret BETA_KEY (the shared key the testers paste), secret GITHUB_TOKEN (fine-grained,
// read-only Contents on the repository), variable GITHUB_REPO (owner/repo). GITHUB_API (default
// https://api.github.com) exists only for local tests. Plain JavaScript, no dependencies: the
// whole Worker is this file.

const DEFAULT_UPSTREAM = "https://api.github.com";
const API_VERSION = "2022-11-28";
const USER_AGENT = "dwe-beta-gateway";
/** Seconds a release listing (or an asset's description) is reused before GitHub is asked again. */
const CACHE_SECONDS = 60;
/** JSON release files up to this size are read and rewritten by the gateway; larger files redirect. */
const MANIFEST_LIMIT = 1024 * 1024;
/** A shorter BETA_KEY is treated as a mistake: the gateway refuses everything until it is fixed. */
const MIN_KEY_LENGTH = 24;
const LISTING_SIZE = 100;

export default {
  async fetch(request, env, ctx) {
    try {
      return await handle(request, env, ctx);
    } catch (error) {
      // Never the error's text to the caller: it could name an upstream URL.
      console.error("beta-gateway: unexpected failure:", error && error.name);
      return message(502, "Gateway error");
    }
  },
};

async function handle(request, env, ctx) {
  if (request.method !== "GET" && request.method !== "HEAD") {
    return message(405, "Method not allowed", { Allow: "GET, HEAD" });
  }
  const config = configuration(env);
  if (!config) {
    console.error("beta-gateway: BETA_KEY, GITHUB_TOKEN or GITHUB_REPO is missing or too short");
    return message(503, "Gateway not configured");
  }
  if (!(await authorized(request, config.key))) {
    return message(401, "Bad credentials: this needs the beta key", { "WWW-Authenticate": 'Bearer realm="dwe beta"' });
  }
  const url = new URL(request.url);
  const gateway = { origin: url.origin, config, ctx };
  const route = parseRoute(url.pathname, config.repo);
  if (!route) return message(404, "Not Found");
  switch (route.kind) {
    case "list":
      return listReleases(gateway, url.searchParams);
    case "latest":
      return oneRelease(gateway, "/releases/latest");
    case "tag":
      return oneRelease(gateway, `/releases/tags/${encodeURIComponent(route.tag)}`);
    case "asset":
      return asset(gateway, route.id, request);
    case "download":
      return assetByName(gateway, route.tag, route.name, request);
  }
  return message(404, "Not Found");
}

function configuration(env) {
  const key = typeof env.BETA_KEY === "string" ? env.BETA_KEY.trim() : "";
  const token = typeof env.GITHUB_TOKEN === "string" ? env.GITHUB_TOKEN.trim() : "";
  const repo = typeof env.GITHUB_REPO === "string" ? env.GITHUB_REPO.trim() : "";
  if (key.length < MIN_KEY_LENGTH || !token || !/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo)) return null;
  const upstream = (typeof env.GITHUB_API === "string" && env.GITHUB_API.trim()) || DEFAULT_UPSTREAM;
  return { key, token, repo, upstream: upstream.replace(/\/+$/, "") };
}

// ---- The beta key -------------------------------------------------------------------------------

/** Compares the request's bearer with the key in constant time (both hashed first, so neither
 *  the length nor the first differing byte shows in the timing). */
async function authorized(request, key) {
  const header = request.headers.get("Authorization") || "";
  const match = /^Bearer\s+(.+)$/i.exec(header.trim());
  const given = match ? match[1].trim() : "";
  const [a, b] = await Promise.all([sha256(given), sha256(key)]);
  let difference = given.length === 0 ? 1 : 0;
  for (let i = 0; i < a.length; i++) difference |= a[i] ^ b[i];
  return difference === 0;
}

async function sha256(text) {
  return new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)));
}

// ---- Routes -------------------------------------------------------------------------------------

function parseRoute(pathname, repo) {
  const prefix = `/repos/${repo}/releases`.toLowerCase();
  const path = pathname.replace(/\/+$/, "");
  if (!path.toLowerCase().startsWith(prefix)) return null;
  const rest = path.slice(prefix.length);
  if (rest === "") return { kind: "list" };
  if (rest === "/latest") return { kind: "latest" };
  let m = /^\/tags\/([^/]+)$/.exec(rest);
  if (m) return { kind: "tag", tag: decode(m[1]) };
  m = /^\/assets\/(\d{1,20})$/.exec(rest);
  if (m) return { kind: "asset", id: m[1] };
  m = /^\/download\/([^/]+)\/([^/]+)$/.exec(rest);
  if (m) return { kind: "download", tag: decode(m[1]), name: decode(m[2]) };
  return null;
}

function decode(part) {
  try {
    return decodeURIComponent(part);
  } catch {
    return part;
  }
}

async function listReleases(gateway, params) {
  const result = await cachedJson(gateway, `/releases?per_page=${LISTING_SIZE}`, (list) =>
    Array.isArray(list) ? list.filter((r) => r && !r.draft).map(shapeRelease) : null,
  );
  // The listing is never "not found" for a repository the token can read.
  if (result.status !== 200) return upstreamFailure(result.status === 404 ? 403 : result.status);
  const perPage = clamp(parseInt(params.get("per_page") || "30", 10), 1, LISTING_SIZE, 30);
  const page = clamp(parseInt(params.get("page") || "1", 10), 1, 1000, 1);
  const slice = result.data.slice((page - 1) * perPage, page * perPage);
  return jsonResponse(slice.map((r) => pointAtGateway(r, gateway)));
}

async function oneRelease(gateway, path) {
  const result = await cachedJson(gateway, path, (r) => (r && !r.draft ? shapeRelease(r) : null));
  if (result.status !== 200) return upstreamFailure(result.status);
  return jsonResponse(pointAtGateway(result.data, gateway));
}

async function assetByName(gateway, tag, name, request) {
  const result = await cachedJson(gateway, `/releases/tags/${encodeURIComponent(tag)}`, (r) =>
    r && !r.draft ? shapeRelease(r) : null,
  );
  if (result.status !== 200) return upstreamFailure(result.status);
  const found = result.data.assets.find((a) => a.name === name);
  if (!found) return message(404, "Not Found");
  return asset(gateway, String(found.id), request);
}

/** A release file: its description (JSON, as GitHub answers without octet-stream), a small JSON
 *  file read and rewritten, or a redirect to GitHub's signed download URL. */
async function asset(gateway, id, request) {
  const info = await cachedJson(gateway, `/releases/assets/${id}`, shapeAsset);
  if (info.status !== 200) return upstreamFailure(info.status);
  const accept = request.headers.get("Accept") || "";
  if (!accept.includes("application/octet-stream")) {
    return jsonResponse(pointAssetAtGateway(info.data, gateway));
  }
  const upstream = await github(gateway.config, `/releases/assets/${id}`, "application/octet-stream");
  const isManifest = info.data.name.toLowerCase().endsWith(".json") && info.data.size <= MANIFEST_LIMIT;
  if (isManifest) return manifestFile(gateway, upstream);
  if (isRedirect(upstream.status)) {
    const location = upstream.headers.get("Location");
    if (!location || (!location.startsWith("https://") && !isLocal(gateway.config.upstream))) {
      return message(502, "Gateway error");
    }
    return new Response(null, { status: 302, headers: { Location: location, "Cache-Control": "no-store" } });
  }
  if (!upstream.ok) return upstreamFailure(upstream.status);
  // GitHub always redirects a file; streaming a package through the Worker is not wanted.
  console.error("beta-gateway: GitHub answered a file without a redirect");
  return message(502, "Gateway error");
}

/** The JSON file's text, from the upstream answer or its redirect (followed by hand and without
 *  the token: the signed URL needs none), with every release URL inside pointed at the gateway. */
async function manifestFile(gateway, upstream) {
  let response = upstream;
  if (isRedirect(upstream.status)) {
    const location = upstream.headers.get("Location");
    if (!location) return message(502, "Gateway error");
    response = await fetch(location, { headers: { "User-Agent": USER_AGENT }, redirect: "follow" });
  }
  if (!response.ok) return upstreamFailure(response.status);
  const text = await response.text();
  if (text.length > MANIFEST_LIMIT) return message(502, "Gateway error");
  let data;
  try {
    data = JSON.parse(text);
  } catch {
    return message(502, "Gateway error");
  }
  return jsonResponse(rewriteUrls(data, gateway));
}

// ---- Shaping: only the fields the launcher reads, every URL on the gateway --------------------

function shapeRelease(r) {
  return {
    id: r.id,
    tag_name: r.tag_name,
    name: r.name ?? null,
    body: r.body ?? "",
    draft: false,
    prerelease: Boolean(r.prerelease),
    published_at: r.published_at ?? null,
    assets: Array.isArray(r.assets) ? r.assets.map(shapeAsset).filter(Boolean) : [],
  };
}

function shapeAsset(a) {
  if (!a || typeof a.id !== "number" || typeof a.name !== "string") return null;
  return { id: a.id, name: a.name, size: Number(a.size) || 0, content_type: a.content_type || "application/octet-stream" };
}

function pointAssetAtGateway(a, gateway) {
  const url = assetUrl(gateway, a.id);
  return { ...a, url, browser_download_url: url };
}

function pointAtGateway(release, gateway) {
  return { ...release, assets: release.assets.map((a) => pointAssetAtGateway(a, gateway)) };
}

function assetUrl(gateway, id) {
  return `${gateway.origin}/repos/${gateway.config.repo}/releases/assets/${id}`;
}

/** Points the release URLs inside a manifest (latest.json names the launcher bundle by its API
 *  URL, or by its download URL) at the gateway. Other strings are left alone. */
function rewriteUrls(value, gateway) {
  if (typeof value === "string") return rewriteUrl(value, gateway);
  if (Array.isArray(value)) return value.map((v) => rewriteUrls(v, gateway));
  if (value && typeof value === "object") {
    const out = {};
    for (const [k, v] of Object.entries(value)) out[k] = rewriteUrls(v, gateway);
    return out;
  }
  return value;
}

function rewriteUrl(text, gateway) {
  const { repo, upstream } = gateway.config;
  const api = new RegExp(`^${escape(upstream)}/repos/${escape(repo)}/releases/assets/(\\d+)$`, "i");
  let m = api.exec(text);
  if (m) return assetUrl(gateway, m[1]);
  const download = new RegExp(`^https://github\\.com/${escape(repo)}/releases/download/([^/]+)/([^/?#]+)$`, "i");
  m = download.exec(text);
  if (m) return `${gateway.origin}/repos/${repo}/releases/download/${m[1]}/${m[2]}`;
  return text;
}

function escape(text) {
  return text.replace(/[.*+?^${}()|[\]\\/]/g, "\\$&");
}

// ---- GitHub and the short cache -----------------------------------------------------------------

function github(config, path, accept) {
  return fetch(`${config.upstream}/repos/${config.repo}${path}`, {
    headers: {
      Authorization: `Bearer ${config.token}`,
      Accept: accept,
      "User-Agent": USER_AGENT,
      "X-GitHub-Api-Version": API_VERSION,
    },
    // Redirects are followed by hand, so the token never goes to another host.
    redirect: "manual",
  });
}

/** Per isolate, always; the Cache API too where the Worker has one (custom domains). */
const memory = new Map();

/** GitHub's JSON answer for path, shaped by shape (null: not found), reused for CACHE_SECONDS. */
async function cachedJson(gateway, path, shape) {
  const { config, ctx } = gateway;
  const key = `https://beta-gateway.cache/${encodeURIComponent(config.upstream)}/${config.repo}${path}`;
  const now = Date.now();
  const kept = memory.get(key);
  if (kept && kept.until > now) return { status: 200, data: kept.data };
  const edge = globalThis.caches && globalThis.caches.default;
  if (edge) {
    const hit = await edge.match(key);
    if (hit) {
      const data = await hit.json();
      memory.set(key, { data, until: now + CACHE_SECONDS * 1000 });
      return { status: 200, data };
    }
  }
  const response = await github(config, path, "application/vnd.github+json");
  if (response.status !== 200) return { status: response.status };
  const data = shape(await response.json());
  if (data == null) return { status: 404 };
  memory.set(key, { data, until: now + CACHE_SECONDS * 1000 });
  if (memory.size > 200) memory.delete(memory.keys().next().value);
  if (edge) {
    const stored = new Response(JSON.stringify(data), {
      headers: { "Content-Type": "application/json", "Cache-Control": `max-age=${CACHE_SECONDS}` },
    });
    const put = edge.put(key, stored);
    if (ctx && typeof ctx.waitUntil === "function") ctx.waitUntil(put);
    else await put;
  }
  return { status: 200, data };
}

// ---- Answers -----------------------------------------------------------------------------------

function isRedirect(status) {
  return status === 301 || status === 302 || status === 303 || status === 307 || status === 308;
}

function isLocal(base) {
  return /^http:\/\/(127\.0\.0\.1|localhost|\[::1\])(:\d+)?$/.test(base);
}

/** What GitHub's refusal becomes: not found stays not found; the gateway's own token being
 *  refused or rate-limited is the gateway's problem, not the tester's key. */
function upstreamFailure(status) {
  if (status === 404) return message(404, "Not Found");
  if (status === 401 || status === 403 || status === 429) {
    console.error(`beta-gateway: GitHub refused the gateway's token (HTTP ${status})`);
    return message(503, "Releases unavailable");
  }
  return message(502, "Gateway error");
}

function clamp(value, low, high, fallback) {
  return Number.isFinite(value) ? Math.min(high, Math.max(low, value)) : fallback;
}

function jsonResponse(data) {
  return new Response(JSON.stringify(data), {
    status: 200,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "private, no-store" },
  });
}

function message(status, text, extra = {}) {
  return new Response(JSON.stringify({ message: text }), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store", ...extra },
  });
}
