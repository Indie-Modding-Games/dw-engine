// node --test: the gateway's fetch handler with GitHub replaced by a mocked global fetch.
import { test, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";
import worker from "./worker.js";

const KEY = "placeholder-beta-key-for-tests-0123456789";
const PREVIOUS = "placeholder-previous-beta-key-0123456789";
const TOKEN = "placeholder-github-token-for-tests";
const REPO = "team/game";
const GATEWAY = "https://beta.example.test";
const SIGNED = "https://objects.example.test/signed/file?sig=short-lived";

let upstreamCount = 0;
let calls;
let realFetch;

/** A fresh fake GitHub per test (its own base URL, so the gateway's short cache never mixes
 *  tests). routes: "<path>" → handler(init) returning a Response. */
function fakeGithub(routes) {
  const base = `https://gh-${++upstreamCount}.example.test`;
  calls = [];
  globalThis.fetch = async (input, init = {}) => {
    const url = String(input);
    calls.push({ url, init });
    const path = url.startsWith(base) ? url.slice(base.length) : url;
    const route = routes[path];
    if (!route) return new Response('{"message":"Not Found"}', { status: 404 });
    return route(init);
  };
  return { base, env: { BETA_KEY: KEY, GITHUB_TOKEN: TOKEN, GITHUB_REPO: REPO, GITHUB_API: base } };
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), { status, headers: { "Content-Type": "application/json" } });
}

function redirect(location) {
  return new Response(null, { status: 302, headers: { Location: location } });
}

async function ask(env, path, { key = KEY, accept = "application/vnd.github+json", method = "GET" } = {}) {
  const headers = { Accept: accept };
  if (key !== null) headers.Authorization = `Bearer ${key}`;
  return worker.fetch(new Request(`${GATEWAY}${path}`, { method, headers }), env, { waitUntil() {} });
}

/** Everything a response shows: status, headers and body. */
async function visible(response) {
  const headers = [...response.headers].map(([k, v]) => `${k}: ${v}`).join("\n");
  return `${response.status}\n${headers}\n\n${await response.clone().text()}`;
}

function releases(base) {
  const api = `${base}/repos/${REPO}/releases`;
  return [
    { id: 9, tag_name: "draft", draft: true, assets: [] },
    {
      id: 7, tag_name: "v0.2.0-beta.2", name: "Beta 2", body: "## Changes", draft: false, prerelease: true,
      published_at: "2026-11-04T12:00:00Z", author: { login: "someone" }, upload_url: `${api}/7/assets{?name}`,
      assets: [
        { id: 11, name: "game-manifest.json", size: 300, url: `${api}/assets/11`, browser_download_url: `https://github.com/${REPO}/releases/download/v0.2.0-beta.2/game-manifest.json`, uploader: { login: "someone" } },
        { id: 12, name: "dwe-0.2.0-linux-x86_64.tar.xz", size: 5000000, url: `${api}/assets/12`, browser_download_url: "https://github.com/x" },
        { id: 13, name: "latest.json", size: 400, url: `${api}/assets/13` },
        { id: 14, name: "dwe-launcher.AppImage", size: 9000000, url: `${api}/assets/14` },
      ],
    },
    { id: 5, tag_name: "v0.1.0-beta.1", name: "", draft: false, prerelease: true, assets: [] },
  ];
}

function assetInfo(base, id, name, size) {
  return () => json({ id, name, size, url: `${base}/repos/${REPO}/releases/assets/${id}`, uploader: { login: "x" } });
}

beforeEach(() => {
  realFetch = globalThis.fetch;
});
afterEach(() => {
  globalThis.fetch = realFetch;
});

test("no key, a wrong key or a key of another length: 401 with a JSON message", async () => {
  const { env } = fakeGithub({});
  for (const key of [null, "", "wrong", KEY.slice(0, -1), KEY + "x", KEY.toUpperCase()]) {
    const response = await ask(env, `/repos/${REPO}/releases`, { key });
    assert.equal(response.status, 401, `key ${key}`);
    assert.match((await response.json()).message, /beta key/);
  }
  assert.equal(calls.length, 0, "GitHub is never asked for an unauthorized request");
});

