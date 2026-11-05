// The add-ons' host (docs/guides/launcher-addons.md): loads each active add-on in sandboxed
// iframes (sandbox="allow-scripts" only: another origin, no access to this page, its IPC or its
// storage) and answers the bridge, API v1. Which add-on a message comes from is taken from the
// iframe it came from, never from the message. Interface calls (ui.*, notify) are answered here,
// against the add-on's declared slots and permissions; context, storage and net.fetch go to the
// backend (addon_call), which checks them again. Nothing else is forwarded.
import { api } from "./api.js";
import { el, toast } from "./dom.js";
import * as slots from "./slots.js";

export const API_VERSION = 1;
const PROTOCOL = "dweAddons";
// Calls answered by the backend (src-tauri/src/addons/bridge.rs).
const BACKEND_CALLS = new Set(["context", "storage.get", "storage.set", "storage.remove", "storage.keys", "net.fetch"]);
const MAX_MESSAGES_PER_SECOND = 40;
const NOTIFY_EVERY_MS = 3000;
const MAX_NEWS = 3;
const EVENTS = new Set(["game.status", "language"]);

const addons = new Map(); // id -> loaded add-on
const frames = new Map(); // contentWindow -> { addon, view, iframe }
let hooks = null;
let view = { safe_mode: false, crashed_last_time: false, addons: [] };
let lastStatus = null;

// hooks: { language(), selectTab(id), changed(), failureText(f), t(key, values) }
export function initAddons(given) {
  hooks = given;
  window.addEventListener("message", onMessage);
}

export const addonsView = () => view;

// ---- Loading ---------------------------------------------------------------------------------

function cleanText(value, max) {
  let text = value;
  if (value && typeof value === "object") text = value[hooks.language()] ?? value.en ?? value.es ?? Object.values(value)[0];
  if (typeof text !== "string") return "";
  // eslint-disable-next-line no-control-regex
  return text.replace(/[\u0000-\u001f\u007f]/g, " ").trim().slice(0, max);
}

// A file of the add-on's own folder: plain relative names only.
function safePage(path, extensions) {
  if (typeof path !== "string" || path.length > 200) return null;
  if (!/^[A-Za-z0-9_\-./]+$/.test(path) || path.startsWith("/") || path.split("/").some((p) => p === ".." || p === "." || p === "")) return null;
  return extensions.test(path) ? path : null;
}

function makeFrame(addon, frameView, page) {
  const iframe = el("iframe", {
    class: `addon-frame is-${frameView}`,
    sandbox: "allow-scripts",
    src: `${addon.listed.base_url}${page}?view=${frameView}`,
    title: addon.manifest.name,
    referrerpolicy: "no-referrer",
    loading: "eager",
  });
  track(iframe, addon, frameView);
  return iframe;
}

// An iframe's window exists once it is in the document and stays the same object across its
// navigations: it is known as soon as it is attached, before the add-on's scripts can post.
function track(iframe, addon, frameView) {
  const known = () => {
    if (!iframe.isConnected || !iframe.contentWindow) return false;
    frames.set(iframe.contentWindow, { addon, view: frameView, iframe });
    return true;
  };
  if (known()) return;
  const observer = new MutationObserver(() => {
    if (known()) observer.disconnect();
  });
  observer.observe(document.body, { childList: true, subtree: true });
  addon.observers.push(observer);
}

function load(listed) {
  const addon = { id: listed.id, listed, manifest: listed.manifest, frames: {}, wrappers: {}, observers: [], lastNotify: 0, window: { start: 0, count: 0 } };
  addons.set(addon.id, addon);
  const background = makeFrame(addon, "background", listed.manifest.entry);
  addon.frames.background = background;
  document.getElementById("addonFrames").append(background);
}

function unload(id) {
  const addon = addons.get(id);
  if (!addon) return;
  for (const observer of addon.observers) observer.disconnect();
  for (const wrapper of Object.values(addon.wrappers)) wrapper.remove();
  for (const iframe of Object.values(addon.frames)) {
    for (const [win, frame] of frames) if (frame.iframe === iframe) frames.delete(win);
    iframe.remove();
  }
  for (const slot of slots.SLOT_NAMES) {
    for (const item of slots.itemsOf(slot)) if (item.addon === id) slots.unregister(slot, item.id);
  }
  addons.delete(id);
}

