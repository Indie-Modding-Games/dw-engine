// The Images section (docs/guides/imagenes-personalizadas.md): export the images the game read
// from the player's disc, load a folder or .zip of improved ones (with a summary first), browse
// every map, grouped by the letters of its id, and every title, menu and icon image, assign or
// reset one, and turn them all on or off. Everything lands in one pack the game mounts
// (src-tauri/src/custom_images).
import { api, pickFile, pickFolder } from "./api.js";
import { $, ask, el, toast } from "./dom.js";
import { failureText, t } from "./i18n.js";

// How many refused files the summary names before "and N more".
const LISTED_PROBLEMS = 12;
const NOT_READY = "images-not-ready";
const ZIP_EXTENSIONS = ["zip"];
const PNG_EXTENSIONS = ["png"];

const view = {
  overview: null,
  preparing: false,
  loading: false,
  busy: false,
  failure: null,
  bulk: null,
  bulkPath: null,
  query: "",
  open: new Set(),
  thumbs: new Map(),
  built: false,
};

// Called whenever the launcher draws its views while the section is shown: the first time it
// wires the section and loads (and prepares) the list; later draws keep what is open.
export async function showImages() {
  if (!view.built) {
    build();
    renderAll();
  }
  if (!view.overview && !view.loading && !view.failure) await load();
}

// A folder or .zip dropped on the section.
export function dropOnImages(path) {
  if (path) inspect(path);
}

function build() {
  if (view.built) return;
  view.built = true;
  $("imagesSearch").addEventListener("input", (event) => {
    view.query = event.target.value.trim().toLowerCase();
    renderList();
  });
}

function renderAll() {
  renderStatus();
  renderToolbar();
  renderBulk();
  renderList();
}

async function run(work) {
  if (view.busy) return;
  view.busy = true;
  renderToolbar();
  try {
    await work();
  } catch (failure) {
    toast({ title: t("errorTitle"), text: failureText(failure), kind: "warning" });
  } finally {
    view.busy = false;
    renderAll();
  }
}

async function load() {
  view.loading = true;
  view.failure = null;
  renderStatus();
  try {
    let overview = await api.imagesOverview();
    if (!overview.prepared) {
      view.preparing = true;
      renderStatus();
      overview = await api.imagesPrepare();
    }
    view.overview = overview;
  } catch (failure) {
    view.failure = failure;
  } finally {
    view.loading = false;
    view.preparing = false;
    renderAll();
  }
}

async function refresh() {
  for (const key of [...view.thumbs.keys()]) if (key.startsWith("custom:")) view.thumbs.delete(key);
  view.overview = await api.imagesOverview();
}

function renderStatus() {
  const status = $("imagesStatus");
  if (!status) return;
  status.replaceChildren();
  if (view.preparing || view.loading) {
    status.append(el("p", { class: "muted images-note", text: view.preparing ? t("images.preparing") : t("images.notReady") }));
  } else if (view.failure) {
    status.append(
      el("p", { class: "images-note is-warning", text: failureText(view.failure) }),
      view.failure.code === NOT_READY || view.failure.code === "images-export-failed"
        ? el("button", { class: "button", type: "button", text: t("images.prepare"), onclick: load })
        : null,
    );
  }
}

function renderToolbar() {
  const bar = $("imagesToolbar");
  if (!bar) return;
  const ready = Boolean(view.overview?.prepared);
  bar.hidden = !ready;
  if (!ready) return;
  const count = view.overview.custom_count;
  const toggle = el("input", {
    type: "checkbox", role: "switch", class: "switch", id: "imagesEnabled", checked: view.overview.enabled, disabled: view.busy,
    onchange: (event) => run(async () => {
      await api.imagesSetEnabled(event.target.checked);
      await refresh();
    }),
  });
  bar.replaceChildren(
    el(
      "div", { class: "images-toolbar-row" },
      el("label", { class: "switch-label images-switch", for: "imagesEnabled" }, toggle, el("span", { text: t("images.enabled") })),
      el("span", { class: "muted", text: count ? t("images.count", { count }) : t("images.countNone") }),
    ),
    el(
      "div", { class: "images-toolbar-row" },
      el("button", { class: "button", type: "button", text: t("images.export"), disabled: view.busy, onclick: exportOriginals }),
      el("button", { class: "button primary", type: "button", text: t("images.loadFolder"), disabled: view.busy, onclick: loadFolder }),
      el("button", { class: "button", type: "button", text: t("images.loadZip"), disabled: view.busy, onclick: loadZip }),
      count ? el("button", { class: "button subtle", type: "button", text: t("images.removeAll"), disabled: view.busy, onclick: removeAll }) : null,
    ),
    el("p", { class: "muted small", text: t("images.dropHint") }),
  );
}

