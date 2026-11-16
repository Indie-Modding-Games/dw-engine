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

// The site in two languages: English at the root, Spanish under es/ (the same file names in both).
const locales = {
  root: {
    dir: "",
    label: "English",
    lang: "en",
    text: {
      home: "Home",
      start: "Get started",
      architecture: "Architecture",
      adr: "Decisions (ADR)",
      modules: "Modules",
      engine: "Engine (packages/engine/)",
      game: "Game (packages/game/)",
      shared: "Shared libraries (packages/)",
      apps: "Applications (apps/)",
      other: "Other",
      formats: "Formats and files",
      guides: "Guides",
      reference: "Original game reference",
      modding: "Modding",
      conventions: "Conventions",
      legal: "Legal",
      plan: "Plan",
      description: "Technical documentation of dwe, the native reimplementation of a classic raising RPG",
      outline: "On this page",
      editLink: "Edit this page",
      previous: "Previous",
      next: "Next",
      footer:
        "No assets, code or data from the original game. Not affiliated with or endorsed by its rights holders.",
      theme: "Appearance",
      menu: "Menu",
      backToTop: "Return to top",
      language: "Language",
    },
  },
  es: {
    dir: "es/",
    label: "Español",
    lang: "es",
    text: {
      home: "Inicio",
      start: "Empezar",
      architecture: "Arquitectura",
      adr: "Decisiones (ADR)",
      modules: "Módulos",
      engine: "Motor (packages/engine/)",
      game: "Juego (packages/game/)",
      shared: "Librerías compartidas (packages/)",
      apps: "Aplicaciones (apps/)",
      other: "Otros",
      formats: "Formatos y archivos",
      guides: "Guías",
      reference: "Referencia del original",
      modding: "Modding",
      conventions: "Convenciones",
      legal: "Legal",
      plan: "Plan",
      description: "Documentación técnica de dwe, la reimplementación nativa de un clásico RPG de crianza",
      outline: "En esta página",
      editLink: "Editar esta página",
      previous: "Anterior",
      next: "Siguiente",
      footer:
        "Sin assets, código ni datos del juego original. No afiliado con los titulares de su licencia ni respaldado por ellos.",
      theme: "Tema",
      menu: "Menú",
      backToTop: "Volver arriba",
      language: "Idioma",
    },
  },
};

// The pages of one language: `file` is relative to the language's folder.
function pagesOf(locale) {
  // The text of a page in the sidebar: its first level-one heading, without inline code marks. The
  // home page has none: its layout draws the title.
  function pageTitle(file) {
    if (file === indexPage) {
      return locale.text.home;
    }
    const heading = readFileSync(join(docsRoot, locale.dir, file), "utf8").match(/^# (.+)$/m);
    return heading ? heading[1].replaceAll("`", "") : file;
  }

  function pageLink(file) {
    return "/" + locale.dir + file.replace(/(^|\/)index\.md$/, "$1").replace(/\.md$/, "");
  }

  function pageItem(file) {
    return { text: pageTitle(file), link: pageLink(file) };
  }

  // The pages of a folder: its index first, then the rest by file name, leaving out the given names.
  function folderItems(folder, { exclude = [] } = {}) {
    const files = readdirSync(join(docsRoot, locale.dir, folder))
      .filter((name) => name.endsWith(".md") && name !== indexPage && !exclude.includes(name))
      .sort()
      .map((name) => `${folder}/${name}`);
    const index = `${folder}/${indexPage}`;
    return (existsSync(join(docsRoot, locale.dir, index)) ? [index, ...files] : files).map(pageItem);
  }

  return { pageItem, folderItems, pageLink };
}

function group(text, items, collapsed = true) {
  return { text, collapsed, items };
}

function sidebarOf(locale) {
  const { pageItem, folderItems } = pagesOf(locale);
  const text = locale.text;
  return [
    group(
      text.start,
      ["index.md", "getting-started/setup.md", "contributing.md", "glossary.md"].map(pageItem),
      false,
    ),
    group(text.architecture, [
      pageItem("architecture/overview.md"),
      ...folderItems("architecture", { exclude: ["overview.md"] }),
    ]),
    group(text.adr, folderItems("adr", { exclude: ["template.md"] })),
    group(text.modules, [
      pageItem("modules/index.md"),
      group(text.engine, folderItems("modules/engine")),
      group(text.game, folderItems("modules/game")),
      group(text.shared, [
        ...["session", "screens", "content"].map((name) => pageItem(`modules/apps/${name}.md`)),
        pageItem("modules/formats.md"),
      ]),
      group(text.apps, [
        ...folderItems("modules/apps", { exclude: ["session.md", "screens.md", "content.md"] }),
        ...folderItems("modules/services"),
      ]),
      group(text.other, [pageItem("modules/shaders.md"), ...folderItems("modules/tests")]),
    ]),
    group(text.formats, folderItems("formats")),
    group(text.guides, folderItems("guides")),
    group(text.reference, folderItems("reference")),
    group(text.modding, folderItems("modding")),
    group(text.conventions, folderItems("conventions")),
    group(text.legal, folderItems("legal")),
    group(text.plan, [pageItem("roadmap.md"), ...folderItems("plan")]),
  ];
}

function themeOf(locale) {
  const text = locale.text;
  const link = (path) => "/" + locale.dir + path;
  return {
    nav: [
      { text: text.start, link: link("getting-started/setup") },
      { text: text.architecture, link: link("architecture/overview") },
      { text: text.modules, link: link("modules/") },
      { text: text.guides, link: link("guides/vertical-slice") },
      { text: text.reference, link: link("reference/") },
      { text: text.plan, link: link("roadmap") },
    ],
    sidebar: sidebarOf(locale),
    outline: { level: [2, 3], label: text.outline },
    editLink: { pattern: `${repositoryUrl}/edit/main/docs/:path`, text: text.editLink },
    docFooter: { prev: text.previous, next: text.next },
    footer: { message: text.footer },
    darkModeSwitchLabel: text.theme,
    sidebarMenuLabel: text.menu,
    returnToTopLabel: text.backToTop,
    langMenuLabel: text.language,
  };
}

function localeConfig(locale) {
  return {
    label: locale.label,
    lang: locale.lang,
    link: "/" + locale.dir,
    description: locale.text.description,
    themeConfig: themeOf(locale),
  };
}

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
  base: sitePath,
  title: "dwe",
  srcExclude: ["adr/template.md", "es/adr/template.md"],
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
  locales: {
    root: localeConfig(locales.root),
    es: localeConfig(locales.es),
  },
  themeConfig: {
    search: { provider: "local" },
    socialLinks: [{ icon: "github", link: repositoryUrl }],
  },
};
