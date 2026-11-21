# Launcher add-ons

The [launcher](../modules/apps/launcher.md) supports community **add-ons**: small HTML pages that add things to its
interface (an icon in the sidebar, a page on the home screen, cards in the news panel and a settings section). This
guide explains how to make one, what it can do (API v1) and how the player is protected.

The complete example is in `apps/launcher/addons/example-notes/` ("Notes": a page where the player jots down what
they want, with its icon in the bar, a news card and a settings section). To get started, copy it.

## What an add-on is

A folder (or a `.zip` of it) with:

```text
my-addon/
├── addon.json      the manifest
├── index.html      the page (the one "entry" names)
├── dwe-addons.js   the bridge client (copy it from apps/launcher/addons/sdk/)
├── main.js, style.css, icon.svg…
```

### addon.json

```json
{
  "id": "org.example.notes",
  "name": "Notes",
  "version": "1.0.0",
  "author": "Your name",
  "description": "What it does, in one or two sentences.",
  "api": 1,
  "entry": "index.html",
  "slots": ["sidebar", "tabs", "news", "settings"],
  "permissions": { "storage": true, "notify": true, "hosts": ["api.example.org"] }
}
```

| Field | What it is |
|---|---|
| `id` | A unique identifier in the style of a reversed domain: two or more parts separated by dots, with lowercase letters, digits and hyphens (`org.example.notes`), 3 to 64 characters. It is also the name of its folder. |
| `name` | Visible name, up to 40 characters. A single text: the manifest has no per-language `name` or `description`; the labels of its slots do (see `label` below). |
| `version` | Semantic version (`1.0.0`, `1.2.0-beta.1`). |
| `author` | Author, up to 60 characters. |
| `description` | Optional, up to 300 characters. It is shown when installing it. |
| `api` | The API version it is made for. Today only `1` exists; the launcher rejects any other. |
| `entry` | The HTML page, inside the folder. |
| `slots` | Where it appears: `sidebar` (sidebar), `tabs` (a page in the center of the home screen), `news` (news), `settings` (settings). At least one; `sidebar` needs `tabs` (the icon opens the page). |
| `permissions` | Optional. `storage` (keep its data), `notify` (short notices) and `hosts` (the servers it can connect to, up to 8: only the name and, if needed, the port, like `api.example.org` or `127.0.0.1:8779`; no `https://`, paths or wildcards). An unknown permission invalidates the manifest. |

## How it runs

- The launcher keeps each add-on in `<data>/addons/<id>/` and its data in `<data>/addon-storage/<id>.json` (outside
  its folder, so an update keeps them).
- It serves its files with a scheme of its own, `dweaddon://localhost/<id>/…` (on Windows,
  `http://dweaddon.localhost/<id>/…`), which only delivers files from the folder of **that** add-on, only if it is
  active, only of known types (HTML, JS, CSS, JSON, SVG, PNG, JPEG, WebP, GIF, fonts, text) and never following a link
  that leaves the folder.
- Each response carries a strict security policy (CSP): scripts, styles, images and fonts only from its folder,
  `connect-src 'none'` (neither `fetch` nor WebSocket: the network goes through `net.fetch`), no frames, no forms, no
  `eval`. No inline scripts either: put the code in `.js` files. Inline styles are allowed.
- The page is loaded in an `<iframe sandbox="allow-scripts">`: it runs in an opaque origin, with no access to the
  launcher's page, its IPC, cookies or `localStorage`, no pop-up windows and no navigation of the main window.
- It loads `dwe-addons.js` with a classic `<script>` tag (not `type="module"`: modules do not load in an opaque
  origin).
- Active add-ons are loaded when the launcher opens. The same page is opened in three **views**, which the client
  indicates in `DweAddons.view`:
  - `background`: always loaded and invisible. The interface pieces are registered here.
  - `tab`: the content of its page (if it registered one).
  - `settings`: its section in Settings → Add-ons (if it registered one).

## The v1 API

Everything goes through the `dwe-addons.js` client, which defines `window.DweAddons`. Each call returns a promise; if
the launcher rejects it, the promise fails with an `Error` whose `code` says why (`addon-denied`, `addon-host`,
`addon-storage-full`, `network`, `timeout`…).

```js
const dwe = window.DweAddons;
await dwe.connect();                         // { api: 1, view, id }
if (dwe.view === "background") {
  await dwe.ui.sidebar({ label: { es: "Notas", en: "Notes" }, icon: "icon.svg" });
  await dwe.ui.tab({ label: { es: "Notas", en: "Notes" } });
}
```