function exportOriginals() {
  run(async () => {
    const folder = await pickFolder(t("images.exportPick"));
    if (!folder) return;
    const written = await api.imagesExport(folder);
    toast({ title: t("images.title"), text: t("images.exported", { path: written }) });
  });
}

async function loadFolder() {
  const folder = await pickFolder(t("images.loadPick"));
  if (folder) inspect(folder);
}

async function loadZip() {
  const file = await pickFile(t("images.zipPick"), t("images.zipFilter"), ZIP_EXTENSIONS);
  if (file) inspect(file);
}

function removeAll() {
  run(async () => {
    const body = el("p", { text: t("images.removeBody") });
    const sure = await ask({ title: t("images.removeTitle"), body, ok: t("images.removeOk"), cancel: t("images.cancel"), danger: true });
    if (!sure) return;
    await api.imagesRemoveAll();
    await refresh();
  });
}

function inspect(path) {
  run(async () => {
    view.bulk = await api.imagesInspect(path);
    view.bulkPath = path;
  });
}

function problemText(problem) {
  return t(`images.problem.${problem.kind}`, problem);
}

function baseName(path) {
  return path.split(/[\\/]/).filter(Boolean).pop() ?? path;
}

function renderBulk() {
  const box = $("imagesBulk");
  if (!box) return;
  box.replaceChildren();
  const bulk = view.bulk;
  if (!bulk) return;
  const matched = bulk.matched.length;
  const lines = [
    el("li", { class: "is-ok", text: t("images.bulkMatched", { count: matched }) }),
    bulk.unchanged ? el("li", { text: t("images.bulkUnchanged", { count: bulk.unchanged }) }) : null,
    bulk.ignored ? el("li", { text: t("images.bulkIgnored", { count: bulk.ignored }) }) : null,
  ];
  const refused = bulk.rejected.length
    ? el(
        "div", { class: "images-refused" },
        el("p", { class: "is-warning", text: t("images.bulkRejected", { count: bulk.rejected.length }) }),
        el(
          "ul", {},
          ...bulk.rejected.slice(0, LISTED_PROBLEMS).map((r) => el("li", {}, el("code", { text: r.name }), el("span", { text: ` — ${problemText(r.problem)}` }))),
          bulk.rejected.length > LISTED_PROBLEMS ? el("li", { class: "muted", text: t("images.more", { count: bulk.rejected.length - LISTED_PROBLEMS }) }) : null,
        ),
      )
    : null;
  const close = () => {
    view.bulk = null;
    renderBulk();
  };
  const apply = () => run(async () => {
    const applied = await api.imagesApply(view.bulkPath);
    view.bulk = null;
    await refresh();
    toast({ title: t("images.title"), text: t("images.bulkApplied", { count: applied.matched.length }) });
  });
  box.append(
    el(
      "section", { class: "card images-bulk", "aria-labelledby": "imagesBulkTitle" },
      el("h2", { id: "imagesBulkTitle", text: t("images.bulkTitle", { name: baseName(view.bulkPath) }) }),
      el("ul", { class: "images-bulk-counts" }, ...lines.filter(Boolean)),
      refused,
      matched ? null : el("p", { class: "muted", text: t("images.bulkNothing") }),
      el(
        "div", { class: "row" },
        el("button", { class: "button primary", type: "button", text: t("images.bulkApply", { count: matched }), disabled: !matched || view.busy, onclick: apply }),
        el("button", { class: "button", type: "button", text: t("images.cancel"), onclick: close }),
      ),
    ),
  );
}

// The names a row is found by: the map's or image's name, its id, the group's.
function entryLabel(entry) {
  if (entry.name) return entry.name;
  const known = t(`images.known.${entry.id}`);
  return known.startsWith("images.") ? entry.id.split("/").pop() : known;
}

const PALETTE_VARIANT = "palette";

function variantLabel(group, file) {
  if (group.kind === "images") return t("images.palette", { number: file.variant });
  if (file.variant.startsWith(PALETTE_VARIANT)) return t("images.palette", { number: file.variant.slice(PALETTE_VARIANT.length) });
  const known = t(`images.variant.${file.variant}`);
  return known.startsWith("images.") ? file.variant : known;
}

function matches(group, entry) {
  if (!view.query) return true;
  const text = `${group.key} ${group.name} ${entry.id} ${entryLabel(entry)}`.toLowerCase();
  return text.includes(view.query);
}

