// The add-on manager (Settings → Add-ons): install from a .zip or a folder (picker or drop),
// the permission prompt, the list with on/off and remove, and safe mode (Settings → Advanced).
// Installing always shows what the add-on asks for; declining throws the package away.
import { addonsView, reloadAddons } from "./addons.js";
import { api, pickAddonFile, pickFolder } from "./api.js";
import { add, ask, el, toast } from "./dom.js";
import { failureText, t } from "./i18n.js";
import * as slots from "./slots.js";

let dragging = false;
let installing = false;
let hooks = null;

// hooks: { message(title, body), redraw() }
export function initAddonManager(given) {
  hooks = given;
  registerSections();
}

export function setAddonDragging(value) {
  if (dragging === value) return;
  dragging = value;
  slots.render("settings", null);
}

function permissionText(key) {
  if (key.startsWith("net:")) return t("addons.perm.net", { host: key.slice(4) });
  return t(`addons.perm.${key}`);
}

// What an add-on is and what it asks for, as the prompt and the list show it.
function permissionList(keys, fresh = []) {
  if (!keys.length) return el("p", { class: "muted", text: t("addons.permNone") });
  return el(
    "ul",
    { class: "perm-list" },
    ...keys.map((key) =>
      el(
        "li",
        { class: fresh.includes(key) ? "is-new" : "" },
        el("span", { text: permissionText(key) }),
        fresh.includes(key) ? el("span", { class: "chip is-new", text: t("addons.new") }) : null,
      ),
    ),
  );
}

function slotChips(slotNames) {
  return el("div", { class: "chips" }, ...slotNames.map((slot) => el("span", { class: "chip", text: t(`addons.slot.${slot}`) })));
}

function promptBody(manifest, { replaces = null, fresh, keys }) {
  const meta = replaces ? t("addons.metaUpdate", { version: manifest.version, author: manifest.author, old: replaces }) : t("addons.meta", { version: manifest.version, author: manifest.author });
  return add(
    el("div", { class: "addon-prompt" }),
    el("p", { class: "addon-meta", text: meta }),
    manifest.description ? el("p", { class: "addon-description", text: manifest.description }) : null,
    el("h3", { text: t("addons.where") }),
    slotChips(manifest.slots),
    el("h3", { text: replaces && fresh.length ? t("addons.whatNew") : t("addons.what") }),
    permissionList(keys, replaces ? fresh : []),
    el("p", { class: "addon-safety", text: t("addons.safety") }),
  );
}

export async function installAddonFrom(path) {
  if (installing || !path) return;
  installing = true;
  try {
    const staged = await api.addonInspect(path);
    const { manifest } = staged;
    const keys = permissionKeys(manifest.permissions);
    const accepted = await ask({
      title: staged.replaces ? t("addons.askUpdate", { name: manifest.name }) : t("addons.askInstall", { name: manifest.name }),
      body: promptBody(manifest, { replaces: staged.replaces, fresh: staged.new_permissions, keys }),
      ok: staged.replaces ? t("addons.update") : t("addons.install"),
      cancel: t("addons.cancel"),
    });
    await api.addonInstall(staged.ticket, accepted);
    if (accepted) toast({ title: manifest.name, text: t("addons.installed"), kind: "success" });
    await reloadAddons(accepted ? manifest.id : null);
  } catch (failure) {
    hooks.message(t("addons.errorTitle"), failureText(failure));
  } finally {
    installing = false;
    hooks.redraw();
  }
}

function permissionKeys(permissions) {
  return [...(permissions.storage ? ["storage"] : []), ...(permissions.notify ? ["notify"] : []), ...permissions.hosts.map((h) => `net:${h}`)];
}

async function setEnabled(listed, enabled) {
  try {
    if (enabled && listed.ungranted.length) {
      const ok = await ask({
        title: t("addons.askPermissions", { name: listed.manifest.name }),
        body: promptBody(listed.manifest, { replaces: listed.manifest.version, fresh: listed.ungranted, keys: permissionKeys(listed.manifest.permissions) }),
        ok: t("addons.accept"),
        cancel: t("addons.cancel"),
      });
      if (!ok) return hooks.redraw();
    }
    await api.addonSetEnabled(listed.id, enabled);
    await reloadAddons();
  } catch (failure) {
    hooks.message(t("errorTitle"), failureText(failure));
  }
  hooks.redraw();
}

async function uninstall(listed) {
  const name = listed.manifest?.name ?? listed.id;
  const ok = await ask({
    title: t("addons.askRemove", { name }),
    body: el("p", { text: t("addons.removeBody") }),
    ok: t("addons.remove"),
    cancel: t("addons.cancel"),
    danger: true,
  });
  if (!ok) return;
  try {
    await api.addonUninstall(listed.id);
    await reloadAddons();
    toast({ title: name, text: t("addons.removed") });
  } catch (failure) {
    hooks.message(t("errorTitle"), failureText(failure));
  }
  hooks.redraw();
}

export async function setSafeMode(on) {
  await api.addonsSafeMode(on);
  await reloadAddons();
  toast({ text: on ? t("addons.safeOnToast") : t("addons.safeOffToast"), kind: on ? "warning" : "info" });
  hooks.redraw();
}

