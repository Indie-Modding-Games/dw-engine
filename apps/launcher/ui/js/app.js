// The launcher's page: state from the backend (api.js), the views (first run and home), the
// action bar and the slots (slots.js) the launcher's own pieces and the add-ons (addons.js)
// register in. The settings view is settings.js; the add-on manager is addon-manager.js.
import { initAddonManager, installAddonFrom, setAddonDragging, setSafeMode } from "./addon-manager.js";
import { addonsView, initAddons, publishStatus, reloadAddons, setView } from "./addons.js";
import { api, listen, onDragDrop, openUrl, pickDisc, pickFolder } from "./api.js";
import { emblem, GAMES } from "./art.js";
import { $, add, ask, el, icon, reconcile, toast } from "./dom.js";
import { dropOnImages, showImages } from "./custom-images.js";
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
  // The main area: "home", "games" or an add-on's page ("addon:<id>").
  view: "home",
  // The news panel shows every block of the notes.
  newsFull: false,
  backgroundLoaded: false,
  // The launcher's own update: downloading now, and whether its banner was put off.
  launcherInstalling: false,
  launcherLater: false,
  // What this computer has (check_system): free space and the GPU check; checking now; and
  // whether the compatibility mode's notice was put away.
  requirements: null,
  checkingSystem: false,
  compatSeen: false,
};

function announce(text) {
  $("live").textContent = text;
}

// ---- Views -------------------------------------------------------------------------------

const ready = () => state.status?.content_installed && state.status?.game;
const firstRun = () => !state.status?.content_installed;
// The beta is closed while versions are 0.x or prereleases.
const isBeta = (version) => version.startsWith("0.") || version.includes("-");

function renderStaticTexts() {
  for (const node of document.querySelectorAll("[data-text]")) node.textContent = t(node.dataset.text);
  $("rail").setAttribute("aria-label", t("nav.label"));
  $("aboutButton").dataset.tip = t("nav.about");
  $("statusPills").setAttribute("aria-label", t("pill.label"));
  $("links").setAttribute("aria-label", t("links.label"));
  $("newsPanel").setAttribute("aria-label", t("newsTitle"));
  $("dropOverlayText").textContent = t("dropActive");
  $("dropTitle").textContent = t("firstRunHint");
  const f = state.status?.formats;
  if (f) {
    const list = (exts) => exts.map((e) => `.${e}`).join(", ");
    $("dropHint").textContent = t("dropHint", { images: ".cue + .bin, .iso", archives: list(f.archives) });
    $("dropUnsupported").textContent = t("dropUnsupported", { unsupported: list(f.unsupported.slice(0, 6)) });
  }
}

// The four games; only the base game opens (the others are announced, greyed out).
function renderCards() {
  const list = $("gameCards");
  list.replaceChildren();
  for (const game of GAMES) {
    const active = game === "base";
    const art = el("div", { class: "game-card-art" });
    art.insertAdjacentHTML("afterbegin", emblem(game, 64));
    const parts = [
      art,
      el("h2", { text: t(`games.${game}.name`) }),
      el("p", { text: t(`games.${game}.blurb`) }),
      el("span", { class: "badge", text: active ? t("available") : t("soon") }),
    ];
    const card = active
      ? el("button", { class: "game-card is-active", type: "button", onclick: () => selectView("home") }, ...parts)
      : el("div", { class: "game-card is-soon", "aria-disabled": "true" }, ...parts);
    list.append(el("li", {}, card));
  }
}

function renderViews() {
  if (!state.status) return;
  const view = state.view;
  $("homeView").hidden = view !== "home";
  $("gamesView").hidden = view !== "games";
  $("imagesView").hidden = view !== "images";
  $("addonView").hidden = !view.startsWith("addon:");
  $("dropOverlay").hidden = !state.dragging;
  if (view === "games") renderCards();
  if (view === "images") showImages();
  const setup = firstRun();
  document.body.classList.toggle("is-first-run", setup);
  $("homeTitle").textContent = setup ? t("setupTitle") : t("games.base.name");
  $("homeLead").textContent = setup ? t("setupLead") : t("games.base.blurb");
  $("firstRun").hidden = !setup;
  $("configureWrap").hidden = setup;
  renderNotice();
  renderPills();
  renderAddonsPill();
  renderNewsPill();
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
  } else if (requirementNotice()) {
    ({ title, body, actions } = requirementNotice());
  } else if (state.status.content_outdated) {
    title = t("outdatedTitle");
    body = t("outdatedBody");
  } else if (state.status.launcher_ready && !state.launcherLater) {
    const playing = state.status.game_running;
    title = t("launcherReadyTitle", { version: state.status.launcher_ready });
    body = playing ? t("launcherReadyPlaying") : t("launcherReadyBody");
    actions = el(
      "div",
      { class: "row" },
      el("button", { class: "button primary", type: "button", text: t("launcherRestart"), disabled: playing || Boolean(state.task), onclick: restartLauncher }),
      el("button", { class: "button", type: "button", text: t("launcherLater"), onclick: () => { state.launcherLater = true; renderAll(); } }),
    );
  }
  notice.hidden = !title;
  if (!title) return;
  notice.append(el("h2", { text: title }), el("p", { text: body }));
  if (pick) notice.append(dropzoneButton());
  if (actions) notice.append(actions);
}

