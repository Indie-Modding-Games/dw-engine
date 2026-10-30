#!/usr/bin/env python3
"""Port coverage map: which functions of the decomp the engine ports, system by system.

It reads the decomp's function index (`function<TAB>src/<dir>/<file>.c:<line>`, generated from the decomp) and the
repository, and classifies each function:

- ported: the engine's code (game/, apps/, engine/, tests/, content/packs/) names it. When every citation says it is
  not done yet ("not drawn yet", "not ported", "TODO"...), it is partial instead. A citation by line alone
  (`partner.c:906`) is not used: the index lacks some functions (static ones), so a line often lands in the wrong
  one. What only the disc reader and the tools name (apps/dwx, formats/...) is missing: they name the original's
  functions to find its tables.
- doc-only: only docs/ names it.
- missing: nothing names it.

Then the config (docs/reference/port-coverage.toml) puts each function in a system and applies the manual calls:
rules that mark what the engine replaces (graphics, sound and disc plumbing: not needed), the copies of the battle
code in the tournament and versus overlays, and per-function entries with a status, a note, the engine's location
and a one-line description.

It writes the generated part of docs/reference/port-coverage.md, between its two markers, and leaves the hand-written
rest of the page as it is. --check exits 1 when the page is not up to date; --list prints one row per function.

Usage: scripts/port_coverage.py [--index FILE] [--config FILE] [--page FILE] [--check | --list]
"""

from __future__ import annotations

import argparse
import collections
import re
import subprocess
import sys
import tomllib
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INDEX = ROOT.parent / "decomp-index" / "functions.tsv"
DEFAULT_CONFIG = ROOT / "docs" / "reference" / "port-coverage.toml"
DEFAULT_PAGE = ROOT / "docs" / "reference" / "port-coverage.md"
BEGIN_MARK = "<!-- port-coverage:begin (scripts/port_coverage.py writes this part) -->"
END_MARK = "<!-- port-coverage:end -->"

CODE_DIRS = ("game", "apps", "engine", "formats", "tests", "content", "shaders")
# Code that reads the disc or edits content names the original's functions to find its tables: that is not a port.
TOOL_PREFIXES = ("apps/dwx/", "apps/studio/", "formats/", "content/schemas/", "content/enhance/", "shaders/")
DOC_DIRS = ("docs",)
TEXT_SUFFIXES = {
    ".cpp", ".hpp", ".h", ".c", ".ixx", ".cppm", ".inl", ".lua", ".yaml", ".yml", ".rml", ".rcss", ".json", ".md",
    ".txt", ".cmake", ".glsl", ".hlsl", ".slang", ".js", ".py", ".sh",
}
# The config's own page and this script name every function; they are not evidence.
SELF_FILES = {"docs/reference/port-coverage.md", "docs/reference/port-coverage.toml", "scripts/port_coverage.py"}

PORTED, PARTIAL, MISSING, NOT_NEEDED = "portado", "parcial", "falta", "no-aplica"
STATUSES = (PORTED, PARTIAL, MISSING, NOT_NEEDED)
STATUS_LABEL = {PORTED: "portado", PARTIAL: "parcial", MISSING: "falta", NOT_NEEDED: "no aplica"}

# A table of the disc reader that names an operation after the original's function that runs it
# (formats/src/effect/efe_subops.cpp): the engine runs the operation by that name, so a quoted name in the code
# counts as a citation of the function.
OP_HANDLER = re.compile(r'\.name\s*=\s*"([a-z0-9_]+)",\s*\.handler\s*=\s*"([A-Za-z_][A-Za-z0-9_]*)"')
QUOTED = re.compile(r'"([a-z0-9_]+)"')
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# A citation that says the thing is not there yet. Case matters for TODO: the Spanish "todo" is common in comments.
NEGATIVE = re.compile(
    r"not (?:yet )?(?:ported|drawn|implemented|handled|done|played|shown)|not \w+ yet|unported|\bstub|\bTODO\b|"
    r"FIXME|\bskip(?:s|ped)?\b|left out|\bignor(?:es|ed)\b|no se dibuja|se saltan|sin portar|esbozo|\bHueco\b",
)


