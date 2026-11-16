# Closed beta gateway

A Cloudflare Worker (`worker.js`, a single JavaScript file with no dependencies) between the launcher and the
repository's releases. The repository has been public since 2026-11-05, but the launcher still talks only to the
gateway: it is the release server compiled into it. Whoever tests the beta only needs the launcher: the **beta key**,
a single shared key that the maintainer generates, is **inside the launcher** from the moment it is published, and the
tester never types it. With it the launcher sees the releases, downloads the game and gets updates of the game and of
the launcher itself; without it, nothing.

**It is a light barrier, not a secret.** The repository and its releases are public, and the key is in the program
that is handed out: whoever has the launcher can extract it. It only stops the gateway from answering anyone.

```
launcher ──(Authorization: Bearer <beta key>)──► gateway ──(read-only token)──► GitHub API
    ▲                                                │
    └──── 302 to a signed, short-lived link ◄────────┘   (the packages do not go through the Worker)
```

## What it does

For a single repository, it answers the GitHub API requests that the launcher and its updater use:

| Request | Response |
|---|---|
| `GET /repos/<repo>/releases` | The releases (no drafts), with only the fields the launcher reads. |
| `GET /repos/<repo>/releases/latest`, `/releases/tags/<tag>` | One release, likewise. |
| `GET /repos/<repo>/releases/assets/<id>` with `Accept: application/octet-stream` | A file: the gateway serves the small JSONs (`game-manifest.json`, `latest.json`); any other is a **302** to GitHub's signed link. |
| `GET /repos/<repo>/releases/download/<tag>/<nombre>` | The same, by release and name. |

- **The key:** it is compared in constant time with the `BETA_KEY` secret and, if it is set, with
  `BETA_KEY_PREVIOUS` (the previous key, while a key change lasts); always with both, so the timing does not say which
  one matches. With no key or another one: `401` with a JSON message, which the launcher turns into «El servidor
  de la beta ya no acepta este lanzador. Actualiza el lanzador o vuelve a instalarlo desde la última versión
  publicada.» (the launcher's Spanish text: the beta server no longer accepts this launcher; update it or reinstall it
  from the latest release).
- **The URLs:** all the file URLs (`url`, `browser_download_url` and those inside `latest.json`) are rewritten to
  point at the gateway itself. The launcher rejects a file that is on another server, and so does its updater, so the
  key never goes anywhere else.
- **Large downloads:** the gateway asks GitHub for the file without following the redirect and returns that `302`.
  The signed link carries no token; when following it, the launcher's HTTP client drops the `Authorization` header
  because the server changes (tested in `apps/launcher/src-tauri/tests/fake_release.rs`).
- **The GitHub token** lives only in the `GITHUB_TOKEN` secret: it never appears in a response, neither in the body
  nor in the headers (tested in `worker.test.js`).
- **Cache:** the lists and the file descriptions are kept for 60 s (in memory and, with a custom domain, in the
  Cloudflare cache), far below the token's limit of 5000 requests per hour.
- If GitHub rejects the gateway's token (expired, revoked, limit), the launcher gets `503` («no se pudo conectar»,
  "could not connect"), not "wrong key": the problem is the gateway's, not the tester's. The Worker's log says so.

## Worker settings

| Name | Type | Value |
|---|---|---|
| `BETA_KEY` | Secret | The beta key (at least 24 characters; with fewer, the gateway rejects everything). The same value as the repository's `DWE_BETA_KEY` secret. |
| `BETA_KEY_PREVIOUS` | Secret, optional | The previous key, only during a key change (below). If it is set, also 24 characters or more. |
| `GITHUB_TOKEN` | Secret | The read-only GitHub token (see below). |
| `GITHUB_REPO` | Text variable | `Indie-Modding-Games/dw-engine` |

`GITHUB_API` exists only for local tests; it is not set on Cloudflare.

## Deploying it (Cloudflare dashboard)

1. **Workers & Pages → Create → Create Worker** (the "Hello World" template). Name: `dwe-beta-gateway`. **Deploy**.
2. **Edit code**: delete the contents of `worker.js` in the editor, paste all of `apps/beta-gateway/worker.js` and
   press **Deploy**.
3. **Settings → Variables and Secrets → Add**, and then **Deploy**:
   - `BETA_KEY`, type **Secret**, the generated key (below).
   - `GITHUB_TOKEN`, type **Secret**, the GitHub token (below).
   - `GITHUB_REPO`, type **Text**, `Indie-Modding-Games/dw-engine`.
4. The address ends up in **Settings → Domains & Routes**: `https://dwe-beta-gateway.<subdominio>.workers.dev`.
   That is the URL that is compiled into the launcher (below). A custom domain is optional (it turns on the
   Cloudflare cache in addition to the in-memory one).
5. Check it without leaving the key in the terminal history:

   ```bash
   read -rs CLAVE
   curl -s -o /dev/null -w '%{http_code}\n' https://dwe-beta-gateway.<subdominio>.workers.dev/repos/Indie-Modding-Games/dw-engine/releases
   curl -s -H "Authorization: Bearer $CLAVE" https://dwe-beta-gateway.<subdominio>.workers.dev/repos/Indie-Modding-Games/dw-engine/releases | head -c 300
   ```

   The first gives `401`; the second, the list of releases with gateway URLs. A **draft** release does not show up
   until it is published (the read-only token does not see drafts).