// ---- What this computer needs (check_system) -------------------------------------------------

const MAKERS = { nvidia: "NVIDIA", amd: "AMD", intel: "Intel" };

async function checkRequirements(again = false) {
  if (state.checkingSystem) return;
  state.checkingSystem = true;
  renderAll();
  try {
    state.requirements = await api.checkSystem(again);
    if (again) state.compatSeen = false;
  } catch (failure) {
    showFailure(failure);
  } finally {
    state.checkingSystem = false;
    renderAll();
  }
}

// The buttons that lead to a newer GPU driver: the maker's page (all three when the maker is not
// known) and, on Windows, Windows Update. On Linux the AMD and Intel drivers come with the
// system, so only NVIDIA's page is offered.
function driverButtons(req) {
  const vendor = req.graphics?.vendor;
  let makers = MAKERS[vendor] ? [vendor] : Object.keys(MAKERS);
  if (req.os === "linux") makers = makers.filter((maker) => maker === "nvidia");
  const buttons = makers.map((maker) =>
    el("button", { class: "button", type: "button", text: t("req.driverPage", { maker: MAKERS[maker] }), onclick: () => api.openDriverPage(maker).catch(showFailure) }),
  );
  if (req.os === "windows") {
    buttons.push(el("button", { class: "button", type: "button", text: t("req.windowsUpdate"), onclick: () => api.openDriverPage("windows-update").catch(showFailure) }));
  }
  return buttons;
}

function againButton(primary = false) {
  return el("button", {
    class: `button${primary ? " primary" : ""}`, type: "button",
    text: state.checkingSystem ? t("req.checking") : t("req.again"),
    disabled: state.checkingSystem, onclick: () => checkRequirements(true),
  });
}

const gpuName = (graphics) => (graphics?.gpu?.name ? t("req.gpuName", { name: graphics.gpu.name }) : "");

// The home notice for a missing requirement, most blocking first: no room to read the disc or to
// play, no GPU at all, then the compatibility mode (a heads-up the player can put away).
function requirementNotice() {
  const req = state.requirements;
  const s = state.status;
  if (!req || !s || state.task) return null;
  const space = req.space;
  const installing = !s.content_installed || s.content_outdated;
  if ((installing && !space.install_ok) || !space.play_ok) {
    const needed = installing ? space.install_needed : space.play_needed;
    return {
      title: t("req.spaceTitle"),
      body: t(installing ? "req.spaceInstallBody" : "req.spacePlayBody", { needed: formatBytes(needed), folder: space.folder, free: formatBytes(space.free ?? 0) }),
      actions: el("div", { class: "row" }, againButton(true), el("button", { class: "button", type: "button", text: t("req.changeFolder"), onclick: () => openSettings("general") })),
    };
  }
  const graphics = req.graphics;
  if (graphics?.graphics === "blocked") {
    return {
      title: t("req.blockedTitle"),
      body: t(req.os === "linux" ? "req.blockedBodyLinux" : "req.blockedBody", { gpu: gpuName(graphics) }),
      actions: el("div", { class: "row" }, againButton(true), ...driverButtons(req)),
    };
  }
  if ((graphics?.graphics === "fallback" || graphics?.graphics === "software") && !state.compatSeen) {
    return {
      title: t("req.compatTitle"),
      body: t(graphics.graphics === "fallback" ? "req.compatBody" : "req.compatSoftwareBody", { gpu: gpuName(graphics) }),
      actions: el(
        "div",
        { class: "row" },
        el("button", { class: "button primary", type: "button", text: t("req.compatOk"), onclick: () => { state.compatSeen = true; renderAll(); } }),
        ...driverButtons(req),
        againButton(),
      ),
    };
  }
  return null;
}

// ---- The status pills (top) ----------------------------------------------------------------

function statusPill(kind, ...children) {
  return el("li", { class: `status-pill ${kind}` }, ...children);
}