// Reads the list from the backend and loads or unloads add-ons to match it. replaced is the id of
// an add-on that was just installed again: its frames load the new files even when the version
// did not change.
export async function reloadAddons(replaced = null) {
  view = await api.addonsList();
  const active = new Map(view.addons.filter((a) => a.active && a.manifest).map((a) => [a.id, a]));
  for (const [id, addon] of addons) {
    const now = active.get(id);
    if (!now || id === replaced || now.manifest.version !== addon.manifest.version) unload(id);
  }
  for (const [id, listed] of active) if (!addons.has(id)) load(listed);
  hooks.changed();
  return view;
}

export function setView(next) {
  view = next;
}

// ---- The bridge ------------------------------------------------------------------------------

function reply(target, id, result, error) {
  const message = error ? { [PROTOCOL]: API_VERSION, id, error } : { [PROTOCOL]: API_VERSION, id, result: result ?? null };
  // The add-on's origin is opaque ("null"): "*" is the only target that reaches it; nothing
  // secret is ever sent.
  target.postMessage(message, "*");
}

function refuse(code, message) {
  return { code, message };
}

function withinRate(addon) {
  const now = Date.now();
  if (now - addon.window.start > 1000) addon.window = { start: now, count: 0 };
  addon.window.count += 1;
  return addon.window.count <= MAX_MESSAGES_PER_SECOND;
}

function needsSlot(addon, slot) {
  if (!addon.manifest.slots.includes(slot)) throw refuse("addon-denied", `slot ${slot} not declared`);
}

function frameFor(addon, key, frameView, page, parentClass) {
  if (!addon.frames[key]) {
    const wrapper = el("div", { class: parentClass });
    const iframe = makeFrame(addon, frameView, page);
    wrapper.append(iframe);
    addon.frames[key] = iframe;
    addon.wrappers[key] = wrapper;
  }
  return addon.wrappers[key];
}

const handlers = {
  hello: (addon, frame) => ({ api: API_VERSION, view: frame.view, id: addon.id }),

  "ui.sidebar": (addon, frame, params) => {
    needsSlot(addon, "sidebar");
    const label = cleanText(params?.label, 32) || addon.manifest.name;
    const iconPath = safePage(params?.icon, /\.(svg|png|webp)$/i);
    if (!iconPath) throw refuse("addon-denied", "icon must be an .svg, .png or .webp of the add-on's folder");
    slots.register("sidebar", {
      id: `addon:${addon.id}`,
      addon: addon.id,
      order: 200,
      label: () => label,
      iconUrl: `${addon.listed.base_url}${iconPath}`,
      onSelect: () => hooks.selectTab(`addon:${addon.id}`),
    });
    hooks.changed();
  },

  "ui.tab": (addon, frame, params) => {
    needsSlot(addon, "tabs");
    const label = cleanText(params?.label, 32) || addon.manifest.name;
    const page = params?.page == null ? addon.manifest.entry : safePage(params.page, /\.html?$/i);
    if (!page) throw refuse("addon-denied", "page must be an .html of the add-on's folder");
    slots.register("tabs", {
      id: `addon:${addon.id}`,
      addon: addon.id,
      order: 100,
      persistent: true,
      label: () => label,
      render: (panel) => panel.append(frameFor(addon, "tab", "tab", page, "addon-tab")),
    });
    hooks.changed();
  },

  "ui.settings": (addon, frame, params) => {
    needsSlot(addon, "settings");
    const label = cleanText(params?.label, 40) || addon.manifest.name;
    const page = params?.page == null ? addon.manifest.entry : safePage(params.page, /\.html?$/i);
    if (!page) throw refuse("addon-denied", "page must be an .html of the add-on's folder");
    slots.register("settings", {
      id: `addon:${addon.id}`,
      addon: addon.id,
      tab: "addons",
      order: 500,
      persistent: true,
      label: () => label,
      byline: () => addon.manifest.name,
      render: (box) => box.append(frameFor(addon, "settings", "settings", page, "addon-settings")),
    });
    hooks.changed();
  },

  "ui.news": (addon, frame, params) => {
    needsSlot(addon, "news");
    const items = Array.isArray(params?.items) ? params.items.slice(0, MAX_NEWS) : [];
    for (const item of slots.itemsOf("news")) if (item.addon === addon.id) slots.unregister("news", item.id);
    items.forEach((raw, index) => {
      const title = cleanText(raw?.title, 80);
      const body = cleanText(raw?.body, 500);
      if (!title && !body) return;
      const date = typeof raw?.date === "string" && !Number.isNaN(Date.parse(raw.date)) ? raw.date : null;
      slots.register("news", {
        id: `addon:${addon.id}:${index}`,
        addon: addon.id,
        order: 50 + index,
        render: (card) => {
          card.classList.add("is-addon");
          const head = el("div", { class: "card-title" }, el("h2", { text: title }));
          if (date) head.append(el("time", { datetime: date, text: new Date(date).toLocaleDateString(hooks.language()) }));
          card.append(head);
          if (body) card.append(el("p", { class: "addon-news-body", text: body }));
          card.append(el("p", { class: "addon-byline", text: hooks.t("addons.byline", { name: addon.manifest.name }) }));
          return true;
        },
      });
    });
    hooks.changed();
  },

  "ui.resize": (addon, frame, params) => {
    if (frame.view === "background") throw refuse("addon-denied", "only a tab or a settings page can resize");
    const height = Math.round(Number(params?.height));
    if (!Number.isFinite(height)) throw refuse("addon-denied", "height");
    frame.iframe.style.height = `${Math.min(1600, Math.max(80, height))}px`;
  },

  notify: (addon, frame, params) => {
    if (!addon.manifest.permissions.notify) throw refuse("addon-denied", "permission notify");
    const now = Date.now();
    if (now - addon.lastNotify < NOTIFY_EVERY_MS) throw refuse("addon-denied", "too many notices");
    addon.lastNotify = now;
    const text = cleanText(params?.text, 200);
    if (!text) throw refuse("addon-denied", "text");
    toast({ title: addon.manifest.name, text, kind: params?.kind === "success" ? "success" : "info" });
  },

  "events.subscribe": (addon, frame, params) => {
    const wanted = Array.isArray(params?.events) ? params.events.filter((e) => EVENTS.has(e)) : [];
    frame.events = new Set(wanted);
    if (wanted.includes("game.status") && lastStatus) send(frame, "game.status", lastStatus);
    return wanted;
  },
};

