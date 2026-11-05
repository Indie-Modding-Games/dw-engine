// The launcher's page: state from the backend (api.js), the views (first run and home), the
// action bar and the slots (slots.js) the launcher's own pieces and the add-ons (addons.js)
// register in. The settings view is settings.js; the add-on manager is addon-manager.js.
import { initAddonManager, installAddonFrom, setAddonDragging, setSafeMode } from "./addon-manager.js";
import { addonsView, initAddons, publishStatus, reloadAddons, setView } from "./addons.js";
import { api, listen, onDragDrop, pickDisc, pickFolder } from "./api.js";
import { emblem, GAMES } from "./art.js";
import { $, add, el, reconcile } from "./dom.js";
import { failureText, formatBytes, getLanguage, setLanguage, t } from "./i18n.js";
import { renderNotes } from "./notes.js";
import { activeSettingsTab, mountSettings, openSettings, settingsOpen } from "./settings.js";
import * as slots from "./slots.js";

const state = {
  status: null,
  update: null,
  updateFailure: null,
  checking: false,
  task: null,
  dragging: false,
  discMoved: null,
  activeTab: "home",
  backgroundLoaded: false,
};

function announce(text) {
  $("live").textContent = text;
}

// ---- Views -------------------------------------------------------------------------------

const ready = () => state.status?.content_installed && state.status?.game;

function renderStaticTexts() {
  for (const node of document.querySelectorAll("[data-text]")) node.textContent = t(node.dataset.text);
  $("rail").setAttribute("aria-label", t("rail"));
  const f = state.status?.formats;
  $("dropTitle").textContent = state.dragging ? t("dropActive") : t("dropTitle");
  if (f) {
    const list = (exts) => exts.map((e) => `.${e}`).join(", ");
    $("dropHint").textContent = t("dropHint", { images: ".cue + .bin, .iso", archives: list(f.archives) });
    $("dropUnsupported").textContent = t("dropUnsupported", { unsupported: list(f.unsupported.slice(0, 6)) });
  }
}

function renderCards() {
  const list = $("gameCards");
  list.replaceChildren();
  for (const game of GAMES) {
    const active = game === "base";
    const art = el("div", { class: "game-card-art" });
    art.insertAdjacentHTML("afterbegin", emblem(game, 72));
    list.append(
      el(
        "li",
        { class: `game-card ${active ? "is-active" : "is-soon"}`, "aria-disabled": active ? false : "true" },
        art,
        el("h2", { text: t(`games.${game}.name`) }),
        el("p", { text: t(`games.${game}.blurb`) }),
        el("span", { class: "badge", text: active ? t("available") : t("soon") }),
      ),
    );
  }
}

function renderViews() {
  const setup = !state.status?.content_installed;
  $("setupView").hidden = !setup;
  $("homeView").hidden = setup;
  $("dropzone").disabled = Boolean(state.task);
  $("dropzone").classList.toggle("is-dragging", state.dragging);
  if (setup) {
    renderCards();
    return;
  }
  $("homeTitle").textContent = t("games.base.name");
  $("homeMeta").textContent = state.status.game
    ? t("installed", { version: state.status.game.version })
    : "";
  renderNotice();
}

function dropzoneButton() {
  const button = el(
    "button",
    { class: `dropzone ${state.dragging ? "is-dragging" : ""}`, type: "button", onclick: chooseDisc },
    el("span", { class: "dropzone-title", text: state.dragging ? t("dropActive") : t("dropTitle") }),
  );
  button.disabled = Boolean(state.task);
  return button;
}

function renderNotice() {
  const notice = $("notice");
  notice.replaceChildren();
  let title = null;
  let body = null;
  let pick = false;
  let actions = null;
  const addons = addonsView();
  if (addons.crashed_last_time && !addons.safe_mode) {
    title = t("crashTitle");
    body = t("crashBody");
    actions = el(
      "div",
      { class: "row" },
      el("button", { class: "button primary", type: "button", text: t("crashSafe"), onclick: () => setSafeMode(true) }),
      el("button", { class: "button", type: "button", text: t("crashKeep"), onclick: async () => { setView(await api.addonsDismissCrash()); renderAll(); } }),
    );
  } else if (state.discMoved !== null) {
    title = t("discMovedTitle");
    body = state.discMoved ? t("discMovedBody", { path: state.discMoved }) : t("discMovedNone");
    pick = true;
  } else if (state.status.content_outdated) {
    title = t("outdatedTitle");
    body = t("outdatedBody");
  }
  notice.hidden = !title;
  if (!title) return;
  notice.append(el("h2", { text: title }), el("p", { text: body }));
  if (pick) notice.append(dropzoneButton());
  if (actions) notice.append(actions);
}