function renderPills() {
  const s = state.status;
  if (!s) return;
  const list = $("statusPills");
  const pills = [];
  pills.push(
    s.game
      ? statusPill(
          "is-version",
          el("span", { class: "dot" }),
          el("span", { text: t("pill.version", { version: s.game.version }) }),
          isBeta(s.game.version) ? el("span", { class: "pill-tag", text: t("pill.beta") }) : null,
        )
      : statusPill("is-version", el("span", { class: "dot is-off" }), el("span", { text: t("pill.notInstalled") }), el("span", { class: "pill-tag", text: t("pill.beta") })),
  );
  if (s.game_running) pills.push(statusPill("is-running", el("span", { class: "dot is-live" }), el("span", { text: t("pill.running") })));
  const graphics = state.requirements?.graphics?.graphics;
  if (graphics === "fallback" || graphics === "software") {
    pills.push(
      el("li", { class: "status-pill is-compat" },
        el("button", { type: "button", class: "pill-button", onclick: () => { state.compatSeen = false; renderAll(); } },
          el("span", { class: "dot is-warn" }),
          el("span", { text: t("pill.compat") }))),
    );
  }
  const task = state.task;
  if (task) {
    const known = typeof task.percent === "number" || (task.task === "download" && task.total > 0);
    const percent = typeof task.percent === "number" ? task.percent : known ? (100 * (task.done ?? 0)) / task.total : null;
    const bar = el("span", { class: `pill-progress${known ? "" : " is-indeterminate"}`, role: "progressbar", "aria-label": taskText(task), "aria-valuemin": "0", "aria-valuemax": "100" }, el("span", { class: "pill-progress-fill" }));
    if (known) {
      bar.setAttribute("aria-valuenow", String(Math.round(percent)));
      bar.firstChild.style.setProperty("width", `${Math.max(2, Math.min(100, percent))}%`);
    }
    pills.push(statusPill("is-task", el("span", { class: "pill-text", text: taskText(task) }), bar));
  } else if (state.update?.game_update && s.game) {
    pills.push(
      el("li", { class: "status-pill is-update" },
        el("button", { type: "button", class: "pill-button", onclick: () => setPopup("news") },
          el("span", { class: "dot is-new" }),
          el("span", { text: t("pill.update", { version: state.update.game_version }) }))),
    );
  }
  if (s.launcher_ready && state.launcherLater) {
    pills.push(statusPill("is-launcher", el("span", { class: "dot is-new" }), el("span", { text: t("pill.launcherReady", { version: s.launcher_ready }) })));
  }
  list.replaceChildren(...pills);
}

// ---- The hero's buttons ----------------------------------------------------------------------

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
  renderPills();
}

// ---- Menus and panels: Configurar, the add-ons pill and the news ------------------------------

const POPUPS = {
  configure: { button: "configureButton", panel: "configureMenu" },
  addons: { button: "addonsPill", panel: "addonsPopover" },
  news: { button: "newsPill", panel: "newsPanel" },
};
let openPopup = null;

// Opens one popup (or none) and closes the others.
function setPopup(name, focus = true) {
  for (const [key, ids] of Object.entries(POPUPS)) {
    const open = key === name;
    $(ids.panel).hidden = !open;
    $(ids.button).setAttribute("aria-expanded", String(open));
  }
  openPopup = name;
  if (name === "configure") renderConfigureMenu(focus);
  else if (name === "addons") renderAddonsPopover(focus);
  else if (name === "news") {
    slots.render("news", context());
    if (focus) $("newsPanel").querySelector("button")?.focus();
  }
}

function togglePopup(name) {
  setPopup(openPopup === name ? null : name);
}

function closePopup() {
  if (!openPopup) return false;
  const button = $(POPUPS[openPopup].button);
  setPopup(null);
  button.focus();
  return true;
}

const MENU_ICONS = {
  settings: '<circle cx="12" cy="12" r="3"/><path d="M4 7h4M12 7h8M4 17h10M18 17h2"/><circle cx="10" cy="7" r="2"/><circle cx="16" cy="17" r="2"/>',
  reread: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="2.2"/>',
  rollback: '<path d="M9 7 4.5 11.5 9 16"/><path d="M5 11.5h9a5 5 0 0 1 0 10h-2"/>',
  logs: '<path d="M6 3.5h9l3 3V20.5H6Z"/><path d="M9 10h6M9 13.5h6M9 17h3"/>',
};

function configureItems() {
  const s = state.status;
  const busy = Boolean(state.task);
  return [
    { id: "settings", label: t("cfg.settings"), run: () => openSettings("general") },
    {
      id: "reread", label: t("cfg.reread"), disabled: busy || !s.game || s.game_running,
      run: () => (s.disc_source_exists ? runTask(() => api.reextract()) : chooseDisc()),
    },
    { id: "rollback", label: t("cfg.rollback"), disabled: busy || !s.has_previous || s.game_running, run: rollback },
    { id: "logs", label: t("cfg.logs"), run: () => settingCall(() => api.openLogs().then(api.status)) },
  ];
}

function renderConfigureMenu(focus) {
  const menu = $("configureMenu");
  const buttons = configureItems().map((item) => {
    const button = el("button", { class: "menu-item", type: "button", role: "menuitem", tabindex: "-1", disabled: item.disabled ? true : false }, icon(MENU_ICONS[item.id], 18), el("span", { text: item.label }));
    button.addEventListener("click", () => {
      setPopup(null);
      $("configureButton").focus();
      item.run();
    });
    return button;
  });
  menu.replaceChildren(...buttons);
  // Opens upward when the main area has no room below the button (short windows).
  menu.classList.remove("is-up");
  if (menu.getBoundingClientRect().bottom > $("main").getBoundingClientRect().bottom) menu.classList.add("is-up");
  if (focus) buttons.find((b) => !b.disabled)?.focus();
}

