// Small DOM helpers shared by the launcher's modules: building elements, keyed lists that keep
// their nodes (an add-on's iframe must never be moved: moving reloads it), toasts and the
// question dialog.

export const $ = (id) => document.getElementById(id);

// el("button", { class, text, onclick, ... }, ...children): an element; false/null props and
// children are left out.
export function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key === "class") node.className = value;
    else if (key === "text") node.textContent = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else if (value === true) node.setAttribute(key, "");
    else if (value !== false && value != null) node.setAttribute(key, value);
  }
  node.append(...children.filter((c) => c != null && c !== false));
  return node;
}

// append() without the null children of conditional parts.
export function add(parent, ...children) {
  parent.append(...children.filter((c) => c != null && c !== false));
  return parent;
}

// An inline SVG icon from the launcher's own path data (never from an add-on).
export function icon(paths, size = 20) {
  const span = el("span", { class: "icon", "aria-hidden": "true" });
  span.insertAdjacentHTML(
    "afterbegin",
    `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" focusable="false">${paths}</svg>`,
  );
  return span;
}

// Keeps exactly one child of container per key, in the given order. Existing children stay where
// they are (and keep their state); new ones come from make(key). Returns key -> node.
export function reconcile(container, keys, make) {
  const existing = new Map([...container.children].map((child) => [child.dataset.key, child]));
  for (const [key, node] of existing) if (!keys.includes(key)) node.remove();
  const nodes = new Map();
  let previous = null;
  for (const key of keys) {
    let node = existing.get(key);
    if (!node) {
      node = make(key);
      node.dataset.key = key;
    }
    const wanted = previous ? previous.nextSibling : container.firstChild;
    if (node !== wanted) container.insertBefore(node, wanted);
    nodes.set(key, node);
    previous = node;
  }
  return nodes;
}

// A modal dialog lives in the browser's top layer, above any z-index. When popovers exist the
// toast region is one, re-shown on every toast so it stacks above a dialog opened before it.
function raise(region) {
  if (typeof region.showPopover !== "function") return;
  if (!region.hasAttribute("popover")) region.setAttribute("popover", "manual");
  try {
    if (region.matches(":popover-open")) region.hidePopover();
    region.showPopover();
  } catch {
    // Not supported here: the region stays a plain fixed element.
  }
}

// A short notice in the corner: { title, text, kind: "info" | "success" | "warning" }.
export function toast({ title = "", text, kind = "info" }) {
  const region = $("toasts");
  if (!region) return;
  const node = el(
    "div",
    { class: `toast is-${kind}`, role: kind === "warning" ? "alert" : "status" },
    title ? el("strong", { class: "toast-title", text: title }) : null,
    el("span", { class: "toast-text", text }),
  );
  region.append(node);
  while (region.children.length > 4) region.firstElementChild.remove();
  raise(region);
  setTimeout(() => {
    node.classList.add("is-leaving");
    setTimeout(() => node.remove(), 220);
  }, 5000);
}

// The question dialog: resolves true when the player confirms. body is a node (built by the
// caller from text only). The cancel button has the focus first: nothing is accepted by Enter
// alone.
export function ask({ title, body, ok, cancel, danger = false }) {
  const dialog = $("askDialog");
  $("askTitle").textContent = title;
  $("askBody").replaceChildren(body);
  const okButton = $("askOk");
  const cancelButton = $("askCancel");
  okButton.textContent = ok;
  cancelButton.textContent = cancel;
  okButton.classList.toggle("danger", danger);
  return new Promise((resolve) => {
    const finish = (value) => {
      okButton.onclick = null;
      cancelButton.onclick = null;
      dialog.onclose = null;
      if (dialog.open) dialog.close();
      resolve(value);
    };
    okButton.onclick = () => finish(true);
    cancelButton.onclick = () => finish(false);
    dialog.onclose = () => finish(false);
    // autofocus makes showModal itself pick the cancel button; a focus() alone can lose to the
    // dialog when it opens on top of another modal.
    okButton.autofocus = false;
    cancelButton.autofocus = true;
    dialog.showModal();
    cancelButton.focus();
  });
}
