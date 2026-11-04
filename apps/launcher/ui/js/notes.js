// Release notes (Markdown from the release) as safe DOM: headings, lists, paragraphs, bold and
// code. Nothing from the notes is ever parsed as HTML.

function inline(parent, text) {
  const pattern = /(\*\*[^*]+\*\*|`[^`]+`|\[[^\]]+\]\([^)]+\))/g;
  let last = 0;
  for (const match of text.matchAll(pattern)) {
    if (match.index > last) parent.append(text.slice(last, match.index));
    const token = match[0];
    if (token.startsWith("**")) {
      const strong = document.createElement("strong");
      strong.textContent = token.slice(2, -2);
      parent.append(strong);
    } else if (token.startsWith("`")) {
      const code = document.createElement("code");
      code.textContent = token.slice(1, -1);
      parent.append(code);
    } else {
      parent.append(token.slice(1, token.indexOf("]")));
    }
    last = match.index + token.length;
  }
  if (last < text.length) parent.append(text.slice(last));
}

// Renders the notes into container; maxBlocks keeps only the first blocks (the news card).
export function renderNotes(container, markdown, maxBlocks = Infinity) {
  container.replaceChildren();
  let list = null;
  let blocks = 0;
  for (const raw of markdown.replace(/\r/g, "").split("\n")) {
    const line = raw.trim();
    if (blocks >= maxBlocks) break;
    if (!line) {
      list = null;
      continue;
    }
    const heading = line.match(/^(#{1,6})\s+(.*)$/);
    const item = line.match(/^[-*]\s+(.*)$/);
    if (heading) {
      list = null;
      const h = document.createElement(heading[1].length <= 2 ? "h3" : "h4");
      inline(h, heading[2]);
      container.append(h);
      blocks++;
    } else if (item) {
      if (!list) {
        list = document.createElement("ul");
        container.append(list);
      }
      const li = document.createElement("li");
      inline(li, item[1]);
      list.append(li);
      blocks++;
    } else {
      list = null;
      const p = document.createElement("p");
      inline(p, line);
      container.append(p);
      blocks++;
    }
  }
  // A cut-off excerpt must not end on a heading whose text was left out.
  while (blocks >= maxBlocks && /^H[34]$/.test(container.lastElementChild?.tagName ?? "")) {
    container.lastElementChild.remove();
  }
  return blocks;
}