// Arrow keys, Home and End inside a menu (only its enabled items); Tab closes it.
function menuKeys(event) {
  const items = [...event.currentTarget.querySelectorAll('[role="menuitem"]:not(:disabled)')];
  if (event.key === "Tab") {
    // Back on the button first, so Tab goes on from there.
    setPopup(null, false);
    $("configureButton").focus();
    return;
  }
  const at = items.indexOf(document.activeElement);
  const to = { ArrowDown: at + 1, ArrowUp: at - 1, Home: 0, End: items.length - 1 }[event.key];
  if (to === undefined || !items.length) return;
  event.preventDefault();
  items[(to + items.length) % items.length].focus();
}

async function rollback() {
  const ok = await ask({ title: t("cfg.rollbackTitle"), body: el("p", { text: t("cfg.rollbackBody") }), ok: t("cfg.rollbackOk"), cancel: t("cfg.cancel") });
  if (ok) runTask(() => api.rollbackGame());
}

function addonState(listed, view) {
  if (view.safe_mode) return t("addons.stateSafe");
  if (listed.active) return t("addons.stateOn");
  if (listed.problem || !listed.manifest) return t("addons.stateBroken");
  if (listed.ungranted?.length) return t("addons.statePermissions");
  return t("addons.stateOff");
}

function renderAddonsPill() {
  const count = addonsView().addons.length;
  $("addonsCount").textContent = count ? String(count) : "";
  $("addonsCount").hidden = !count;
}

// The add-ons pill: the installed add-ons (opening the ones that have a page) and the manager.
function renderAddonsPopover(focus) {
  const view = addonsView();
  const pages = new Set(slots.itemsOf("tabs").map((item) => item.id));
  const rows = view.addons.map((listed) => {
    const name = listed.manifest?.name ?? listed.id;
    const page = `addon:${listed.id}`;
    return el(
      "li",
      { class: `pop-row${listed.active ? " is-on" : ""}` },
      el("span", { class: "pop-mark", "aria-hidden": "true", text: name.slice(0, 1).toUpperCase() }),
      el("span", { class: "pop-name" }, el("span", { text: name }), el("span", { class: "pop-state", text: addonState(listed, view) })),
      pages.has(page) ? el("button", { class: "button small-button", type: "button", text: t("addonsOpen"), onclick: () => selectView(page) }) : null,
    );
  });
  const box = $("addonsPopover");
  box.replaceChildren(
    el("h2", { class: "pop-title", id: "addonsPopoverTitle", text: t("addonsPopover") }),
    rows.length ? el("ul", { class: "pop-list", role: "list" }, ...rows) : el("p", { class: "muted small", text: t("addonsNone") }),
    el("button", { class: "button pop-manage", type: "button", text: t("addonsManage"), onclick: () => { setPopup(null); openSettings("addons"); } }),
  );
  if (focus) box.querySelector("button")?.focus();
}

function renderNewsPill() {
  const update = state.update;
  $("newsVersion").textContent = update?.game_version ?? "";
  $("newsDot").hidden = !(update?.game_update || (update?.launcher_update && update.launcher_plan === "offer"));
}

// Generic link icons (no brand logos). Which links there are, and where they go, is built into
// the launcher (DWE_LAUNCHER_LINKS, src-tauri/src/links.rs); by default none, and the row is hidden.
const LINK_ICONS = {
  repo: '<path d="m8.5 8-4 4 4 4M15.5 8l4 4-4 4M13.5 5l-3 14"/>',
  community: '<circle cx="9" cy="9" r="3.2"/><path d="M3.5 19c.6-3 2.8-4.8 5.5-4.8s4.9 1.8 5.5 4.8"/><circle cx="16.5" cy="8" r="2.5"/><path d="M15.5 13.3c2.6-.3 4.6 1.3 5 4.2"/>',
  web: '<circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.4 2.4 3.5 5.3 3.5 8.5s-1.1 6.1-3.5 8.5c-2.4-2.4-3.5-5.3-3.5-8.5s1.1-6.1 3.5-8.5Z"/>',
};

function renderLinks() {
  const links = (state.status?.links ?? []).filter((link) => LINK_ICONS[link.kind] && link.url.startsWith("https://"));
  $("links").hidden = !links.length;
  $("links").replaceChildren(
    ...links.map((link) => {
      const label = t(`links.${link.kind}`);
      const button = el("button", { class: "link-icon", type: "button", "data-tip": label, onclick: () => openUrl(link.url).catch((failure) => showMessage(t("errorTitle"), String(failure))) }, icon(LINK_ICONS[link.kind], 20), el("span", { class: "sr-only", text: label }));
      return el("li", {}, button);
    }),
  );
}

