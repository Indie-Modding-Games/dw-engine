# beta-gateway

The beta gateway: a Cloudflare Worker (`apps/beta-gateway/worker.js`, a single JavaScript file with no
dependencies) between the [launcher](../apps/launcher.md) and the releases in the GitHub repository. The launcher
never talks to GitHub: it asks the gateway for the releases, the game packages and its own updates, with the
**beta key** in the header. Without a key, or with a different one, the gateway shows nothing.

The key is compiled into the launcher (`release.yml`, from the repository secret `DWE_BETA_KEY`, with the same value
as `BETA_KEY`): the tester never types it. It is a light barrier, not a secret: the repository and its releases are
public and anyone who has the launcher can extract it.

How to deploy it, create the token, generate and change the key: the
[gateway README](https://github.com/Indie-Modding-Games/dw-engine/blob/main/apps/beta-gateway/README.md).

```text
launcher ──(Authorization: Bearer <key>)──► gateway ──(read-only token)──► GitHub API
    ▲                                          │
    └──── 302 to a short signed link ◄─────────┘   (packages do not go through the Worker)
```

## What it answers

The requests to GitHub's releases API that the launcher and its updater use, for a single repository:

| Request | Response |
|---|---|
| `GET /repos/<repo>/releases` | The published releases (no drafts), with only the fields the launcher reads, `prerelease` included |
| `GET /repos/<repo>/releases/latest` | The newest stable release (GitHub never returns a prerelease here) |
| `GET /repos/<repo>/releases/tags/<tag>` | A release by its tag |
| `GET /repos/<repo>/releases/assets/<id>` | A file: the small JSONs (`game-manifest.json`, `latest.json`) are served by the gateway; any other is a `302` to the signed GitHub link |
| `GET /repos/<repo>/releases/download/<tag>/<name>` | The same, by release and name |

The launcher's two channels come from these responses: "Testing" takes the newest release in the list, prerelease or
not; "Stable", the newest stable one in the list or, if it is not among the first 20, `releases/latest` (`404`
while there is none). Every merge into `main` publishes a prerelease (`release.yml`) and `promote.yml` turns it into
stable without building anything ([publishing a release](../apps/launcher.md#publishing-a-release)).

## Rules

- **The key** is compared in constant time with the `BETA_KEY` secret and, if set, with `BETA_KEY_PREVIOUS`, the
  previous key during a change, so that launchers already distributed keep working until they update (both at least
  24 characters; with fewer, the gateway rejects everything). Without it: `401`, which the launcher shows as "The
  beta server no longer accepts this launcher. Update the launcher or reinstall it from the latest release."
- **The URLs** of the files (also those inside `latest.json`) are rewritten to point to the gateway. The launcher
  rejects a file from another server, so the key never goes anywhere else.
- **Large downloads** do not go through the Worker: the gateway asks GitHub for the file without following the
  redirect and returns that `302`. When following it, the launcher's HTTP client drops the `Authorization` header
  because the server changes.
- **The GitHub token** (secret `GITHUB_TOKEN`, read-only access to contents) never appears in a response.
- **Cache:** lists and descriptions are kept for 60 s.
- **Errors:** if GitHub rejects the gateway's token, the launcher receives `503` ("could not connect"), not "wrong
  key": the problem is the gateway's, not the tester's.

## Settings

| Name | Type | What it is |
|---|---|---|
| `BETA_KEY` | Secret | The beta key; the same value as the repository secret `DWE_BETA_KEY` |
| `BETA_KEY_PREVIOUS` | Secret, optional | The previous key, only while a key change lasts |
| `GITHUB_TOKEN` | Secret | The read-only GitHub token |
| `GITHUB_REPO` | Text | `Indie-Modding-Games/dw-engine` |
| `GITHUB_API` | Text | Only in local tests: the URL of the server that stands in for GitHub |

The launcher has the gateway URL compiled in: `DWE_LAUNCHER_GATEWAY` when building by hand and the repository
variable `DWE_BETA_GATEWAY` in `release.yml`, which does not build without it. Without a URL it points to
`https://beta-gateway.invalid`, which does not exist. `DWE_LAUNCHER_API` changes it for one run. The key is the same:
`DWE_LAUNCHER_BETA_KEY` when building (in `release.yml`, the secret `DWE_BETA_KEY`, without which it does not
publish) and `DWE_LAUNCHER_BETA_KEY` when running, for one run.

## Tests

```bash
node --test apps/beta-gateway/     # the Worker with a simulated fetch (worker.test.js)
scripts/launcher.sh test               # includes through_the_beta_gateway, end to end
```

`through_the_beta_gateway` (`apps/launcher/src-tauri/tests/fake_release.rs`) starts a server that stands in for
GitHub (`apps/launcher/tools/fake_release_server.py`), the gateway in front of it with `dev/serve.js` and the
launcher's client against it: without a key, with a different one, with the previous one, the release list, the game
installation and `latest.json`.
CI runs both when a pull request touches the launcher, the gateway or the release scripts.

## Pending

- Throttle by IP the attempts with a wrong key (a Cloudflare rate-limit rule is enough).
