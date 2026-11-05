// dwe-addons.js: the client of the launcher's add-on bridge, API v1 (docs/guides/launcher-addons.md).
// Copy this file into your add-on's folder and load it with a classic <script> tag (not a module:
// the add-on runs on an opaque origin). It defines window.DweAddons.
//
//   const dwe = window.DweAddons;
//   await dwe.connect();                       // { api, view, id }
//   dwe.view                                   // "background", "tab" or "settings"
//   await dwe.ui.tab({ label: { es: "Notas", en: "Notes" } });
//   await dwe.storage.set("notes", [...]);
//   dwe.on("game.status", (status) => ...);
//
// Every call returns a promise; a refused call rejects with an Error whose .code says why
// ("addon-denied", "addon-host", "addon-storage-full", "network", "timeout"...).
(function () {
  "use strict";
  const PROTOCOL = "dweAddons";
  const API = 1;
  let next = 1;
  const pending = new Map();
  const listeners = new Map();

  function failure(code, message) {
    const error = new Error(message || code);
    error.code = code;
    return error;
  }

  window.addEventListener("message", (event) => {
    if (event.source !== window.parent) return;
    const message = event.data;
    if (!message || typeof message !== "object" || message[PROTOCOL] !== API) return;
    if (typeof message.event === "string") {
      for (const listener of listeners.get(message.event) || []) {
        try {
          listener(message.data);
        } catch (error) {
          console.error(error);
        }
      }
      return;
    }
    const waiting = pending.get(message.id);
    if (!waiting) return;
    pending.delete(message.id);
    if (message.error) waiting.reject(failure(message.error.code, message.error.message));
    else waiting.resolve(message.result);
  });

  function call(method, params, timeoutMs) {
    return new Promise((resolve, reject) => {
      const id = next++;
      pending.set(id, { resolve, reject });
      window.parent.postMessage({ [PROTOCOL]: API, id, method, params: params === undefined ? null : params }, "*");
      setTimeout(() => {
        if (pending.delete(id)) reject(failure("timeout", `${method} got no answer`));
      }, timeoutMs || 30000);
    });
  }

  // The launcher may not be listening yet when the page starts: hello is asked again a few times.
  async function connect() {
    for (let attempt = 0; attempt < 25; attempt++) {
      try {
        return await call("hello", null, 250);
      } catch (error) {
        if (error.code !== "timeout") throw error;
      }
    }
    throw failure("timeout", "the launcher did not answer");
  }

  let observer = null;
  // Keeps a tab or settings page as tall as its content.
  function autoResize() {
    if (observer || typeof ResizeObserver === "undefined") return;
    let last = 0;
    observer = new ResizeObserver(() => {
      // The body's own box, not the document's scroll height: that one never drops below the
      // frame's current height, so the frame could grow but never shrink.
      const style = getComputedStyle(document.body);
      const height = Math.ceil(document.body.getBoundingClientRect().bottom + window.scrollY + parseFloat(style.marginBottom || "0"));
      if (height !== last) {
        last = height;
        call("ui.resize", { height }).catch(() => {});
      }
    });
    observer.observe(document.body);
  }

  window.DweAddons = Object.freeze({
    api: API,
    view: new URLSearchParams(location.search).get("view") || "background",
    connect,
    context: () => call("context"),
    ui: Object.freeze({
      sidebar: (options) => call("ui.sidebar", options),
      tab: (options) => call("ui.tab", options || {}),
      settings: (options) => call("ui.settings", options || {}),
      news: (items) => call("ui.news", { items }),
      resize: (height) => call("ui.resize", { height }),
      autoResize,
    }),
    storage: Object.freeze({
      get: (key) => call("storage.get", { key }),
      set: (key, value) => call("storage.set", { key, value }),
      remove: (key) => call("storage.remove", { key }),
      keys: () => call("storage.keys"),
    }),
    notify: (text, kind) => call("notify", { text, kind }),
    net: Object.freeze({
      fetch: (url, options) => {
        const o = options || {};
        return call("net.fetch", { url, method: o.method, headers: Object.entries(o.headers || {}), body: o.body });
      },
    }),
    on(event, listener) {
      if (!listeners.has(event)) listeners.set(event, []);
      listeners.get(event).push(listener);
      return call("events.subscribe", { events: [...listeners.keys()] });
    },
    // A raw call, for testing what the launcher refuses.
    call,
  });
})();