@dataclass
class Function:
    name: str
    path: str  # src/<dir>/<file>.c
    line: int
    code_refs: list[tuple[str, int, str]] = field(default_factory=list)  # (file, line, text)
    tool_refs: list[tuple[str, int, str]] = field(default_factory=list)
    doc_refs: list[tuple[str, int]] = field(default_factory=list)
    system: str = ""
    status: str = MISSING
    auto: str = MISSING
    note: str = ""
    what: str = ""
    where: str = ""

    @property
    def location(self) -> str:
        return f"{self.path.removeprefix('src/')}:{self.line}"


def read_index(path: Path) -> list[Function]:
    functions = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        name, location = raw.split("\t")
        file, line = location.rsplit(":", 1)
        functions.append(Function(name=name, path=file, line=int(line)))
    return functions


def tracked_files(root: Path, dirs: tuple[str, ...]) -> list[str]:
    listed = subprocess.run(
        ["git", "ls-files", "--", *dirs], cwd=root, capture_output=True, text=True, check=True
    ).stdout.splitlines()
    return [f for f in listed if Path(f).suffix in TEXT_SUFFIXES and f not in SELF_FILES]


def scan(root: Path, functions: list[Function]) -> None:
    by_name: dict[str, list[Function]] = collections.defaultdict(list)
    for fn in functions:
        by_name[fn.name].append(fn)
    code_files = tracked_files(root, CODE_DIRS)
    handlers: dict[str, list[Function]] = collections.defaultdict(list)
    for file in code_files:
        if file.startswith(TOOL_PREFIXES):
            text = (root / file).read_text(encoding="utf-8", errors="ignore")
            for op, handler in OP_HANDLER.findall(text):
                handlers[op].extend(by_name.get(handler, ()))
    for file in code_files:
        try:
            text = (root / file).read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        tool = file.startswith(TOOL_PREFIXES)
        for number, line in enumerate(text.splitlines(), 1):
            named = set()
            for token in set(IDENT.findall(line)):
                for fn in by_name.get(token, ()):
                    (fn.tool_refs if tool else fn.code_refs).append((file, number, line.strip()))
                    named.add(fn.name)
            if tool:
                continue
            for op in set(QUOTED.findall(line)):
                for fn in handlers.get(op, ()):
                    if fn.name not in named:
                        fn.code_refs.append((file, number, line.strip()))
                        named.add(fn.name)
    for file in tracked_files(root, DOC_DIRS):
        text = (root / file).read_text(encoding="utf-8", errors="ignore")
        for number, line in enumerate(text.splitlines(), 1):
            for token in set(IDENT.findall(line)):
                for fn in by_name.get(token, ()):
                    fn.doc_refs.append((file, number))


def classify_auto(fn: Function) -> None:
    refs = fn.code_refs
    if refs:
        positive = [r for r in refs if not NEGATIVE.search(r[2])]
        if positive:
            fn.auto = PORTED
            fn.where = f"{positive[0][0]}:{positive[0][1]}"
        else:
            fn.auto = PARTIAL
            fn.where = f"{refs[0][0]}:{refs[0][1]}"
            fn.note = shorten(refs[0][2])
    elif fn.doc_refs:
        fn.auto = "doc"
    elif fn.tool_refs:
        fn.auto = "tool"
        fn.note = "solo lo cita la extracción del disco"
    fn.status = MISSING if fn.auto in ("doc", "tool") else fn.auto


def shorten(text: str, limit: int = 110) -> str:
    text = re.sub(r"^\s*(//+|#+|--+|\*+)\s*", "", text).replace("|", "\\|")
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def load_config(path: Path) -> dict:
    with path.open("rb") as stream:
        return tomllib.load(stream)