function monogram(name) {
  return el("span", { class: "addon-mark", "aria-hidden": "true", text: (name || "?").trim().charAt(0).toUpperCase() });
}

function statusChip(listed, safeMode) {
  if (listed.problem) return el("span", { class: "chip is-danger", text: t("addons.stateBroken") });
  if (listed.enabled && listed.ungranted.length) return el("span", { class: "chip is-warning", text: t("addons.statePermissions") });
  if (listed.enabled && safeMode) return el("span", { class: "chip is-warning", text: t("addons.stateSafe") });
  if (listed.active) return el("span", { class: "chip is-on", text: t("addons.stateOn") });
  return el("span", { class: "chip", text: t("addons.stateOff") });
}

function addonRow(listed, safeMode) {
  const m = listed.manifest;
  const name = m?.name ?? listed.id;
  const switchId = `addon-switch-${listed.id.replace(/[^a-z0-9-]/g, "-")}`;
  const toggle = el("input", {
    type: "checkbox", role: "switch", class: "switch", id: switchId, disabled: Boolean(listed.problem),
    onchange: (e) => setEnabled(listed, e.target.checked),
  });
  toggle.checked = listed.enabled && !listed.ungranted.length;
  const details = add(
    el("div", { class: "addon-info" }),
    el("div", { class: "addon-name-line" }, el("strong", { class: "addon-name", text: name }), m ? el("span", { class: "addon-version", text: `v${m.version}` }) : null, statusChip(listed, safeMode)),
    m ? el("p", { class: "addon-meta", text: t("addons.by", { author: m.author, id: listed.id }) }) : el("p", { class: "addon-meta", text: listed.id }),
    m?.description ? el("p", { class: "addon-description", text: m.description }) : null,
    listed.problem ? el("p", { class: "warning", text: failureText(listed.problem) }) : null,
    m ? el("div", { class: "chips" }, ...m.slots.map((s) => el("span", { class: "chip is-quiet", text: t(`addons.slot.${s}`) })), ...permissionKeys(m.permissions).map((k) => el("span", { class: "chip is-quiet", text: permissionText(k) }))) : null,
  );
  return el(
    "li",
    { class: `addon-row${listed.active ? " is-active" : ""}` },
    monogram(name),
    details,
    el(
      "div",
      { class: "addon-actions" },
      el("label", { class: "switch-label", for: switchId }, el("span", { class: "sr-only", text: t("addons.toggle", { name }) }), toggle),
      el("button", { class: "button subtle", type: "button", text: t("addons.remove"), onclick: () => uninstall(listed) }),
    ),
  );
}

function registerSections() {
  slots.register("settings", {
    id: "addons-install",
    tab: "addons",
    order: 0,
    label: () => t("addons.installTitle"),
    render: (box) => {
      const view = addonsView();
      add(
        box,
        view.safe_mode
          ? el(
              "div",
              { class: "inline-notice", role: "status" },
              el("p", { text: t("addons.safeBanner") }),
              el("button", { class: "button", type: "button", text: t("addons.safeLeave"), onclick: () => setSafeMode(false) }),
            )
          : null,
        el(
          "button",
          { class: `dropzone compact${dragging ? " is-dragging" : ""}`, type: "button", id: "addonDrop", disabled: installing, onclick: chooseZip },
          el("span", { class: "dropzone-title", text: dragging ? t("addons.dropActive") : t("addons.drop") }),
          el("span", { class: "dropzone-hint", text: t("addons.dropHint") }),
        ),
        el(
          "div",
          { class: "row" },
          el("button", { class: "button", type: "button", text: t("addons.pickZip"), disabled: installing, onclick: chooseZip }),
          el("button", { class: "button", type: "button", text: t("addons.pickFolder"), disabled: installing, onclick: chooseFolder }),
        ),
        el("p", { class: "muted small", text: t("addons.safety") }),
      );
    },
  });

  slots.register("settings", {
    id: "addons-installed",
    tab: "addons",
    order: 10,
    label: () => t("addons.listTitle"),
    render: (box) => {
      const view = addonsView();
      if (!view.addons.length) {
        box.append(el("p", { class: "muted", text: t("addons.empty") }));
        return;
      }
      box.append(el("ul", { class: "addon-list", role: "list" }, ...view.addons.map((a) => addonRow(a, view.safe_mode))));
    },
  });

  slots.register("settings", {
    id: "safe-mode",
    tab: "advanced",
    order: 10,
    label: () => t("addons.safeTitle"),
    render: (box) => {
      const on = addonsView().safe_mode;
      add(
        box,
        el("p", { text: t("addons.safeBody") }),
        el("p", { class: on ? "warning" : "muted", text: on ? t("addons.safeIsOn") : t("addons.safeIsOff") }),
        el(
          "div",
          { class: "row" },
          el("button", { class: on ? "button" : "button primary", type: "button", text: on ? t("addons.safeLeave") : t("addons.safeEnter"), onclick: () => setSafeMode(!on) }),
        ),
      );
    },
  });
}

async function chooseZip() {
  const path = await pickAddonFile(t("addons.pickZip"), t("addons.pickFilter"));
  if (path) installAddonFrom(path);
}

async function chooseFolder() {
  const path = await pickFolder(t("addons.pickFolder"));
  if (path) installAddonFrom(path);
}