// ---- The action bar ------------------------------------------------------------------------

function taskText(task) {
  if (!task) return "";
  switch (task.task) {
    case "download":
      return t("task.download", { done: formatBytes(task.done ?? 0), total: formatBytes(task.total ?? 0) });
    case "extract": {
      const step = task.step ? t(`steps.${task.step}`) : "";
      const percent = Math.round(task.percent ?? 0);
      return step && !step.startsWith("steps.") ? `${t("task.extract", { percent })} · ${step}` : t("task.extract", { percent });
    }
    default:
      return t(`task.${task.task}`);
  }
}

function mainAction() {
  const s = state.status;
  if (!s) return { label: "…", disabled: true };
  if (state.task) {
    const label = t(`busy.${state.task.task}`);
    return { label: label.startsWith("busy.") ? t("busy.other") : label, disabled: true };
  }
  if (s.game_running) return { label: t("btnPlaying"), disabled: true };
  if (!s.content_installed || state.discMoved !== null) return { label: t("btnInstall"), run: chooseDisc };
  if (s.content_outdated) return { label: t("btnReadAgain"), run: () => runTask(() => api.reextract()) };
  if (state.update?.game_update && s.signing_ready) {
    const size = state.update.download_bytes;
    return {
      label: size ? t("btnUpdate", { size: formatBytes(size) }) : t("btnUpdateNoSize"),
      update: true,
      run: () => runTask(() => api.updateGame()),
      secondary: s.game ? { label: t("playWithoutUpdate"), run: play } : null,
    };
  }
  if (!s.game) return { label: t("btnInstall"), run: chooseDisc };
  return { label: t("btnPlay"), run: play };
}

function renderActionBar() {
  const action = mainAction();
  const button = $("mainAction");
  button.textContent = action.label;
  button.disabled = Boolean(action.disabled);
  button.classList.toggle("is-update", Boolean(action.update));
  button.classList.toggle("is-busy", Boolean(state.task));
  button.onclick = action.run ?? null;

  const secondary = $("secondaryAction");
  secondary.hidden = !action.secondary;
  if (action.secondary) {
    secondary.textContent = action.secondary.label;
    secondary.onclick = action.secondary.run;
  }

  const task = state.task;
  $("stopButton").hidden = !task || !["download", "extract", "identify", "unpack"].includes(task.task);
  $("progress").hidden = !task;
  $("actionText").textContent = taskText(task);
  if (task) {
    const fill = $("progressFill");
    const known = typeof task.percent === "number";
    $("progress").classList.toggle("is-indeterminate", !known);
    fill.style.setProperty("width", known ? `${Math.max(2, task.percent)}%` : "");
    $("progress").setAttribute("role", "progressbar");
    $("progress").setAttribute("aria-label", taskText(task));
    if (known) $("progress").setAttribute("aria-valuenow", String(Math.round(task.percent)));
    else $("progress").removeAttribute("aria-valuenow");
  }
}

// ---- Tasks -----------------------------------------------------------------------------------

function showMessage(title, body, log = null) {
  $("messageTitle").textContent = title;
  $("messageBody").textContent = body;
  $("messageLog").hidden = !log;
  $("messageLog").textContent = log ?? "";
  const dialog = $("messageDialog");
  if (!dialog.open) dialog.showModal();
  $("messageLog").scrollTop = $("messageLog").scrollHeight;
  $("messageDismiss").focus();
}

async function refresh() {
  state.status = await api.status();
  setLanguage(state.status.language);
  if (state.status.content_installed && !state.backgroundLoaded) {
    const still = await api.background();
    if (still) {
      const backdrop = $("backdrop");
      backdrop.style.setProperty("background-image", `url("${still}")`);
      backdrop.classList.add("has-still");
    }
    state.backgroundLoaded = true;
  }
  renderAll();
}

