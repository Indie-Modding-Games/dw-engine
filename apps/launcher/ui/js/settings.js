// The settings view: a dialog with a vertical list of tabs (General, Updates, Access, Disc,
// Add-ons, Advanced) and one panel per tab. Each panel shows the "settings" slot items whose tab
// is its own, in order, as cards. Panels and cards are kept (only their contents are drawn again),
// so an add-on's settings iframe is never reloaded by a redraw. Keyboard: arrows, Home and End in
// the list; Ctrl+Tab and Ctrl+Shift+Tab (or Ctrl+PageDown/PageUp) anywhere in the dialog.
import { $, add, el, icon, reconcile } from "./dom.js";
import { t } from "./i18n.js";
import * as slots from "./slots.js";

const ICONS = {
  general:
    '<path d="M21 4h-7M10 4H3M21 12h-9M8 12H3M21 20h-5M12 20H3M14 2v4M8 10v4M16 18v4"/>',
  updates:
    '<path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/><path d="M21 3v5h-5"/><path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/><path d="M8 16H3v5"/>',
  account:
    '<circle cx="8" cy="15" r="4"/><path d="m10.85 12.15 8.65-8.65M18 5l2 2M15 8l2 2"/>',
  disc: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="2.5"/><path d="M12 3a9 9 0 0 1 9 9"/>',
  addons:
    '<path d="M10 3h4v3a2 2 0 1 0 4 0V3h3v7h-3a2 2 0 1 0 0 4h3v7h-7v-3a2 2 0 1 0-4 0v3H3v-7h3a2 2 0 1 0 0-4H3V3h7z"/>',
  advanced: '<path d="m4 17 6-6-6-6"/><path d="M12 19h8"/>',
};

export const SETTINGS_TABS = ["general", "updates", "account", "disc", "addons", "advanced"];

let active = "general";

export const activeSettingsTab = () => active;
export const settingsOpen = () => $("settingsDialog").open;

function selectSettingsTab(id, focus = true) {
  if (!SETTINGS_TABS.includes(id)) return;
  active = id;
  slots.render("settings", null);
  if (focus) $(`stab-${id}`)?.focus();
}

function step(delta) {
  const index = SETTINGS_TABS.indexOf(active);
  selectSettingsTab(SETTINGS_TABS[(index + delta + SETTINGS_TABS.length) % SETTINGS_TABS.length]);
}

function renderNav() {
  const nav = $("settingsNav");
  const nodes = reconcile(nav, SETTINGS_TABS, (id) =>
    el(
      "button",
      { class: "settings-tab", type: "button", role: "tab", id: `stab-${id}`, "aria-controls": `spanel-${id}`, onclick: () => selectSettingsTab(id) },
      icon(ICONS[id], 20),
      el("span", { class: "settings-tab-label" }),
    ),
  );
  for (const [id, button] of nodes) {
    const selected = id === active;
    button.setAttribute("aria-selected", String(selected));
    button.tabIndex = selected ? 0 : -1;
    button.querySelector(".settings-tab-label").textContent = t(`st.${id}`);
  }
}

function renderPanels(items) {
  const panels = $("settingsPanels");
  const nodes = reconcile(panels, SETTINGS_TABS, (id) =>
    el(
      "section",
      { class: "settings-panel", role: "tabpanel", id: `spanel-${id}`, "aria-labelledby": `stab-${id}`, tabindex: "0" },
      el("header", { class: "panel-head" }, el("h3", { class: "panel-title" }), el("p", { class: "panel-lead" })),
      el("div", { class: "panel-body" }),
    ),
  );
  for (const [id, panel] of nodes) {
    panel.hidden = id !== active;
    panel.querySelector(".panel-title").textContent = t(`st.${id}`);
    panel.querySelector(".panel-lead").textContent = t(`st.${id}Lead`);
    const mine = items.filter((item) => (item.tab ?? "addons") === id);
    const body = panel.querySelector(".panel-body");
    const cards = reconcile(body, mine.map((item) => item.id), (key) => {
      const item = mine.find((i) => i.id === key);
      const card = el("section", { class: `setting${item.persistent ? " is-addon" : ""}`, "aria-labelledby": `setting-${cssId(key)}` });
      if (item.persistent) {
        // An add-on's section: drawn once, its iframe stays.
        add(card, el("h4", { id: `setting-${cssId(key)}`, text: item.label() }), item.byline ? el("p", { class: "addon-byline", text: t("addons.byline", { name: item.byline() }) }) : null);
        item.render(card);
      }
      return card;
    });
    // Only the panel on screen is drawn again (the others are drawn when they are opened).
    if (id !== active) continue;
    for (const item of mine) {
      if (item.persistent) continue;
      const card = cards.get(item.id);
      card.replaceChildren(el("h4", { id: `setting-${cssId(item.id)}`, text: item.label() }));
      item.render(card);
    }
  }
}

const cssId = (id) => id.replace(/[^A-Za-z0-9_-]/g, "-");

export function mountSettings() {
  slots.mount("settings", (items) => {
    renderNav();
    renderPanels(items);
  });
  $("settingsNav").addEventListener("keydown", (e) => {
    const moves = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 };
    if (moves[e.key]) step(moves[e.key]);
    else if (e.key === "Home") selectSettingsTab(SETTINGS_TABS[0]);
    else if (e.key === "End") selectSettingsTab(SETTINGS_TABS[SETTINGS_TABS.length - 1]);
    else return;
    e.preventDefault();
  });
  $("settingsDialog").addEventListener("keydown", (e) => {
    if (!e.ctrlKey) return;
    if (e.key === "Tab" || e.code === "Tab") step(e.shiftKey ? -1 : 1);
    else if (e.key === "PageDown") step(1);
    else if (e.key === "PageUp") step(-1);
    else return;
    e.preventDefault();
  });
  $("settingsClose").addEventListener("click", () => $("settingsDialog").close());
}

export function openSettings(tab = active) {
  active = SETTINGS_TABS.includes(tab) ? tab : "general";
  slots.render("settings", null);
  const dialog = $("settingsDialog");
  if (!dialog.open) dialog.showModal();
  $(`stab-${active}`)?.focus();
}