test("after a key change both the new key and the previous one open the releases", async () => {
  let base;
  const fake = fakeGithub({ [`/repos/${REPO}/releases?per_page=100`]: () => json(releases(base)) });
  base = fake.base;
  const env = { ...fake.env, BETA_KEY_PREVIOUS: PREVIOUS };
  for (const key of [KEY, PREVIOUS, ` ${PREVIOUS} `]) {
    assert.equal((await ask(env, `/repos/${REPO}/releases`, { key })).status, 200, `key ${key}`);
  }
  for (const key of [null, "", "wrong", PREVIOUS.slice(0, -1), PREVIOUS + "x", KEY + PREVIOUS, TOKEN]) {
    assert.equal((await ask(env, `/repos/${REPO}/releases`, { key })).status, 401, `key ${key}`);
  }
  // Without BETA_KEY_PREVIOUS (or with it empty), only the new key opens them.
  for (const previous of [undefined, "", "   "]) {
    const only = { ...fake.env, BETA_KEY_PREVIOUS: previous };
    assert.equal((await ask(only, `/repos/${REPO}/releases`, { key: PREVIOUS })).status, 401);
    assert.equal((await ask(only, `/repos/${REPO}/releases`, { key: KEY })).status, 200);
  }
});

test("a gateway without its settings refuses everything", async () => {
  const { env } = fakeGithub({});
  for (const broken of [{ ...env, BETA_KEY: "" }, { ...env, BETA_KEY: "short" }, { ...env, BETA_KEY_PREVIOUS: "short" }, { ...env, GITHUB_TOKEN: "" }, { ...env, GITHUB_REPO: "nope" }]) {
    const response = await ask(broken, `/repos/${REPO}/releases`, { key: broken.BETA_KEY || "x" });
    assert.equal(response.status, 503);
  }
  const post = await worker.fetch(new Request(`${GATEWAY}/repos/${REPO}/releases`, { method: "POST", headers: { Authorization: `Bearer ${KEY}` } }), env, {});
  assert.equal(post.status, 405);
});

test("the listing: no drafts, only the launcher's fields, every file on the gateway, cached", async () => {
  const { base, env } = fakeGithub({ [`/repos/${REPO}/releases?per_page=100`]: () => json(releases(base)) });
  const response = await ask(env, `/repos/${REPO}/releases?per_page=20`);
  assert.equal(response.status, 200);
  const listed = await response.json();
  assert.deepEqual(listed.map((r) => r.tag_name), ["v0.2.0-beta.2", "v0.1.0-beta.1"]);
  const [release] = listed;
  assert.equal(release.author, undefined);
  assert.equal(release.upload_url, undefined);
  assert.equal(release.body, "## Changes");
  for (const asset of release.assets) {
    assert.equal(asset.url, `${GATEWAY}/repos/${REPO}/releases/assets/${asset.id}`);
    assert.equal(asset.browser_download_url, asset.url);
    assert.equal(asset.uploader, undefined);
  }
  // The token went to GitHub, with redirects left to the gateway.
  assert.equal(calls[0].init.headers.Authorization, `Bearer ${TOKEN}`);
  assert.equal(calls[0].init.redirect, "manual");
  // per_page is honoured, and a second request within the minute does not ask GitHub again.
  const one = await (await ask(env, `/repos/${REPO}/releases?per_page=1`)).json();
  assert.equal(one.length, 1);
  assert.equal(calls.length, 1);
  // Another repository's path is not served.
  assert.equal((await ask(env, `/repos/other/repo/releases`)).status, 404);
});