async function runTask(work) {
  if (state.task) return;
  state.task = { task: "identify" };
  state.discMoved = null;
  renderAll();
  try {
    state.status = await work();
    state.update = null;
    announce(t("readyTitle"));
    if (state.status.check_updates) checkUpdates();
  } catch (failure) {
    if (failure?.code === "disc-moved") {
      state.discMoved = failure.detail ?? "";
    } else if (failure?.code !== "cancelled") {
      showMessage(t("errorTitle"), failureText(failure));
    }
  } finally {
    state.task = null;
    await refresh();
  }
}

async function chooseDisc() {
  const f = state.status?.formats;
  const extensions = f ? [...f.images, ...f.archives] : ["cue", "bin", "iso", "zip", "7z"];
  const path = await pickDisc(t("pickTitle"), t("pickFilter"), extensions, t("pickAll"));
  if (path) installFrom(path);
}

function installFrom(path) {
  const rereading = state.status?.content_installed;
  runTask(() => (rereading ? api.reextract(path) : api.installDisc(path)));
}

async function play() {
  try {
    await api.play();
    await refresh();
  } catch (failure) {
    showMessage(t("errorTitle"), failureText(failure));
  }
}

async function checkUpdates() {
  if (state.checking) return;
  state.checking = true;
  slots.render("settings", context());
  try {
    state.update = await api.checkUpdates();
    state.updateFailure = null;
  } catch (failure) {
    state.update = null;
    state.updateFailure = failure;
  } finally {
    state.checking = false;
    renderAll();
  }
}

// ---- Built-in slot items -------------------------------------------------------------------

