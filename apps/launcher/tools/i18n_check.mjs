import { readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const folder = join(dirname(fileURLToPath(import.meta.url)), "..", "ui", "js", "i18n");
const load = async (code) => (await import(pathToFileURL(join(folder, `${code}.js`)).href)).default;

function flatten(node, prefix = "", out = new Map()) {
  for (const [key, value] of Object.entries(node)) {
    const path = prefix ? `${prefix}.${key}` : key;
    if (value && typeof value === "object") flatten(value, path, out);
    else out.set(path, value);
  }
  return out;
}

const placeholders = (text) => [...String(text).matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort().join(",");

const reference = flatten(await load("en"));
const codes = process.argv.length > 2
  ? process.argv.slice(2)
  : readdirSync(folder).filter((f) => f.endsWith(".js") && f !== "en.js").map((f) => f.slice(0, -3)).sort();
let failed = false;
for (const code of codes) {
  const texts = flatten(await load(code));
  const missing = [...reference.keys()].filter((k) => !texts.has(k));
  const extra = [...texts.keys()].filter((k) => !reference.has(k));
  const wrong = [...texts].filter(([k, v]) => reference.has(k) && placeholders(v) !== placeholders(reference.get(k)));
  console.log(`${code}: ${texts.size - extra.length}/${reference.size} keys, ${missing.length} missing, ${extra.length} extra, ${wrong.length} placeholder mismatches`);
  for (const k of extra) console.log(`  extra: ${k}`);
  for (const [k] of wrong) console.log(`  placeholders differ: ${k} (en: {${placeholders(reference.get(k))}})`);
  if (extra.length || wrong.length) failed = true;
}
process.exit(failed ? 1 : 0);