| Call | What it does | Requires |
|---|---|---|
| `connect()` | Greets the launcher (retries for a few seconds). Returns `{ api, view, id }`. | — |
| `context()` | Read-only data: `api`, the add-on (`id`, `version`, its permissions), `launcher_version`, `game_version`, `game_installed`, `content_installed`, `language` (`en`, `es`, `pt`, `fr`, `de`, `ru`, `zh`, `ko` or `ja`) and `platform`. **Never** paths, the beta key, the versions server or the disc. | — |
| `ui.sidebar({ label, icon })` | An icon in the sidebar that opens its page. `icon` is an `.svg`, `.png` or `.webp` from its folder (shown as an image, never as HTML). | slot `sidebar` |
| `ui.tab({ label, page? })` | Its page in the center of the home screen (`page`, by default `entry`) in the `tab` view. It is opened from its icon or from the "Add-ons" pill. | slot `tabs` |
| `ui.settings({ label, page? })` | A section in Settings → Add-ons with its page in the `settings` view. | slot `settings` |
| `ui.news(items)` | Up to 3 news cards: `{ title, body, date? }`, text only (the launcher paints them and adds "Add-on “name”"). Each call replaces the previous ones. | slot `news` |
| `ui.resize(height)`, `ui.autoResize()` | Changes the height of its frame (80 to 1600 px); `autoResize` fits it to the content. Only in the `tab` and `settings` views. | — |
| `storage.get(key)`, `.set(key, value)`, `.remove(key)`, `.keys()` | Its key-value store: JSON values, keys up to 128 characters, **256 KB** in total. | `storage` |
| `notify(text, kind?)` | A short notice (up to 200 characters, at most one every 3 s) with its name. `kind`: `info` or `success`. | `notify` |
| `net.fetch(url, { method?, headers?, body? })` | A `GET` or `POST` request made by the launcher. Returns `{ status, content_type, body }` (text, up to 1 MB). | `hosts` |
| `on(event, listener)` | Events: `game.status` (`{ running, busy, ready, gameVersion, language }`, every time it changes) and `language` (`{ language }`). | — |

Labels (`label`) can be a text or an object by language code (`{ en, es, fr, ... }`); the launcher picks the player's
language, else `en`, else the first one.

### net.fetch

- Only to the declared and accepted `hosts`, comparing exact name and port (`api.example.org` does not include
  `www.example.org`).
- Only `https`; `http` only to this same computer (`127.0.0.1`, `localhost`), for testing.
- Each redirect is checked against the same list (and at most 3).
- The request is made by a separate client of the launcher: it **never** carries the beta key or cookies. The only
  headers that can be set are `Accept`, `Content-Type` and `Accept-Language`; any other (like `Authorization`) makes
  the call fail.
- Body up to 64 KB, response up to 1 MB, 20 s at most.

### What an add-on can never do

There is no call for: the beta key or the keychain, reading the disc or extracting, looking for or installing
updates, going back to a previous version, starting the game or another program, opening folders, or reading or
writing files outside its store. The bridge answers `addon-denied` to any call that is not in the table, and the
backend checks each one again (that the add-on is active, its permissions and its servers).

## Permissions and the player

- **When installing**, the launcher shows the name, the version, the author, the description, where it appears and
  what it will be able to do. If the player cancels, nothing is installed.
- **When updating**, it shows the same and marks as "New" what it did not ask for before. If they cancel, the previous
  version stays.
- If someone edits by hand the `addon.json` of an installed add-on to ask for more, the launcher does not load it
  ("Permissions to accept") until the player turns it on again and accepts.
- Each add-on can be turned off or removed in Settings → Add-ons. Removing one deletes its folder and its store.

## Safe mode

In safe mode the launcher does not load or serve any add-on. It is turned on:

- by opening the launcher with `--safe-mode` (`dwe-launcher --safe-mode`);
- from Settings → Advanced → Safe mode (for this session);
- with the "Use safe mode" button that appears if the launcher did not close properly last time with active add-ons.

## Installing and testing

1. In the launcher, Settings → Add-ons.
2. Drag the folder, the `.zip` or the `addon.json`, or use "Pick a .zip…" / "Pick a folder…".
3. Review the permissions and press "Install".

When creating one, install the folder directly; to test a change, install it again (it is an update: it keeps its
data). The launcher writes in its log (Settings → Advanced → Logs) why it rejects an add-on.

The launcher rejects, with a message in the player's language:

- an `addon.json` that is missing, is not JSON or has a wrong field (it says which field and why);
- an API version it does not know;
- a `.zip` with paths that leave the folder (`../`), absolute paths or links;
- a package over 20 MB compressed, 40 MB uncompressed or 1000 files;
- a folder with symbolic links.

## Tests

- `scripts/launcher.sh test` runs, among others, the tests for the manifest, the security of `.zip` files, the
  permissions, `net.fetch`'s server list, the store limit and `tests/addons_bridge.rs`, which tests that an add-on
  cannot name any launcher command, that `net.fetch` never carries the key or cookies (not even toward the same
  versions server that does receive it), that it does not follow redirects to other servers and that its context
  contains no paths or secrets.
- `apps/launcher/addons/test-fetch/` is an add-on for tests only: its news card reads the versions from the fake
  server with `net.fetch` and its "Isolation" page tries everything an add-on must **not** be able to do (see the
  launcher's page, use its IPC, call its commands, bypass `net.fetch`, read cookies or `localStorage`, open windows,
  load files from another add-on, navigate the main window) and shows a "PASS" line for each blocked attempt ("PASA" in Spanish). It needs
  the fake server on the port it declares:

  ```bash
  apps/launcher/tools/fake_release_server.py <versions folder> --port 8779
  DWE_LAUNCHER_API=http://127.0.0.1:8779 build/launcher/release/dwe-launcher
  ```