function registerBuiltIns() {
  GAMES.forEach((game, index) =>
    slots.register("sidebar", {
      id: `game-${game}`,
      order: index,
      label: () => (game === "base" ? t(`games.${game}.name`) : `${t(`games.${game}.name`)} · ${t("soon")}`),
      icon: () => emblem(game, 38),
      current: game === "base",
      disabled: game !== "base",
    }),
  );

  slots.register("tabs", {
    id: "home",
    order: 0,
    label: () => t("tabHome"),
    render: (panel, ctx) => {
      const news = el("div", { class: "news", "data-slot": "news" });
      panel.append(news);
      renderNews(news, ctx);
    },
  });
  slots.register("tabs", {
    id: "notes",
    order: 10,
    label: () => t("tabNotes"),
    render: (panel) => {
      const card = el("article", { class: "card" });
      const notes = el("div", { class: "notes full" });
      if (state.update?.notes) renderNotes(notes, state.update.notes);
      else notes.append(el("p", { text: t("newsEmpty") }));
      card.append(el("div", { class: "card-title" }, el("h2", { text: state.update?.release_name ?? t("tabNotes") })), notes);
      panel.append(card);
    },
  });

  slots.register("news", {
    id: "launcher-update",
    order: 0,
    render: (card) => {
      if (!state.update?.launcher_update) return false;
      card.append(
        el("div", { class: "card-title" }, el("h2", { text: t("newsLauncher", { version: state.update.launcher_update }) })),
        el("button", { class: "button primary", type: "button", text: t("newsLauncherButton"), onclick: () => runTask(() => api.installLauncherUpdate()) }),
      );
      return true;
    },
  });
  slots.register("news", {
    id: "release-notes",
    order: 10,
    render: (card) => {
      const title = el("div", { class: "card-title" }, el("h2", { text: state.update?.release_name ?? t("newsTitle") }));
      if (state.update?.published_at) {
        const date = new Date(state.update.published_at);
        title.append(el("time", { datetime: state.update.published_at, text: date.toLocaleDateString(getLanguage()) }));
      }
      const notes = el("div", { class: "notes" });
      if (state.update?.notes) {
        renderNotes(notes, state.update.notes, 6);
      } else if (state.updateFailure) {
        notes.append(el("p", { text: `${t("newsOffline")} ${failureText(state.updateFailure)}` }));
      } else {
        notes.append(el("p", { text: t("newsEmpty") }));
      }
      card.append(title, notes);
      if (state.update?.notes) {
        card.append(el("button", { class: "link-button", type: "button", text: t("newsMore"), onclick: () => selectTab("notes") }));
      }
      return true;
    },
  });

  const section = (id, tab, order, title, fill) =>
    slots.register("settings", { id, tab, order, label: () => t(title), render: fill });

  section("language", "general", 0, "s.langTitle", (box) => {
    const select = el(
      "select",
      { id: "languageSelect", onchange: (e) => settingCall(() => api.setLanguage(e.target.value)) },
      el("option", { value: "es", text: "Español" }),
      el("option", { value: "en", text: "English" }),
    );
    select.value = getLanguage();
    add(box, el("p", { text: t("s.langBody") }), el("div", { class: "row" }, el("label", { for: "languageSelect", class: "sr-only", text: t("s.langTitle") }), select));
  });

  section("data", "general", 10, "s.dataTitle", (box) => {
    const s = state.status;
    add(box,
      el("p", { text: t("s.dataBody") }),
      el("p", { class: "path" }, el("span", { text: s.data_folder }), s.own_data_folder ? el("span", { class: "chip is-quiet", text: t("s.dataOwn") }) : null),
      el(
        "div",
        { class: "row" },
        el("button", {
          class: "button", type: "button", text: t("s.dataChange"), disabled: Boolean(state.task),
          onclick: async () => {
            const folder = await pickFolder(t("s.dataTitle"));
            if (folder) settingCall(() => api.setDataFolder(folder), true);
          },
        }),
        s.own_data_folder ? null : el("button", { class: "button subtle", type: "button", text: t("s.dataReset"), onclick: () => settingCall(() => api.setDataFolder(null), true) }),
      ),
    );
  });

  section("updates", "updates", 0, "s.updatesTitle", (box) => {
    const s = state.status;
    const auto = el("input", { type: "checkbox", role: "switch", class: "switch", id: "autoUpdates", onchange: (e) => settingCall(() => api.setCheckUpdates(e.target.checked)) });
    auto.checked = s.check_updates;
    let result = "";
    let tone = "muted";
    if (state.checking) result = t("s.updatesChecking");
    else if (state.update) {
      result = state.update.game_update ? t("s.updatesAvailable", { version: state.update.game_version }) : t("s.updatesLatest", { version: state.update.game_version });
      tone = state.update.game_update ? "accent" : "muted";
    } else if (state.updateFailure) {
      result = failureText(state.updateFailure);
      tone = "warning";
    }
    add(box,
      el(
        "div",
        { class: "row between" },
        el("label", { for: "autoUpdates", class: "row-label" }, el("span", { text: t("s.updatesAuto") }), el("span", { class: "muted small", text: t("s.updatesAutoHint") })),
        auto,
      ),
      el(
        "div",
        { class: "row" },
        el("button", { class: "button", type: "button", text: t("s.updatesNow"), disabled: state.checking, onclick: checkUpdates }),
        el("p", { class: `status-line ${tone}`, role: "status", text: result }),
      ),
      s.signing_ready ? null : el("p", { class: "warning", text: t("s.updatesNoKey") }),
    );
  });

  section("versions", "updates", 10, "s.versionsTitle", (box) => {
    const s = state.status;
    const fact = (label, value) => [el("dt", { text: label }), el("dd", { text: value })];
    add(box,
      el(
        "dl",
        { class: "facts" },
        ...fact(t("s.factLauncher"), s.launcher_version),
        ...fact(t("s.factGame"), s.game?.version ?? t("s.none")),
        ...fact(t("s.factExtractor"), String(s.game?.extractor_version ?? t("s.none"))),
        ...fact(t("s.factPlatform"), s.platform),
      ),
      el("p", { text: s.has_previous ? t("s.rollbackBody") : t("s.rollbackNone") }),
      s.has_previous
        ? el("div", { class: "row" }, el("button", { class: "button", type: "button", text: t("s.rollback"), disabled: Boolean(state.task), onclick: () => { $("settingsDialog").close(); runTask(() => api.rollbackGame()); } }))
        : null,
    );
  });

  section("token", "account", 0, "s.tokenTitle", (box) => {
    const s = state.status;
    const input = el("input", { type: "password", id: "tokenInput", autocomplete: "off", spellcheck: "false", disabled: s.token === "unavailable" });
    const statusText = { saved: t("s.tokenSaved"), none: t("s.tokenNone"), unavailable: t("s.tokenUnavailable") }[s.token];
    add(box,
      el("p", { text: t("s.tokenBody") }),
      el("p", { class: `status-line ${s.token === "saved" ? "accent" : s.token === "unavailable" ? "warning" : "muted"}`, role: "status", text: statusText }),
      el("label", { for: "tokenInput", class: "field-label", text: t("s.tokenLabel") }),
      el(
        "div",
        { class: "row" },
        input,
        el("button", {
          class: "button primary", type: "button", text: t("s.tokenSave"), disabled: s.token === "unavailable",
          onclick: () => { const value = input.value; input.value = ""; settingCall(() => api.saveToken(value)); },
        }),
        s.token === "saved" ? el("button", { class: "button subtle", type: "button", text: t("s.tokenClear"), onclick: () => settingCall(() => api.clearToken()) }) : null,
      ),
      el("p", { class: "muted small", text: t("s.tokenHint") }),
    );
  });

  section("disc", "disc", 0, "s.discTitle", (box) => {
    const s = state.status;
    add(box,
      el("p", { text: t("s.discLead") }),
      s.disc_source ? el("p", { class: "path" }, el("span", { text: s.disc_source }), s.disc_source_exists ? null : el("span", { class: "chip is-warning", text: t("s.discMissing") })) : el("p", { class: "muted", text: t("s.discNone") }),
      el(
        "div",
        { class: "row" },
        el("button", {
          class: "button", type: "button", text: t("s.discAgain"), disabled: Boolean(state.task) || !s.game,
          onclick: () => { $("settingsDialog").close(); s.disc_source_exists ? runTask(() => api.reextract()) : chooseDisc(); },
        }),
      ),
    );
  });

  section("logs", "advanced", 0, "s.logsTitle", (box) => {
    add(box,
      el("p", { text: t("s.logsBody") }),
      el("div", { class: "row" }, el("button", { class: "button", type: "button", text: t("s.logsOpen"), onclick: () => settingCall(() => api.openLogs().then(api.status)) })),
    );
  });
}