// The frame a message came from: one of the add-ons' own iframes, or nothing. An iframe that was
// attached again has a new window: found by looking at every add-on iframe.
function frameOf(source) {
  if (!source) return null;
  const known = frames.get(source);
  if (known) return known;
  for (const addon of addons.values()) {
    for (const [key, iframe] of Object.entries(addon.frames)) {
      if (iframe.contentWindow === source) {
        const frame = { addon, view: key, iframe };
        frames.set(source, frame);
        return frame;
      }
    }
  }
  return null;
}

async function onMessage(event) {
  const frame = frameOf(event.source);
  if (!frame?.addon) return;
  const data = event.data;
  if (!data || typeof data !== "object" || data[PROTOCOL] !== API_VERSION) return;
  const { id, method, params } = data;
  if ((typeof id !== "number" && typeof id !== "string") || typeof method !== "string") return;
  const { addon } = frame;
  if (!addons.has(addon.id)) return;
  if (!withinRate(addon)) {
    reply(event.source, id, null, refuse("addon-denied", "too many calls"));
    return;
  }
  try {
    let result;
    if (Object.hasOwn(handlers, method)) result = handlers[method](addon, frame, params);
    else if (BACKEND_CALLS.has(method)) result = await api.addonCall(addon.id, method, params ?? null);
    else throw refuse("addon-denied", `unknown call ${method.slice(0, 60)}`);
    reply(event.source, id, result);
  } catch (failure) {
    const error = failure?.code ? { code: failure.code, message: failure.message ?? failure.detail ?? "" } : refuse("addon-failed", String(failure));
    reply(event.source, id, null, error);
  }
}

function send(frame, name, data) {
  frame.iframe.contentWindow?.postMessage({ [PROTOCOL]: API_VERSION, event: name, data }, "*");
}

function broadcast(name, data) {
  for (const frame of frames.values()) if (frame.events?.has(name)) send(frame, name, data);
}

// The launcher's state for add-ons: { running, busy, ready, gameVersion }; sent when it changes.
export function publishStatus(status) {
  const text = JSON.stringify(status);
  if (text === JSON.stringify(lastStatus)) return;
  const languageChanged = lastStatus && lastStatus.language !== status.language;
  lastStatus = status;
  broadcast("game.status", status);
  if (languageChanged) broadcast("language", { language: status.language });
}
