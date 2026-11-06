// The documentation site (scripts/dev.sh docs and docs-dev). It imports no package, only Node's own modules, so it
// loads from any folder; VitePress and Mermaid live in the development image.
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const docsRoot = dirname(dirname(fileURLToPath(import.meta.url)));
const repositoryUrl = "https://github.com/Indie-Modding-Games/dw-engine";
const indexPage = "index.md";
// The path the site is served under: "/" locally; the GitHub Pages workflow sets the repository's.
const sitePath = process.env.DWE_DOCS_BASE || "/";

const homeTitle = "Inicio";

// The text of a page in the sidebar: its first level-one heading, without inline code marks. The
// home page has none: its layout draws the title.
function pageTitle(file) {
  if (file === indexPage) {
    return homeTitle;
  }
  const heading = readFileSync(join(docsRoot, file), "utf8").match(/^# (.+)$/m);
  return heading ? heading[1].replaceAll("`", "") : file;
}

function pageLink(file) {
  return "/" + file.replace(/(^|\/)index\.md$/, "$1").replace(/\.md$/, "");
}

function pageItem(file) {
  return { text: pageTitle(file), link: pageLink(file) };
}

// The pages of a folder: its index first, then the rest by file name, leaving out the given names.
function folderItems(folder, { exclude = [] } = {}) {
  const files = readdirSync(join(docsRoot, folder))
    .filter((name) => name.endsWith(".md") && name !== indexPage && !exclude.includes(name))
    .sort()
    .map((name) => `${folder}/${name}`);
  const index = `${folder}/${indexPage}`;
  return (existsSync(join(docsRoot, index)) ? [index, ...files] : files).map(pageItem);
}

function group(text, items, collapsed = true) {
  return { text, collapsed, items };
}

const sidebar = [
  group(
    "Empezar",
    ["index.md", "getting-started/setup.md", "contribuir.md", "glossary.md"].map(pageItem),
    false,
  ),
  group("Arquitectura", [
    pageItem("architecture/overview.md"),
    ...folderItems("architecture", { exclude: ["overview.md"] }),
  ]),
  group("Decisiones (ADR)", folderItems("adr", { exclude: ["template.md"] })),
  group("Módulos", [
    pageItem("modulos/index.md"),
    group("Motor (engine/)", folderItems("modulos/engine")),
    group("Juego (game/)", folderItems("modulos/game")),
    group("Aplicaciones (apps/)", folderItems("modulos/apps")),
    group("Servicios (services/)", folderItems("modulos/services")),
    group("Otros", [
      pageItem("modulos/formats.md"),
      pageItem("modulos/shaders.md"),
      ...folderItems("modulos/tests"),
    ]),
  ]),
  group("Formatos y archivos", folderItems("formats")),
  group("Guías", folderItems("guides")),
  group("Referencia del original", folderItems("reference")),
  group("Modding", folderItems("modding")),
  group("Convenciones", folderItems("conventions")),
  group("Legal", folderItems("legal")),
  group("Plan", [pageItem("roadmap.md"), ...folderItems("plan")]),
];

// Anchors as GitHub makes them (accents kept, punctuation dropped), so the links between pages work the same in the
// repository and on the site.
function githubSlug(text) {
  return text
    .trim()
    .toLowerCase()
    .replace(/[^\p{L}\p{N}\p{M}\s_-]/gu, "")
    .replace(/\s/g, "-");
}

function escapeHtml(text) {
  return text.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;");
}

// Inline code is shown as written: v-pre keeps Vue from reading {{ }} in it as an expression.
function literalInlineCode(md) {
  const codeInline = md.renderer.rules.code_inline;
  md.renderer.rules.code_inline = (tokens, index, options, env, self) => {
    tokens[index].attrSet("v-pre", "");
    return codeInline(tokens, index, options, env, self);
  };
}

// Mermaid blocks reach the page as their source, which the theme draws in the browser; v-pre keeps Vue off it.
function mermaidFences(md) {
  const fence = md.renderer.rules.fence;
  md.renderer.rules.fence = (tokens, index, options, env, self) => {
    const token = tokens[index];
    if (token.info.trim() === "mermaid") {
      return `<pre class="mermaid" v-pre>${escapeHtml(token.content)}</pre>`;
    }
    return fence(tokens, index, options, env, self);
  };
}

export default {
  lang: "es",
  base: sitePath,
  title: "dwe",
  description: "Documentación técnica de dwe, la reimplementación nativa de un clásico RPG de crianza",
  srcExclude: ["adr/template.md"],
  outDir: "../build/docs-site",
  cacheDir: "../build/docs-cache",
  cleanUrls: true,
  // Mermaid is one large chunk, loaded only by the pages with a diagram.
  vite: { build: { chunkSizeWarningLimit: 4096 } },
  markdown: {
    anchor: { slugify: githubSlug },
    config(md) {
      literalInlineCode(md);
      mermaidFences(md);
    },
  },
  themeConfig: {
    nav: [
      { text: "Empezar", link: "/getting-started/setup" },
      { text: "Arquitectura", link: "/architecture/overview" },
      { text: "Módulos", link: "/modulos/" },
      { text: "Guías", link: "/guides/prueba-vertical" },
      { text: "Referencia", link: "/reference/" },
      { text: "Plan", link: "/roadmap" },
    ],
    sidebar,
    outline: { level: [2, 3], label: "En esta página" },
    search: { provider: "local" },
    socialLinks: [{ icon: "github", link: repositoryUrl }],
    editLink: { pattern: `${repositoryUrl}/edit/main/docs/:path`, text: "Editar esta página" },
    docFooter: { prev: "Anterior", next: "Siguiente" },
    footer: {
      message:
        "Sin assets, código ni datos del juego original. No afiliado con los titulares de su licencia ni respaldado por ellos.",
    },
    darkModeSwitchLabel: "Tema",
    sidebarMenuLabel: "Menú",
    returnToTopLabel: "Volver arriba",
    langMenuLabel: "Idioma",
  },
};