def apply_config(functions: list[Function], config: dict) -> list[str]:
    """Puts each function in its system and applies the manual calls. Returns the config's problems."""
    problems = []
    systems = config["system"]
    file_system = {}
    for system in systems:
        for file in system.get("files", ()):
            file_system[file] = system["id"]
    name_rules = [(re.compile(r["pattern"]), r["system"]) for r in config.get("system_by_name", ())]
    known = {fn.name for fn in functions}
    by_name = {fn.name: fn for fn in functions}
    for fn in functions:
        short = fn.path.removeprefix("src/")
        fn.system = file_system.get(short, "")
        for pattern, system in name_rules:
            if pattern.search(fn.name):
                fn.system = system
                break
        if not fn.system:
            problems.append(f"{fn.name} ({short}): no system")

    # Rules only touch what nothing in the code names: a function the engine cites stays ported.
    for rule in config.get("not_needed", ()):
        pattern = re.compile(rule["pattern"]) if "pattern" in rule else None
        files = set(rule.get("files", ()))
        for fn in functions:
            if fn.status == PORTED or fn.status == PARTIAL:
                continue
            short = fn.path.removeprefix("src/")
            if files and short not in files:
                continue
            if pattern and not pattern.search(fn.name):
                continue
            fn.status = NOT_NEEDED
            fn.note = rule["why"]

    for twins in config.get("twins", ()):
        for fn in functions:
            if fn.path.removeprefix("src/").split("/")[0] not in twins["dirs"]:
                continue
            twin = by_name.get(twins["target"] + re.sub(r"^[A-Z0-9]+_+", "", fn.name))
            if twin and fn.status not in (PORTED, PARTIAL):
                fn.status = NOT_NEEDED
                fn.note = f"copia de `{twin.name}`, que cuenta por las dos"

    for name, entry in config.get("functions", {}).items():
        if name not in known:
            problems.append(f"functions.{name}: not in the index")
            continue
        fn = by_name[name]
        status = entry.get("status")
        if status:
            if status not in STATUSES:
                problems.append(f"functions.{name}: unknown status {status}")
            else:
                fn.status = status
        if "note" in entry:
            fn.note = entry["note"]
        if "what" in entry:
            fn.what = entry["what"]
        if "dwe" in entry:
            fn.where = entry["dwe"]
        if "system" in entry:
            fn.system = entry["system"]
    return problems


def percent(part: int, whole: int) -> str:
    return f"{100 * part / whole:.0f} %" if whole else "—"


def render(functions: list[Function], config: dict) -> str:
    systems = config["system"]
    by_system: dict[str, list[Function]] = collections.defaultdict(list)
    for fn in functions:
        by_system[fn.system].append(fn)
    out = []
    totals = collections.Counter()
    out.append("## Resumen por sistema\n")
    out.append(
        "Cobertura = portado + medio parcial, sobre lo que hace falta portar (todo menos «no aplica»).\n"
    )
    out.append("| Sistema | Ficheros del decomp | Portado | Parcial | Falta | No aplica | Cobertura |")
    out.append("|---|---|---:|---:|---:|---:|---:|")
    for system in systems:
        fns = by_system.get(system["id"], [])
        count = collections.Counter(fn.status for fn in fns)
        totals.update(count)
        needed = count[PORTED] + count[PARTIAL] + count[MISSING]
        files = sorted({fn.path.removeprefix("src/") for fn in fns})
        dirs = sorted({f.split("/")[0] for f in files})
        shown = ", ".join(f"`{f}`" for f in files) if len(files) <= 4 else ", ".join(f"`{d}/`" for d in dirs) + (
            f" ({len(files)} ficheros)"
        )
        out.append(
            f"| [{system['name']}](#{anchor(system['name'])}) | {shown} | {count[PORTED]} | {count[PARTIAL]} | "
            f"{count[MISSING]} | {count[NOT_NEEDED]} | {percent(count[PORTED] + count[PARTIAL] / 2, needed)} |"
        )
    needed = totals[PORTED] + totals[PARTIAL] + totals[MISSING]
    out.append(
        f"| **Total** | {len(functions)} funciones | {totals[PORTED]} | {totals[PARTIAL]} | {totals[MISSING]} | "
        f"{totals[NOT_NEEDED]} | {percent(totals[PORTED] + totals[PARTIAL] / 2, needed)} |"
    )
    out.append("")
    for system in systems:
        fns = sorted(by_system.get(system["id"], []), key=lambda f: (f.path, f.line))
        count = collections.Counter(fn.status for fn in fns)
        out.append(f"## {system['name']}\n")
        if system.get("about"):
            out.append(system["about"] + "\n")
        files = sorted({fn.path.removeprefix("src/") for fn in fns})
        out.append("Ficheros del decomp: " + ", ".join(f"`{f}`" for f in files) + ".\n")
        out.append(
            f"Portado {count[PORTED]}, parcial {count[PARTIAL]}, falta {count[MISSING]}, "
            f"no aplica {count[NOT_NEEDED]}.\n"
        )
        gaps = [fn for fn in fns if fn.status in (PARTIAL, MISSING)]
        if gaps:
            out.append("| Función | Decomp | Estado | Qué hace | Motor / nota |")
            out.append("|---|---|---|---|---|")
            for fn in gaps:
                mark = " (citada en docs)" if fn.auto == "doc" else ""
                where = f"`{fn.where}`" if fn.where else ""
                note = " · ".join(x for x in (where, fn.note) if x)
                out.append(
                    f"| `{fn.name}` | `{fn.location}` | {STATUS_LABEL[fn.status]}{mark} | {fn.what or '—'} | "
                    f"{note or '—'} |"
                )
            out.append("")
        skipped = [fn for fn in fns if fn.status == NOT_NEEDED]
        if skipped:
            reasons = collections.Counter(
                "copias de otra función, que cuenta por las dos" if fn.note.startswith("copia de") else fn.note or "—"
                for fn in skipped
            )
            listed = "; ".join(f"{why} ({n})" for why, n in reasons.most_common())
            out.append(f"No aplica: {listed}.\n")
    return "\n".join(out).rstrip() + "\n"