function showAbout() {
  const s = state.status;
  showMessage(
    t("about.title"),
    `${t("about.body", { launcher: s?.launcher_version ?? "?", game: s?.game?.version ?? t("s.none"), platform: s?.platform ?? "?" })}\n\n${t("about.keys")}`,
  );
}

// A failure whose detail runs over several lines (dwx names each asset it could not open) shows its
// first line as the message and the rest as a log that scrolls.
function showFailure(failure) {
  const [first, ...rest] = failureText(failure).split("\n");
  showMessage(t("errorTitle"), first, rest.length > 0 ? rest.join("\n") : null);
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
      const backdrop = $("backdropArt");
      backdrop.style.setProperty("background-image", `url("${still}")`);
      backdrop.classList.add("has-still");
    }
    state.backgroundLoaded = true;
  }
  renderAll();
}

// quiet: an automatic update; a failure is a toast (and Settings → Updates), not a dialog.
async function runTask(work, { recheck = true, quiet = false } = {}) {
  if (state.task) return;
  state.task = { task: "identify" };
  state.discMoved = null;
  renderAll();
  try {
    state.status = await work();
    state.update = null;
    announce(t("readyTitle"));
    if (recheck && state.status.check_updates) checkUpdates();
    else if (!recheck) checkUpdates({ apply: false, record: false });
  } catch (failure) {
    if (failure?.code === "disc-moved") {
      state.discMoved = failure.detail ?? "";
    } else if (failure?.code === "no-space") {
      checkRequirements(true);
    } else if (quiet && failure?.code !== "cancelled") {
      toast({ title: t("autoGameFailedTitle"), text: t("autoGameFailedBody", { reason: failureText(failure) }), kind: "warning" });
    } else if (failure?.code !== "cancelled") {
      showFailure(failure);
    }
  } finally {
    state.task = null;
    await refresh();
    // A new game (or a fuller disk) may change what this computer can do.
    checkRequirements();
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
    // The notice on the home view says what is missing and what to do.
    if (failure?.code === "no-space" || failure?.code === "no-gpu") checkRequirements(true);
    else showFailure(failure);
  }
}

// Looks for updates and, unless apply is false, does what the backend's plan says
// (src-tauri/src/auto_update.rs): installs what the player let it install, offers the rest.
async function checkUpdates({ apply = true, record = true } = {}) {
  if (state.checking) return;
  state.checking = true;
  slots.render("settings", context());
  try {
    state.update = await api.checkUpdates(record);
    state.updateFailure = null;
  } catch (failure) {
    state.update = null;
    state.updateFailure = failure;
  } finally {
    state.checking = false;
    await refresh();
  }
  if (apply && state.update) await applyUpdates();
}

async function applyUpdates() {
  const update = state.update;
  if (update.launcher_plan === "install") await installLauncher(false);
  if (update.game_plan === "install" && !state.task) await runTask(() => api.updateGame(), { recheck: false, quiet: true });
}

// Installs the new launcher; restart: start it right away (the player's button).
async function installLauncher(restart) {
  if (state.launcherInstalling) return;
  state.launcherInstalling = true;
  state.launcherLater = false;
  renderAll();
  try {
    await api.installLauncherUpdate();
    if (restart) await api.restartLauncher();
  } catch (failure) {
    if (restart) showFailure(failure);
  } finally {
    state.launcherInstalling = false;
    // Its progress came through the task events: the bar is done.
    if (state.task?.task === "launcher") state.task = null;
    await refresh();
  }
}

async function restartLauncher() {
  try {
    await api.restartLauncher();
  } catch (failure) {
    showFailure(failure);
  }
}

// ---- Built-in slot items -------------------------------------------------------------------

const NAV_ICONS = {
  home: '<path d="M4 11.2 12 4.5l8 6.7"/><path d="M6.2 9.6V19.5h4.3v-5h3v5h4.3V9.6"/>',
  images: '<rect x="3.5" y="5" width="17" height="14" rx="2.2"/><circle cx="9" cy="10" r="1.6"/><path d="m4 17 5-4.5 3.5 3 3-2.5 4.5 4"/>',
  games: '<rect x="4" y="4" width="6.8" height="6.8" rx="1.6"/><rect x="13.2" y="4" width="6.8" height="6.8" rx="1.6"/><rect x="4" y="13.2" width="6.8" height="6.8" rx="1.6"/><rect x="13.2" y="13.2" width="6.8" height="6.8" rx="1.6"/>',
  addons: '<path d="M9 4h4v2.2a1.8 1.8 0 1 0 3.6 0V4H20v5h-1.8a1.8 1.8 0 1 0 0 3.6H20V20h-5v-1.8a1.8 1.8 0 1 0-3.6 0V20H4v-7h1.8a1.8 1.8 0 1 0 0-3.6H4V4h5Z"/>',
  settings: '<path d="M12 15.5a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7Zm7.4-2.1.9-1.4-.9-1.4-1.7-.3-.6-1.5.6-1.6-1.2-1.2-1.6.6-1.5-.6-.3-1.7L12 3.5l-1.4.9-.3 1.7-1.5.6-1.6-.6-1.2 1.2.6 1.6-.6 1.5-1.7.3-.9 1.4.9 1.4 1.7.3.6 1.5-.6 1.6 1.2 1.2 1.6-.6 1.5.6.3 1.7 1.4.9 1.4-.9.3-1.7 1.5-.6 1.6.6 1.2-1.2-.6-1.6.6-1.5Z"/>',
};