async function settingCall(work, reloadBackground = false) {
  try {
    state.status = await work();
    if (reloadBackground) {
      state.backgroundLoaded = false;
      $("backdrop").classList.remove("has-still");
      $("backdrop").style.removeProperty("background-image");
      state.update = null;
    }
    await refresh();
  } catch (failure) {
    showMessage(t("errorTitle"), failureText(failure));
  }
}

// ---- Slot renderers ------------------------------------------------------------------------

const cssId = (id) => id.replace(/[^A-Za-z0-9_-]/g, "-");

function selectTab(id) {
  if (settingsOpen()) $("settingsDialog").close();
  state.activeTab = id;
  slots.render("tabs", context());
  slots.render("sidebar", context());
  document.getElementById(`tab-${cssId(id)}`)?.focus();
}

function renderNews(container, ctx) {
  for (const item of slots.itemsOf("news")) {
    const card = el("article", { class: "card", "data-item": item.id });
    if (item.render(card, ctx) !== false) container.append(card);
  }
}

function mountSlots() {
  slots.mount("sidebar", (items) => {
    const list = $("sidebar");
    list.replaceChildren();
    for (const item of items) {
      const current = item.current || (item.addon && state.activeTab === item.id);
      const button = el("button", { class: `rail-item${item.addon ? " is-addon" : ""}`, type: "button", title: item.label(), "aria-disabled": item.disabled ? "true" : false, "aria-current": current ? "page" : false });
      // An add-on's icon is an image of its own folder (never markup in this page).
      if (item.iconUrl) button.append(el("img", { src: item.iconUrl, alt: "", width: "30", height: "30", draggable: "false" }));
      else button.insertAdjacentHTML("afterbegin", item.icon());
      button.append(el("span", { class: "sr-only", text: item.label() }));
      if (item.disabled) button.addEventListener("click", (e) => e.preventDefault());
      else if (item.onSelect) button.addEventListener("click", item.onSelect);
      list.append(el("li", {}, button));
    }
  });

  // Tabs and panels are kept by id: switching only hides panels, so an add-on's tab (an iframe)
  // keeps its state; the launcher's own panels are drawn again when shown.
  slots.mount("tabs", (items, ctx) => {
    if ($("homeView").hidden) return;
    if (!items.some((i) => i.id === state.activeTab)) state.activeTab = items[0]?.id;
    const ids = items.map((i) => i.id);
    const tabs = reconcile($("tablist"), ids, (id) =>
      el("button", {
        class: "tab", type: "button", role: "tab", id: `tab-${cssId(id)}`, "aria-controls": `panel-${cssId(id)}`,
        onclick: () => selectTab(id),
        onkeydown: (e) => {
          const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
          if (!step) return;
          const now = slots.itemsOf("tabs").map((i) => i.id);
          selectTab(now[(now.indexOf(id) + step + now.length) % now.length]);
        },
      }),
    );
    const panels = reconcile($("tabpanels"), ids, (id) => {
      const item = items.find((i) => i.id === id);
      const panel = el("div", { class: `tabpanel${item.persistent ? " is-addon" : ""}`, role: "tabpanel", id: `panel-${cssId(id)}`, "aria-labelledby": `tab-${cssId(id)}`, tabindex: "0" });
      if (item.persistent) item.render(panel, ctx);
      return panel;
    });
    for (const item of items) {
      const selected = item.id === state.activeTab;
      const tab = tabs.get(item.id);
      tab.textContent = item.label();
      tab.setAttribute("aria-selected", String(selected));
      tab.tabIndex = selected ? 0 : -1;
      const panel = panels.get(item.id);
      panel.hidden = !selected;
      if (selected && !item.persistent) {
        panel.replaceChildren();
        item.render(panel, ctx);
      }
    }
  });

  // News cards render inside the home tab (renderNews); the slot itself needs no mount.
  mountSettings();
}