test("a package is a 302 to GitHub's signed URL, never streamed and never with a token", async () => {
  const { base, env } = fakeGithub({
    [`/repos/${REPO}/releases/assets/12`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? redirect(SIGNED) : assetInfo(base, 12, "dwe-0.2.0-linux-x86_64.tar.xz", 5000000)(),
  });
  const response = await ask(env, `/repos/${REPO}/releases/assets/12`, { accept: "application/octet-stream" });
  assert.equal(response.status, 302);
  assert.equal(response.headers.get("Location"), SIGNED);
  assert.equal(response.headers.get("Authorization"), null);
  assert.equal(calls.filter((c) => c.url === SIGNED).length, 0, "the gateway does not fetch the package itself");
  assert.ok(calls.every((c) => c.init.redirect === "manual"));

  // Without octet-stream: the file's description, pointed at the gateway.
  const described = await (await ask(env, `/repos/${REPO}/releases/assets/12`)).json();
  assert.equal(described.url, `${GATEWAY}/repos/${REPO}/releases/assets/12`);
  assert.equal(described.uploader, undefined);
});

test("a file GitHub answers without a redirect is refused rather than streamed", async () => {
  const { base, env } = fakeGithub({
    [`/repos/${REPO}/releases/assets/12`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? new Response("big bytes") : assetInfo(base, 12, "big.tar.xz", 5000000)(),
  });
  const response = await ask(env, `/repos/${REPO}/releases/assets/12`, { accept: "application/octet-stream" });
  assert.equal(response.status, 502);
});

test("latest.json: read through its redirect without the token, its bundle URLs on the gateway", async () => {
  const latest = (base) => ({
    version: "0.2.0",
    notes: "v0.2.0-beta.2",
    platforms: {
      "linux-x86_64": { signature: "c2ln", url: `${base}/repos/${REPO}/releases/assets/14` },
      "linux-aarch64": { signature: "c2ln", url: `https://github.com/${REPO}/releases/download/v0.2.0-beta.2/dwe-launcher.AppImage` },
      other: { signature: "c2ln", url: "https://example.org/untouched" },
    },
  });
  const signedJson = "https://objects.example.test/signed/latest.json?sig=x";
  let upstream;
  const fake = fakeGithub({
    [`/repos/${REPO}/releases/assets/13`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? redirect(signedJson) : assetInfo(upstream, 13, "latest.json", 400)(),
    [signedJson]: (init) => {
      assert.equal(init.headers.Authorization, undefined, "no token to the signed URL");
      return json(latest(upstream));
    },
  });
  upstream = fake.base;
  const response = await ask(fake.env, `/repos/${REPO}/releases/assets/13`, { accept: "application/octet-stream" });
  assert.equal(response.status, 200);
  const served = await response.json();
  assert.equal(served.platforms["linux-x86_64"].url, `${GATEWAY}/repos/${REPO}/releases/assets/14`);
  assert.equal(served.platforms["linux-aarch64"].url, `${GATEWAY}/repos/${REPO}/releases/download/v0.2.0-beta.2/dwe-launcher.AppImage`);
  assert.equal(served.platforms.other.url, "https://example.org/untouched");
  assert.equal(served.version, "0.2.0");
});

test("game-manifest.json is served as it is (it names files, no URLs)", async () => {
  const manifest = { schema: 1, version: "0.2.0-beta.2", extractor_version: 13, packages: { "linux-x86_64": { asset: "dwe-0.2.0-linux-x86_64.tar.xz", size: 5, sha256: "0".repeat(64), signature: "c2ln" } } };
  const { base, env } = fakeGithub({
    [`/repos/${REPO}/releases/assets/11`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? json(manifest) : assetInfo(base, 11, "game-manifest.json", 300)(),
  });
  const response = await ask(env, `/repos/${REPO}/releases/assets/11`, { accept: "application/octet-stream" });
  assert.deepEqual(await response.json(), manifest);
});

test("a file by release and name resolves to its asset", async () => {
  const { base, env } = fakeGithub({
    [`/repos/${REPO}/releases/tags/v0.2.0-beta.2`]: () => json(releases(base)[1]),
    [`/repos/${REPO}/releases/assets/14`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? redirect(SIGNED) : assetInfo(base, 14, "dwe-launcher.AppImage", 9000000)(),
  });
  const response = await ask(env, `/repos/${REPO}/releases/download/v0.2.0-beta.2/dwe-launcher.AppImage`, { accept: "application/octet-stream" });
  assert.equal(response.status, 302);
  assert.equal(response.headers.get("Location"), SIGNED);
  const tag = await (await ask(env, `/repos/${REPO}/releases/tags/v0.2.0-beta.2`)).json();
  assert.equal(tag.assets[0].url, `${GATEWAY}/repos/${REPO}/releases/assets/11`);
  assert.equal((await ask(env, `/repos/${REPO}/releases/download/v0.2.0-beta.2/missing.bin`, { accept: "application/octet-stream" })).status, 404);
});

test("GitHub refusing the gateway's own token is not the tester's key: 503, nothing leaked", async () => {
  const { env } = fakeGithub({
    [`/repos/${REPO}/releases?per_page=100`]: () => json({ message: "Bad credentials" }, 401),
    [`/repos/${REPO}/releases/assets/12`]: () => json({ message: "Not Found" }, 404),
  });
  assert.equal((await ask(env, `/repos/${REPO}/releases`)).status, 503);
  assert.equal((await ask(env, `/repos/${REPO}/releases/assets/12`, { accept: "application/octet-stream" })).status, 404);
  globalThis.fetch = async () => {
    throw new Error(`connection to ${env.GITHUB_API} with ${TOKEN} failed`);
  };
  const failed = await ask({ ...env, GITHUB_REPO: "team/other" }, `/repos/team/other/releases`);
  assert.equal(failed.status, 502);
  assert.ok(!(await visible(failed)).includes(TOKEN));
});

test("the GitHub token and the beta key never appear in any answer", async () => {
  const signedJson = "https://objects.example.test/signed/latest.json?sig=y";
  let base;
  const fake = fakeGithub({
    [`/repos/${REPO}/releases?per_page=100`]: () => json(releases(base)),
    [`/repos/${REPO}/releases/tags/v0.2.0-beta.2`]: () => json(releases(base)[1]),
    [`/repos/${REPO}/releases/assets/12`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? redirect(SIGNED) : assetInfo(base, 12, "p.tar.xz", 5000000)(),
    [`/repos/${REPO}/releases/assets/13`]: (init) =>
      init.headers.Accept === "application/octet-stream" ? redirect(signedJson) : assetInfo(base, 13, "latest.json", 400)(),
    [signedJson]: () => json({ version: "1.0.0", platforms: { "linux-x86_64": { url: `${base}/repos/${REPO}/releases/assets/12`, signature: "s" } } }),
  });
  base = fake.base;
  const requests = [
    [`/repos/${REPO}/releases`, {}],
    [`/repos/${REPO}/releases/tags/v0.2.0-beta.2`, {}],
    [`/repos/${REPO}/releases/latest`, {}],
    [`/repos/${REPO}/releases/assets/12`, { accept: "application/octet-stream" }],
    [`/repos/${REPO}/releases/assets/12`, {}],
    [`/repos/${REPO}/releases/assets/13`, { accept: "application/octet-stream" }],
    [`/repos/${REPO}/releases/assets/99`, { accept: "application/octet-stream" }],
    [`/repos/${REPO}/releases`, { key: "wrong" }],
    [`/`, {}],
  ];
  const env = { ...fake.env, BETA_KEY_PREVIOUS: PREVIOUS };
  for (const [path, options] of requests) {
    const text = await visible(await ask(env, path, options));
    assert.ok(!text.includes(TOKEN), `${path} shows the GitHub token`);
    assert.ok(!text.includes(KEY), `${path} shows the beta key`);
    assert.ok(!text.includes(PREVIOUS), `${path} shows the previous beta key`);
    assert.ok(!text.includes(fake.base), `${path} shows the upstream server`);
  }
});