const navIcon = (paths) =>
  `<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" focusable="false">${paths}</svg>`;

function registerBuiltIns() {
  // The sidebar: three views of this page and two shortcuts to the settings. Add-ons add their
  // icons after these (order 200), below a divider.
  const nav = (id, order, extra) => slots.register("sidebar", { id, order, label: () => t(`nav.${id}`), icon: () => navIcon(NAV_ICONS[id]), ...extra });
  nav("home", 0, { view: "home" });
  nav("games", 10, { view: "games" });
  nav("images", 15, { view: "images" });
  nav("addons", 20, { onSelect: () => openSettings("addons") });
  nav("settings", 30, { onSelect: () => openSettings() });

  slots.register("news", {
    id: "launcher-update",
    order: 0,
    render: (card) => {
      // Only when the player installs it (automatic updates off, or this copy cannot replace
      // itself); otherwise it installs by itself and the notice offers the restart.
      if (!state.update?.launcher_update || state.update.launcher_plan !== "offer") return false;
      card.append(
        el("div", { class: "card-title" }, el("h2", { text: t("newsLauncher", { version: state.update.launcher_update }) })),
        el("button", {
          class: "button",
          type: "button",
          text: state.launcherInstalling ? t("launcherInstalling") : t("newsLauncherButton"),
          disabled: state.launcherInstalling || Boolean(state.status?.game_running),
          onclick: () => installLauncher(true),
        }),
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
      const notes = el("div", { class: `notes${state.newsFull ? " full" : ""}` });
      if (state.update?.notes) {
        renderNotes(notes, state.update.notes, state.newsFull ? Infinity : 6);
      } else if (state.updateFailure) {
        notes.append(el("p", { text: `${t("newsOffline")} ${failureText(state.updateFailure)}` }));
      } else {
        notes.append(el("p", { text: t("newsEmpty") }));
      }
      card.append(title, notes);
      if (state.update?.notes) {
        card.append(
          el("button", {
            class: "link-button news-more", type: "button", text: state.newsFull ? t("newsLess") : t("newsMore"),
            onclick: () => {
              state.newsFull = !state.newsFull;
              slots.render("news", context());
              $("newsPanel").querySelector(".news-more")?.focus();
            },
          }),
        );
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

  section("requirements", "general", 20, "req.title", (box) => {
    const req = state.requirements;
    const lines = [el("p", { text: t("req.body") })];
    if (!req) {
      lines.push(el("p", { class: "muted", text: t("req.checking") }));
    } else {
      const space = req.space;
      const spaceOk = space.install_ok && space.play_ok;
      lines.push(
        el("p", { class: `status-line ${spaceOk ? "accent" : "warning"}`, role: "status",
          text: space.free == null ? t("req.spaceUnknown", { folder: space.folder }) : t("req.space", { folder: space.folder, free: formatBytes(space.free) }) }),
        el("p", { class: "muted small", text: t("req.spaceNeeds", { install: formatBytes(space.install_needed), play: formatBytes(space.play_needed) }) }),
      );
      const graphics = req.graphics;
      const kind = graphics?.graphics ?? "unknown";
      const gpu = graphics?.gpu;
      const text = {
        hardware: t("req.gpuHardware", { name: gpu?.name ?? "", driver: gpu?.driver ?? "" }),
        software: t("req.gpuSoftware", { gpu: gpuName(graphics) }),
        fallback: t("req.gpuFallback", { gpu: gpuName(graphics) }),
        blocked: t("req.gpuBlocked", { gpu: gpuName(graphics) }),
        unknown: graphics ? t("req.gpuNoAnswer") : t("req.gpuUnknown"),
      }[kind];
      lines.push(el("p", { class: `status-line ${kind === "hardware" ? "accent" : kind === "unknown" ? "muted" : "warning"}`, role: "status", text }));
      if (kind === "fallback" || kind === "software" || kind === "blocked") lines.push(el("div", { class: "row" }, ...driverButtons(req)));
    }
    lines.push(el("div", { class: "row" }, againButton()));
    add(box, ...lines);
  });

  section("updates", "updates", 0, "s.updatesTitle", (box) => {
    const s = state.status;
    const toggle = (id, label, hint, checked, onchange, disabled = false) => {
      const input = el("input", { type: "checkbox", role: "switch", class: "switch", id, onchange: (e) => settingCall(() => onchange(e.target.checked)) });
      input.checked = checked;
      input.disabled = disabled;
      return el("div", { class: "row between" }, el("label", { for: id, class: "row-label" }, el("span", { text: label }), el("span", { class: "muted small", text: hint })), input);
    };
    const channel = el(
      "select",
      { id: "updateChannel", class: "compact", onchange: (e) => changeChannel(e.target.value) },
      el("option", { value: "stable", text: t("s.channelStable") }),
      el("option", { value: "testing", text: t("s.channelTesting") }),
    );
    channel.value = s.update_channel;
    channel.disabled = state.checking || Boolean(state.task) || state.launcherInstalling;
    const last = lastCheckLine(s);
    add(box,
      toggle("autoUpdates", t("s.updatesAuto"), t("s.updatesAutoHint"), s.check_updates, (on) => api.setCheckUpdates(on)),
      toggle("autoLauncher", t("s.autoLauncher"), s.launcher_replaceable ? t("s.autoLauncherHint") : t("s.autoLauncherManual"), s.auto_update_launcher, (on) => api.setAutoUpdate("launcher", on)),
      toggle("autoGame", t("s.autoGame"), t("s.autoGameHint"), s.auto_update_game, (on) => api.setAutoUpdate("game", on)),
      el("div", { class: "row between" }, el("label", { for: "updateChannel", class: "row-label" }, el("span", { text: t("s.channel") }), el("span", { class: "muted small", text: t("s.channelHint") })), channel),
      el(
        "div",
        { class: "row" },
        el("button", {
          class: "button",
          type: "button",
          text: last.failed ? t("s.updatesRetry") : t("s.updatesNow"),
          disabled: state.checking || Boolean(state.task) || state.launcherInstalling,
          onclick: () => checkUpdates(),
        }),
        el("p", { class: `status-line ${last.tone}`, role: "status", text: last.text }),
      ),
      last.when ? el("p", { class: "muted small", text: last.when }) : null,
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

// The last check (kept in settings.json by the backend): when, and what it did.
function lastCheckLine(s) {
  if (state.checking) return { text: t("s.updatesChecking"), tone: "muted" };
  if (state.launcherInstalling) return { text: t("launcherInstalling"), tone: "accent" };
  const last = s.last_check;
  if (!last?.at) return { text: t("s.updatesNever"), tone: "muted" };
  const when = t("s.updatesWhen", { when: new Date(last.at * 1000).toLocaleString(getLanguage(), { dateStyle: "medium", timeStyle: "short" }) });
  const what = [
    last.launcher ? t("s.updatesWhatLauncher", { version: last.launcher }) : null,
    last.game ? t("s.updatesWhatGame", { version: last.game }) : null,
  ].filter(Boolean).join(", ");
  switch (last.outcome) {
    case "updated":
      return { text: t("s.updatesUpdated", { what }), tone: "accent", when };
    case "available":
      return { text: t("s.updatesAvailable", { what }), tone: "accent", when };
    case "failed":
      return { text: t("s.updatesFailed", { reason: failureText({ code: last.code, detail: last.detail ?? "" }) }), tone: "warning", when, failed: true };
    default:
      return { text: t("s.updatesLatest"), tone: "muted", when };
  }
}

// Another update channel: what the last check found belongs to the old one, so it is forgotten
// and the launcher looks again (an update is only ever a newer version, never an older one).
async function changeChannel(channel) {
  await settingCall(() => api.setUpdateChannel(channel));
  state.update = null;
  state.updateFailure = null;
  checkUpdates();
}

async function settingCall(work, reloadBackground = false) {
  try {
    state.status = await work();
    if (reloadBackground) {
      state.backgroundLoaded = false;
      $("backdropArt").classList.remove("has-still");
      $("backdropArt").style.removeProperty("background-image");
      state.update = null;
    }
    await refresh();
  } catch (failure) {
    showFailure(failure);
  }
}

// ---- Slot renderers ------------------------------------------------------------------------

const cssId = (id) => id.replace(/[^A-Za-z0-9_-]/g, "-");

// Shows one view of the main area: "home", "games" or an add-on's page ("addon:<id>").
function selectView(id) {
  if (settingsOpen()) $("settingsDialog").close();
  setPopup(null, false);
  state.view = id;
  renderViews();
  slots.render("tabs", context());
  slots.render("sidebar", context());
  const current = $("sidebar").querySelector('[aria-current="page"]');
  (current ?? document.getElementById(`panel-${cssId(id)}`))?.focus();
}

function mountSlots() {
  slots.mount("sidebar", (items) => {
    const list = $("sidebar");
    list.replaceChildren();
    let divided = false;
    for (const item of items) {
      if (item.addon && !divided) {
        divided = true;
        list.append(el("li", { class: "side-sep", "aria-hidden": "true" }));
      }
      const current = item.view ? state.view === item.view : item.addon && state.view === item.id;
      const label = item.label();
      const button = el("button", { class: `side-item${item.addon ? " is-addon" : ""}`, type: "button", "data-tip": label, "aria-disabled": item.disabled ? "true" : false, "aria-current": current ? "page" : false });
      // An add-on's icon is an image of its own folder (never markup in this page).
      if (item.iconUrl) button.append(el("img", { src: item.iconUrl, alt: "", width: "24", height: "24", draggable: "false" }));
      else button.insertAdjacentHTML("afterbegin", item.icon());
      button.append(el("span", { class: "sr-only", text: label }));
      if (item.disabled) button.addEventListener("click", (e) => e.preventDefault());
      else if (item.view) button.addEventListener("click", () => selectView(item.view));
      else if (item.onSelect) button.addEventListener("click", item.onSelect);
      list.append(el("li", {}, button));
    }
  });

  // The add-ons' pages. Panels are kept by id: switching only hides them, so an add-on's page (an
  // iframe) keeps its state and is never moved in the DOM.
  slots.mount("tabs", (items, ctx) => {
    const ids = items.map((i) => i.id);
    if (state.view.startsWith("addon:") && !ids.includes(state.view)) {
      state.view = "home";
      renderViews();
    }
    const panels = reconcile($("tabpanels"), ids, (id) => {
      const item = items.find((i) => i.id === id);
      const panel = el("div", { class: `tabpanel${item.persistent ? " is-addon" : ""}`, role: "region", id: `panel-${cssId(id)}`, "aria-label": item.label(), tabindex: "-1" });
      if (item.persistent) item.render(panel, ctx);
      return panel;
    });
    for (const item of items) {
      const selected = item.id === state.view;
      const panel = panels.get(item.id);
      panel.hidden = !selected;
      if (!selected) continue;
      $("addonTitle").textContent = item.label();
      if (!item.persistent) {
        panel.replaceChildren();
        item.render(panel, ctx);
      }
    }
  });

  // The news panel (the bottom pill): the launcher's cards and the add-ons'.
  slots.mount("news", (items, ctx) => {
    const container = $("news");
    container.replaceChildren();
    for (const item of items) {
      const card = el("article", { class: "card", "data-item": item.id });
      if (item.render(card, ctx) !== false) container.append(card);
    }
  });

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
  renderLinks();
  const ctx = context();
  slots.renderAll(ctx);
  renderActionBar();
  if (openPopup === "addons") renderAddonsPopover(false);
  publishStatus({ running: ctx.gameRunning, busy: ctx.busy, ready: ctx.ready, gameVersion: ctx.gameVersion, language: ctx.language });
}

// ---- Start -----------------------------------------------------------------------------------

function wireControls() {
  $("aboutButton").addEventListener("click", showAbout);
  $("messageDismiss").addEventListener("click", () => $("messageDialog").close());
  $("stopButton").addEventListener("click", () => api.cancel());
  $("configureButton").addEventListener("click", () => togglePopup("configure"));
  $("configureButton").addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      setPopup("configure");
    }
  });
  $("configureMenu").addEventListener("keydown", menuKeys);
  $("addonsPill").addEventListener("click", () => togglePopup("addons"));
  $("newsPill").addEventListener("click", () => togglePopup("news"));
  // A click outside the open popup, or Esc, closes it (dialogs handle their own Esc).
  document.addEventListener("pointerdown", (e) => {
    if (openPopup && !e.target.closest(".menu-wrap")) setPopup(null, false);
  });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !document.querySelector("dialog[open]") && closePopup()) e.preventDefault();
  });
  // ↑/↓ move through the sidebar's buttons.
  $("rail").addEventListener("keydown", (e) => {
    const step = { ArrowDown: 1, ArrowUp: -1 }[e.key];
    if (!step) return;
    const buttons = [...$("rail").querySelectorAll("button")];
    const at = buttons.indexOf(document.activeElement);
    if (at < 0) return;
    e.preventDefault();
    buttons[(at + step + buttons.length) % buttons.length].focus();
  });
}

async function start() {
  registerBuiltIns();
  mountSlots();
  initAddons({ language: getLanguage, selectTab: selectView, changed: () => renderAll(), t });
  initAddonManager({ message: showMessage, redraw: () => renderAll() });
  wireControls();

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
    // An update found while the game ran waited for it to end.
    if (state.update?.launcher_plan === "wait" || state.update?.game_plan === "wait") checkUpdates();
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
    // Over Images, a drop is a folder or .zip of the player's images.
    if (state.view === "images") {
      if (event.type === "drop") dropOnImages(event.paths?.[0]);
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
  checkRequirements();
  try {
    await reloadAddons();
  } catch (failure) {
    showFailure(failure);
  }
  if (state.status.check_updates) checkUpdates();
}

start().catch((error) => showMessage(t("errorTitle"), String(error)));
