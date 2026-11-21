import de from "./i18n/de.js";
import en from "./i18n/en.js";
import es from "./i18n/es.js";
import fr from "./i18n/fr.js";
import ja from "./i18n/ja.js";
import ko from "./i18n/ko.js";
import pt from "./i18n/pt.js";
import ru from "./i18n/ru.js";
import zh from "./i18n/zh.js";

export const LANGUAGES = [
  { code: "en", name: "English", html: "en" },
  { code: "es", name: "Español", html: "es" },
  { code: "pt", name: "Português (Brasil)", html: "pt-BR" },
  { code: "fr", name: "Français", html: "fr" },
  { code: "de", name: "Deutsch", html: "de" },
  { code: "ru", name: "Русский", html: "ru" },
  { code: "zh", name: "简体中文", html: "zh-Hans" },
  { code: "ko", name: "한국어", html: "ko" },
  { code: "ja", name: "日本語", html: "ja" },
];

const FALLBACK = "en";
const STRINGS = { en, es, pt, fr, de, ru, zh, ko, ja };

let language = FALLBACK;

export function setLanguage(lang) {
  language = STRINGS[lang] ? lang : FALLBACK;
  document.documentElement.lang = LANGUAGES.find((l) => l.code === language).html;
}

export function getLanguage() {
  return language;
}

export function pickLanguage(tags) {
  for (const tag of tags ?? []) {
    const code = String(tag).toLowerCase().split(/[-_]/)[0];
    if (STRINGS[code]) return code;
  }
  return FALLBACK;
}

function lookup(table, path) {
  let node = table;
  for (const key of path.split(".")) node = node?.[key];
  return typeof node === "string" ? node : null;
}

// t("s.discBody", { path: "x" }): the text at that path, with {path} filled in.
export function t(path, values = {}) {
  const text = lookup(STRINGS[language], path) ?? lookup(STRINGS[FALLBACK], path);
  if (text === null) return path;
  return text.replace(/\{(\w+)\}/g, (_, key) => (key in values ? String(values[key]) : ""));
}

// An invalid add-on's detail is "<field>:<reason>": the field as written in addon.json, the
// reason in words.
function addonDetail(detail) {
  const at = detail.lastIndexOf(":");
  if (at < 0) return detail;
  const field = detail.slice(0, at);
  const reason = t(`addonReason.${detail.slice(at + 1)}`);
  return `«${field}» ${reason.startsWith("addonReason.") ? detail.slice(at + 1) : reason}`;
}

export function failureText(failure) {
  const code = failure?.code ?? "io";
  let detail = failure?.detail ?? String(failure ?? "");
  if (code === "addon-invalid") detail = addonDetail(detail);
  const text = t(`failure.${code}`, { detail });
  return text === `failure.${code}` ? `${code}: ${detail}` : text.trim();
}

export function formatBytes(bytes) {
  const mb = bytes / (1024 * 1024);
  if (mb >= 1024) {
    return `${(mb / 1024).toLocaleString(language, { maximumFractionDigits: 1, minimumFractionDigits: 1 })} GB`;
  }
  const digits = mb >= 100 ? 0 : 1;
  return `${mb.toLocaleString(language, { maximumFractionDigits: digits, minimumFractionDigits: digits })} MB`;
}