The free Workers plan (100,000 requests a day) is more than enough for the beta: the launcher makes a few requests
each time it looks for updates.

## The beta key

Generate it on the maintainer's computer, never in a file of the repository:

```bash
openssl rand -base64 32
```

It is pasted in **two** places, with the same value:

- the Worker's `BETA_KEY` secret (above);
- the repository secret `DWE_BETA_KEY` (GitHub → **Settings → Secrets and variables → Actions → Secrets → New
  repository secret**). `.github/workflows/release.yml` compiles it into the launcher and refuses to publish if it is
  missing or shorter than 24 characters. It is never written to the run's log.

Nothing is sent to the testers: the launcher already carries it. Old launchers that kept it in the system keyring
delete it from there the first time they update.

**Changing it** (if you want to cut off access for whoever has an old launcher). Since the key is inside the launcher,
a change needs a new launcher release, and the previous key stays valid while the launchers update themselves:

1. Generate a new one with `openssl rand -base64 32`.
2. In the Worker: copy the current `BETA_KEY` to `BETA_KEY_PREVIOUS`, paste the new one into `BETA_KEY` and
   **Deploy**. Both work.
3. In the repository: paste the new one into `DWE_BETA_KEY` and publish a release (a merge into `main`, or
   *Actions → Release → Run workflow*). The launchers update with the previous key and then bring the new one.
4. When the testers already have the new launcher (a few days), delete `BETA_KEY_PREVIOUS` and **Deploy**. A launcher
   that did not update will say «El servidor de la beta ya no acepta este lanzador…» and will have to be reinstalled
   from the latest release.

If the key really leaks, step 2 can be skipped (without `BETA_KEY_PREVIOUS`): the old one stops working at once and
all the testers have to reinstall the launcher.

## The GitHub token

A *fine-grained personal access token* that can only read the contents of `dw-engine`. With the repository public,
the gateway could read the releases without it, but the token raises the API request limit (5000 per hour instead of
60 per IP, which all the Workers behind the same Cloudflare egress share):

1. GitHub → **Settings → Developer settings → Personal access tokens → Fine-grained tokens → Generate new token**.
2. **Resource owner:** `Indie-Modding-Games` (if the organization asks for token approval, an owner gives it).
3. **Expiration:** whatever the organization allows (for example 90 days); before it expires, create another and
   change the `GITHUB_TOKEN` secret the same way as the key.
4. **Repository access:** *Only select repositories* → `dw-engine`.
5. **Permissions → Repository permissions → Contents: Read-only**. GitHub adds *Metadata: Read-only* by itself. No
   other permission.
6. Generate it, copy it and paste it straight into the Worker's `GITHUB_TOKEN` secret. It is not kept anywhere else.

## The URL in the launcher

The launcher uses as its release server the URL it was compiled with:

- `DWE_LAUNCHER_GATEWAY=https://dwe-beta-gateway.<subdominio>.workers.dev scripts/launcher.sh build` (or `bundle`).
- In `.github/workflows/release.yml`, the repository variable `DWE_BETA_GATEWAY` (Settings → Secrets and
  variables → Actions → Variables); the workflow refuses to build without it.
- Without that variable, the build points at `https://beta-gateway.invalid`, which does not exist: a development build
  never talks to a real server.
- `DWE_LAUNCHER_API=<url>` changes it for one run (tests). The server is not saved in `settings.json`.

The key is compiled in the same way: `DWE_LAUNCHER_BETA_KEY=<clave> scripts/launcher.sh build`, and in `release.yml`
from the `DWE_BETA_KEY` secret. `DWE_LAUNCHER_BETA_KEY=<clave>` when running the launcher changes it for that run
(tests). A hand-made build without a key sends the one an old launcher saved in the keyring, if there is one.

## Testing locally

Without installing anything (node comes with the host or with the `dwe-launcher-dev` image):

```bash
node --test apps/beta-gateway/          # the Worker with a mocked fetch
scripts/launcher.sh test                    # includes the end-to-end test (below)
```

The `through_the_beta_gateway` test in `apps/launcher/src-tauri/tests/fake_release.rs` starts
`apps/launcher/tools/fake_release_server.py` as GitHub (which redirects each file to another server and rejects any
request with `Authorization` there, like GitHub's storage), the gateway with `dev/serve.js` in front, and the
launcher's client against the gateway: with no key or another one, with the previous key (`BETA_KEY_PREVIOUS`), the
list of releases, the game install and `latest.json` with its launcher.

By hand:

```bash
python3 apps/launcher/tools/fake_release_server.py <carpeta> --port 8765 --token token-falso --repo Indie-Modding-Games/dw-engine &
BETA_KEY=clave-de-prueba-de-al-menos-24-caracteres GITHUB_TOKEN=token-falso GITHUB_REPO=Indie-Modding-Games/dw-engine \
  GITHUB_API=http://127.0.0.1:8765 node apps/beta-gateway/dev/serve.js --port 8787 &
DWE_LAUNCHER_API=http://127.0.0.1:8787 DWE_LAUNCHER_BETA_KEY=clave-de-prueba-de-al-menos-24-caracteres \
  build/launcher/release/dwe-launcher
```

## Pending

- Slow down by IP the attempts with a wrong key (a Cloudflare *rate limiting* rule works if needed).
