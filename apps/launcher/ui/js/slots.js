// The page's mount points: the sidebar list, the main tabs, the news area and the settings
// sections. Each is a named slot that renders the items registered in it, in order. The launcher's
// own pieces register here exactly as a future add-on would (through a sandboxed bridge that only
// forwards safe calls: never the token, the disc path, extraction, updates or starting programs,
// which stay in api.js).
//
// An item: { id, order, label?(), render(container, context), disabled? }.
// - sidebar: label() and icon(): a rail button; render() is not used.
// - tabs: label() names the tab; render(container) fills its panel.
// - news: render(container) fills one card.
// - settings: label() is the section title; render(container) fills the section.

export const SLOT_NAMES = ["sidebar", "tabs", "news", "settings"];

const items = new Map(SLOT_NAMES.map((name) => [name, []]));
const mounts = new Map();

export function register(slot, item) {
  const list = items.get(slot);
  if (!list) throw new Error(`unknown slot ${slot}`);
  const at = list.findIndex((existing) => existing.id === item.id);
  if (at >= 0) list.splice(at, 1);
  list.push({ order: 100, ...item });
  list.sort((a, b) => a.order - b.order);
}

export function unregister(slot, id) {
  const list = items.get(slot);
  if (!list) return;
  const at = list.findIndex((existing) => existing.id === id);
  if (at >= 0) list.splice(at, 1);
}

export function itemsOf(slot) {
  return [...(items.get(slot) ?? [])];
}

// Where a slot renders: render(slot, context) calls the renderer given here.
export function mount(slot, renderer) {
  mounts.set(slot, renderer);
}

export function render(slot, context) {
  const renderer = mounts.get(slot);
  if (renderer) renderer(itemsOf(slot), context);
}

export function renderAll(context) {
  for (const slot of SLOT_NAMES) render(slot, context);
}