def anchor(title: str) -> str:
    """The heading's id as the docs site (VitePress) makes it: accents off, punctuation and spaces to one dash."""
    text = unicodedata.normalize("NFKD", title)
    text = re.sub(r"[\u0300-\u036f]", "", text)
    text = re.sub(r"[\s~`!@#$%^&*()\-_+=\[\]{}|\\;:\"'“”‘’<>,.?/]+", "-", text)
    return re.sub(r"-{2,}", "-", text).strip("-").lower()


def write_page(page: Path, generated: str, check: bool) -> int:
    text = page.read_text(encoding="utf-8")
    start, end = text.find(BEGIN_MARK), text.find(END_MARK)
    if start < 0 or end < start:
        print(f"{page}: the markers are missing", file=sys.stderr)
        return 2
    updated = text[: start + len(BEGIN_MARK)] + "\n\n" + generated + "\n" + text[end:]
    if check:
        if updated != text:
            print(f"{page} is not up to date: run scripts/port_coverage.py", file=sys.stderr)
            return 1
        return 0
    page.write_text(updated, encoding="utf-8")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX, help="the decomp's functions.tsv")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--page", type=Path, default=DEFAULT_PAGE)
    parser.add_argument("--root", type=Path, default=ROOT, help="the repository to scan")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", help="exit 1 when the page is not up to date")
    action.add_argument("--list", action="store_true", help="print one row per function instead")
    args = parser.parse_args()

    if not args.index.is_file():
        print(f"{args.index}: no function index (pass --index)", file=sys.stderr)
        return 2
    functions = read_index(args.index)
    scan(args.root, functions)
    for fn in functions:
        classify_auto(fn)
    problems = apply_config(functions, load_config(args.config))
    for problem in problems:
        print(f"config: {problem}", file=sys.stderr)
    if args.list:
        for fn in sorted(functions, key=lambda f: (f.system, f.path, f.line)):
            refs = len(fn.code_refs)
            print("\t".join((fn.system, fn.status, fn.auto, fn.name, fn.location, str(refs), fn.where, fn.note)))
        return 0
    return write_page(args.page, render(functions, load_config(args.config)), args.check) or (1 if problems else 0)


if __name__ == "__main__":
    sys.exit(main())