// What a slot item may read: a copy of the public part of the state, never the disc path or
// anything secret.
function context() {
  const s = state.status;
  return {
    language: getLanguage(),
    ready: Boolean(ready()),
    gameVersion: s?.game?.version ?? null,
    gameRunning: Boolean(s?.game_running),
    busy: Boolean(state.task),
  };
}

function renderAll() {
  renderStaticTexts();
  renderViews();
  const ctx = context();
  slots.renderAll(ctx);
  renderActionBar();
  publishStatus({ running: ctx.gameRunning, busy: ctx.busy, ready: ctx.ready, gameVersion: ctx.gameVersion, language: ctx.language });
}

// ---- Start -----------------------------------------------------------------------------------

async function start() {
  registerBuiltIns();
  mountSlots();
  initAddons({ language: getLanguage, selectTab, changed: () => renderAll(), t });
  initAddonManager({ message: showMessage, redraw: () => renderAll() });
  $("settingsButton").addEventListener("click", () => openSettings());
  $("messageDismiss").addEventListener("click", () => $("messageDialog").close());
  $("dropzone").addEventListener("click", chooseDisc);
  $("stopButton").addEventListener("click", () => api.cancel());

  await listen("task", async (payload) => {
    const changed = state.task?.task !== payload.task;
    state.task = payload;
    renderActionBar();
    // A new step can change what is installed (the game before the extraction): draw it.
    if (changed) {
      state.status = await api.status();
      renderViews();
    }
  });
  await listen("game-exited", async (exit) => {
    await refresh();
    if (exit.failed) {
      const body = exit.code == null ? t("gameFailedSignal") : t("gameFailedBody", { code: exit.code });
      showMessage(t("gameFailedTitle"), body, exit.log.join("\n"));
    }
  });
  await onDragDrop((event) => {
    // Over Settings → Add-ons, a drop is an add-on (.zip, folder or addon.json).
    if (settingsOpen() && activeSettingsTab() === "addons") {
      if (event.type === "enter" || event.type === "over") setAddonDragging(true);
      else if (event.type === "leave") setAddonDragging(false);
      else if (event.type === "drop") {
        setAddonDragging(false);
        installAddonFrom(event.paths?.[0]);
      }
      return;
    }
    const accepting = !state.task && (!state.status?.content_installed || state.discMoved !== null || state.status?.content_outdated);
    if (!accepting) return;
    if (event.type === "enter" || event.type === "over") {
      if (!state.dragging) {
        state.dragging = true;
        renderAll();
      }
    } else if (event.type === "leave") {
      state.dragging = false;
      renderAll();
    } else if (event.type === "drop") {
      state.dragging = false;
      const path = event.paths?.[0];
      renderAll();
      if (path) installFrom(path);
    }
  });

  await refresh();
  try {
    await reloadAddons();
  } catch (failure) {
    showMessage(t("errorTitle"), failureText(failure));
  }
  if (state.status.check_updates) checkUpdates();
}

start().catch((error) => showMessage(t("errorTitle"), String(error)));