function renderList() {
  const list = $("imagesList");
  if (!list) return;
  list.replaceChildren();
  const groups = view.overview?.groups ?? [];
  const search = $("imagesSearch");
  search.hidden = !groups.length;
  search.placeholder = t("images.search");
  search.setAttribute("aria-label", t("images.search"));
  for (const [kind, title] of [["maps", t("images.mapsTitle")], ["images", t("images.imagesTitle")]]) {
    const shown = groups
      .filter((group) => group.kind === kind)
      .map((group) => ({ group, entries: group.entries.filter((entry) => matches(group, entry)) }))
      .filter(({ entries }) => entries.length);
    if (!shown.length) continue;
    list.append(el("h2", { class: "images-kind", text: title }), ...shown.map(({ group, entries }) => groupNode(group, entries)));
  }
}

function groupNode(group, entries) {
  const key = `${group.kind}:${group.key}`;
  const open = view.open.has(key) || Boolean(view.query);
  const custom = entries.reduce((sum, entry) => sum + entry.files.filter((f) => f.custom).length, 0);
  const count = group.kind === "maps" ? t("images.mapsCount", { count: entries.length }) : t("images.imagesCount", { count: entries.length });
  const details = el(
    "details", { class: "images-group", open },
    el(
      "summary", {},
      el("span", { class: "images-group-name", text: group.name || group.key }),
      group.name ? el("span", { class: "chip", text: group.key }) : null,
      el("span", { class: "muted", text: count }),
      custom ? el("span", { class: "chip is-new", text: t("images.customCount", { count: custom }) }) : null,
    ),
  );
  const body = el("div", { class: "images-entries" });
  details.append(body);
  const fill = () => body.replaceChildren(...entries.map((entry) => entryNode(group, entry)));
  if (open) fill();
  details.addEventListener("toggle", () => {
    if (details.open) {
      view.open.add(key);
      if (!body.childElementCount) fill();
    } else {
      view.open.delete(key);
    }
  });
  return details;
}

function entryNode(group, entry) {
  return el(
    "article", { class: "images-entry" },
    el("h3", {}, el("span", { text: entryLabel(entry) }), el("code", { class: "muted", text: entry.id })),
    el("div", { class: "images-files" }, ...entry.files.map((file) => fileNode(group, file))),
  );
}

function thumbnail(target, custom, label) {
  const key = `${custom ? "custom" : "original"}:${target}`;
  const image = el("img", { class: `images-thumb${custom ? "" : " is-original"}`, alt: label, decoding: "async" });
  const cached = view.thumbs.get(key);
  if (cached) {
    image.src = cached;
  } else {
    api.imagesThumbnail(target, custom).then((url) => {
      view.thumbs.set(key, url);
      image.src = url;
    }).catch(() => image.classList.add("is-missing"));
  }
  return image;
}

function fileNode(group, file) {
  const label = variantLabel(group, file);
  const original = el("figure", {}, thumbnail(file.target, false, `${t("images.original")} ${label}`), el("figcaption", { text: `${t("images.original")} · ${t("images.size", file)}` }));
  const mine = file.custom
    ? el("figure", {}, thumbnail(file.target, true, `${t("images.custom")} ${label}`), el("figcaption", { class: "is-ok", text: `${t("images.custom")} · ${file.custom_scale ? t("images.scale", { scale: file.custom_scale }) : "?"}` }))
    : el("figure", { class: "is-empty" }, el("div", { class: "images-thumb images-placeholder", text: t("images.none") }), el("figcaption", { text: t("images.custom") }));
  return el(
    "div", { class: "images-file" },
    el("p", { class: "images-variant", text: label }),
    el("div", { class: "images-pair" }, original, mine),
    el(
      "div", { class: "row" },
      el("button", { class: "button small-button", type: "button", text: file.custom ? t("images.replace") : t("images.assign"), disabled: view.busy, onclick: () => assign(file.target) }),
      file.custom ? el("button", { class: "button subtle small-button", type: "button", text: t("images.reset"), disabled: view.busy, onclick: () => reset(file.target) }) : null,
    ),
  );
}

function assign(target) {
  run(async () => {
    const path = await pickFile(t("images.pngPick"), t("images.pngFilter"), PNG_EXTENSIONS);
    if (!path) return;
    const result = await api.imagesAssign(target, path);
    if (result.problem) {
      toast({ title: baseName(path), text: problemText(result.problem), kind: "warning" });
      return;
    }
    await refresh();
    toast({ title: t("images.title"), text: t("images.assigned", { scale: result.scale }) });
  });
}

function reset(target) {
  run(async () => {
    await api.imagesReset(target);
    await refresh();
    toast({ title: t("images.title"), text: t("images.resetDone") });
  });
}
