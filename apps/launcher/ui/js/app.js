// The launcher's page: state from the backend (api.js), the views (first run and home), the
// action bar and the slots (slots.js) the launcher's own pieces register in.
import { api, listen, onDragDrop, pickDisc, pickFolder } from "./api.js";
import { emblem, GAMES } from "./art.js";
import { failureText, formatBytes, getLanguage, setLanguage, t } from "./i18n.js";
import { renderNotes } from "./notes.js";
import * as slots from "./slots.js";

const $ = (id) => document.getElementById(id);

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

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key === "class") node.className = value;
    else if (key === "text") node.textContent = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else if (value === true) node.setAttribute(key, "");
    else if (value !== false && value != null) node.setAttribute(key, value);
  }
  node.append(...children.filter((c) => c != null));
  return node;
}

// append() without the null children of conditional parts.
function add(parent, ...children) {
  parent.append(...children.filter((c) => c != null && c !== false));
  return parent;
}

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
  if (state.discMoved !== null) {
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

  const section = (id, order, title, fill) =>
    slots.register("settings", { id, order, label: () => t(title), render: fill });

  section("data", 0, "s.dataTitle", (box) => {
    const s = state.status;
    add(box, 
      el("p", { text: t("s.dataBody") }),
      el("p", { class: "path", text: `${s.data_folder} ${s.own_data_folder ? t("s.dataOwn") : ""}` }),
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
        s.own_data_folder ? null : el("button", { class: "button", type: "button", text: t("s.dataReset"), onclick: () => settingCall(() => api.setDataFolder(null), true) }),
      ),
    );
  });

  section("language", 10, "s.langTitle", (box) => {
    const select = el(
      "select",
      { id: "languageSelect", onchange: (e) => settingCall(() => api.setLanguage(e.target.value)) },
      el("option", { value: "es", text: "Español" }),
      el("option", { value: "en", text: "English" }),
    );
    select.value = getLanguage();
    add(box, el("div", { class: "row" }, el("label", { for: "languageSelect", class: "sr-only", text: t("s.langTitle") }), select));
  });

  section("updates", 20, "s.updatesTitle", (box) => {
    const s = state.status;
    const auto = el("input", { type: "checkbox", id: "autoUpdates", onchange: (e) => settingCall(() => api.setCheckUpdates(e.target.checked)) });
    auto.checked = s.check_updates;
    let result = "";
    if (state.checking) result = t("s.updatesChecking");
    else if (state.update) result = state.update.game_update ? t("s.updatesAvailable", { version: state.update.game_version }) : t("s.updatesLatest", { version: state.update.game_version });
    else if (state.updateFailure) result = failureText(state.updateFailure);
    add(box, 
      el("div", { class: "row" }, auto, el("label", { for: "autoUpdates", text: t("s.updatesAuto") })),
      el(
        "div",
        { class: "row" },
        el("button", { class: "button", type: "button", text: t("s.updatesNow"), disabled: state.checking, onclick: checkUpdates }),
        s.has_previous ? el("button", { class: "button", type: "button", text: t("s.rollback"), disabled: Boolean(state.task), onclick: () => { $("settingsDialog").close(); runTask(() => api.rollbackGame()); } }) : null,
      ),
      result ? el("p", { text: result }) : null,
      s.signing_ready ? null : el("p", { class: "warning", text: t("s.updatesNoKey") }),
      el("p", {
        text: t("s.versions", {
          launcher: s.launcher_version,
          game: s.game?.version ?? t("s.none"),
          extractor: s.game?.extractor_version ?? t("s.none"),
        }),
      }),
    );
  });

  section("token", 30, "s.tokenTitle", (box) => {
    const s = state.status;
    const input = el("input", { type: "password", id: "tokenInput", autocomplete: "off", spellcheck: "false", disabled: s.token === "unavailable" });
    const statusText = { saved: t("s.tokenSaved"), none: t("s.tokenNone"), unavailable: t("s.tokenUnavailable") }[s.token];
    add(box, 
      el("p", { text: t("s.tokenBody") }),
      el(
        "div",
        { class: "row" },
        el("label", { for: "tokenInput", text: t("s.tokenLabel") }),
        input,
        el("button", {
          class: "button primary", type: "button", text: t("s.tokenSave"), disabled: s.token === "unavailable",
          onclick: () => { const value = input.value; input.value = ""; settingCall(() => api.saveToken(value)); },
        }),
        s.token === "saved" ? el("button", { class: "button", type: "button", text: t("s.tokenClear"), onclick: () => settingCall(() => api.clearToken()) }) : null,
      ),
      el("p", { class: s.token === "unavailable" ? "warning" : "", text: statusText }),
    );
  });

  section("disc", 40, "s.discTitle", (box) => {
    const s = state.status;
    add(box, 
      el("p", { class: s.disc_source ? "path" : "", text: s.disc_source ? t("s.discBody", { path: s.disc_source }) : t("s.discNone") }),
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

  section("logs", 50, "s.logsTitle", (box) => {
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

function selectTab(id) {
  state.activeTab = id;
  slots.render("tabs", context());
  document.getElementById(`tab-${id}`)?.focus();
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
      const button = el("button", { class: "rail-item", type: "button", title: item.label(), "aria-disabled": item.disabled ? "true" : false, "aria-current": item.current ? "page" : false });
      button.insertAdjacentHTML("afterbegin", item.icon());
      button.append(el("span", { class: "sr-only", text: item.label() }));
      if (item.disabled) button.addEventListener("click", (e) => e.preventDefault());
      else if (item.onSelect) button.addEventListener("click", item.onSelect);
      list.append(el("li", {}, button));
    }
  });

  slots.mount("tabs", (items, ctx) => {
    if ($("homeView").hidden) return;
    const tablist = $("tablist");
    const panels = $("tabpanels");
    tablist.replaceChildren();
    panels.replaceChildren();
    if (!items.some((i) => i.id === state.activeTab)) state.activeTab = items[0]?.id;
    items.forEach((item, index) => {
      const selected = item.id === state.activeTab;
      const tab = el("button", {
        class: "tab", type: "button", role: "tab", id: `tab-${item.id}`, "aria-selected": String(selected),
        "aria-controls": `panel-${item.id}`, tabindex: selected ? "0" : "-1", text: item.label(),
        onclick: () => selectTab(item.id),
        onkeydown: (e) => {
          const step = { ArrowRight: 1, ArrowLeft: -1 }[e.key];
          if (step) selectTab(items[(index + step + items.length) % items.length].id);
        },
      });
      tablist.append(tab);
      if (selected) {
        const panel = el("div", { role: "tabpanel", id: `panel-${item.id}`, "aria-labelledby": `tab-${item.id}`, tabindex: "0" });
        item.render(panel, ctx);
        panels.append(panel);
      }
    });
  });

  // News cards render inside the home tab (renderNews); the slot itself needs no mount.

  slots.mount("settings", (items, ctx) => {
    const sections = $("settingsSections");
    if (!state.status) return;
    sections.replaceChildren();
    for (const item of items) {
      const box = el("section", { class: "setting", "aria-labelledby": `setting-${item.id}`, "data-item": item.id });
      add(box, el("h3", { id: `setting-${item.id}`, text: item.label() }));
      item.render(box, ctx);
      sections.append(box);
    }
  });
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
  slots.renderAll(context());
  renderActionBar();
}

// ---- Start -----------------------------------------------------------------------------------

async function start() {
  registerBuiltIns();
  mountSlots();
  $("settingsButton").addEventListener("click", () => {
    slots.render("settings", context());
    $("settingsDialog").showModal();
  });
  $("settingsClose").addEventListener("click", () => $("settingsDialog").close());
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
  if (state.status.check_updates) checkUpdates();
}

start().catch((error) => showMessage(t("errorTitle"), String(error)));
