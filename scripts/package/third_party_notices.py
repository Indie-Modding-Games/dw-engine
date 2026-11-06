#!/usr/bin/env python3
"""THIRD_PARTY_NOTICES.txt: the credits, the Apache-2.0 NOTICE files and the licence texts of the
software a package is built with, next to the project's own LICENSE.

Usage:
    third_party_notices.py game --licenses <folder> --out <THIRD_PARTY_NOTICES.txt>
    third_party_notices.py launcher --manifest-path <Cargo.toml> [--target <triple>]...
                                    [--extra <name>=<file>]... --out <THIRD_PARTY_NOTICES.txt>
    third_party_notices.py check <package folder>

game: from the licence files a packager gathers (scripts/package_*.sh: vcpkg's
share/<port>/copyright of each port, the font's, the C++ library's), one per component, named
<component>.txt; files in subfolders (licenses/lavapipe/) are included too. Every component of the
top folder under the Apache License must be in APACHE_NOTICES, which says where its NOTICE file is
or that upstream has none: a new Apache-2.0 dependency fails until someone looks.
launcher: from `cargo metadata` (the crates the launcher is built from, normal dependencies of
the given targets), each crate's own LICENSE*, COPYING*, NOTICE* and COPYRIGHT* files; a crate
that ships none gets its licence expression and the standard text. --extra adds a file (the
font's licence).
check: the package folder has LICENSE and a THIRD_PARTY_NOTICES.txt this script wrote.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HEADER = "Third-party notices"
STANDARD = Path(__file__).resolve().parent / "licenses"
APACHE = re.compile(r"Apache([- ]2\.0| License)")

# The NOTICE file of each Apache-2.0 component of the game (section 4(d) of the licence asks to
# pass it on when there is one), checked against each project's repository.
APACHE_NOTICES = {
    "egl-registry": "No NOTICE file upstream.",
    "ktx": "Its NOTICE.md is the same file as its LICENSE.md, included below.",
    "llvm-libcxx": "No NOTICE file upstream (Apache-2.0 WITH LLVM-exception).",
    "moltenvk": "No NOTICE file upstream.",
    "sdl3": "No NOTICE file upstream (Apache-2.0 covers one OpenGL ES header only).",
    "shader-slang": "No NOTICE file upstream. A build tool only: it compiles the shaders, and no part of it ships.",
    "vulkan-headers": "No NOTICE file upstream.",
    "vulkan-loader": "No NOTICE file upstream.",
}

# The FreeType License asks for this credit in the documentation of a program that uses it.
def freetype_credit() -> str:
    year = time.gmtime(int(os.environ.get("SOURCE_DATE_EPOCH", time.time()))).tm_year
    return (f"Portions of this software are copyright © 1996-{year} The FreeType Project "
            "(https://freetype.org). All rights reserved.")


def normalized(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def rule(char: str = "=") -> str:
    return char * 100


def render(intro: str, credits: list[str], notices: list[tuple[str, str]],
           texts: list[tuple[list[str], str]]) -> str:
    """The file: intro, credits, NOTICE section, the component list and the texts; components
    with the same text (up to whitespace) share one copy."""
    out = [HEADER, rule(), "", intro, ""]
    if credits:
        out += ["Credits", rule("-"), *credits, ""]
    if notices:
        out += ["Apache License 2.0 NOTICE files", rule("-")]
        for name, notice in notices:
            out += [f"{name}:", notice.rstrip(), ""]
    grouped: dict[str, tuple[list[str], str]] = {}
    for names, text in texts:
        key = normalized(text)
        if key in grouped:
            grouped[key][0].extend(names)
        else:
            grouped[key] = (list(names), text)
    out += ["Components", rule("-")]
    out += [f"- {name}" for name in sorted(n for names, _ in grouped.values() for n in names)]
    out.append("")
    for names, text in sorted(grouped.values(), key=lambda item: sorted(item[0])[0]):
        out += [rule(), ", ".join(sorted(names)), rule(), "", text.rstrip(), ""]
    return "\n".join(out) + "\n"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def game(args) -> int:
    folder = Path(args.licenses)
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    if not files:
        print(f"third_party_notices: no licence files in {folder}", file=sys.stderr)
        return 1
    texts = []
    notices = []
    problems = []
    for path in files:
        name = path.relative_to(folder).with_suffix("").as_posix()
        text = read(path)
        texts.append(([name], text))
        if path.parent == folder and APACHE.search(text):
            if name in APACHE_NOTICES:
                notices.append((name, APACHE_NOTICES[name]))
            else:
                problems.append(f"{name} is under the Apache License: add its NOTICE file, or say it has "
                                "none, in APACHE_NOTICES (scripts/package/third_party_notices.py)")
    if problems:
        for problem in problems:
            print(f"third_party_notices: {problem}", file=sys.stderr)
        return 1
    if any(APACHE.search(text) for _, text in texts):
        texts.append((["Apache License 2.0 (the full text)"], read(STANDARD / "Apache-2.0.txt")))
    names = {path.stem for path in files if path.parent == folder}
    credits = [freetype_credit()] if "freetype" in names else []
    intro = ("dwe is under the MIT License (LICENSE). The game is built with the software below, under "
             "their own licences,\nwhose texts follow.")
    Path(args.out).write_text(render(intro, credits, notices, texts), encoding="utf-8")
    return 0


LICENCE_FILE = re.compile(r"^(licen[cs]e|copying|notice|copyright|unlicense)([-_.].*)?$", re.IGNORECASE)


def shipped_crates(metadata: dict) -> list[dict]:
    """The packages the workspace's members depend on, through normal dependencies only (build and
    dev dependencies never ship)."""
    packages = {p["id"]: p for p in metadata["packages"]}
    nodes = {n["id"]: n for n in metadata["resolve"]["nodes"]}
    members = set(metadata["workspace_members"])
    seen = set()
    todo = list(members)
    while todo:
        node = nodes.get(todo.pop())
        if node is None:
            continue
        for dep in node.get("deps", []):
            if any(kind.get("kind") is None for kind in dep.get("dep_kinds", [])) and dep["pkg"] not in seen:
                seen.add(dep["pkg"])
                todo.append(dep["pkg"])
    return sorted((packages[i] for i in seen - members), key=lambda p: (p["name"], p["version"]))


def crate_texts(package: dict) -> list[tuple[str, str]]:
    folder = Path(package["manifest_path"]).parent
    return [(p.name, read(p)) for p in sorted(folder.iterdir()) if p.is_file() and LICENCE_FILE.match(p.name)]


def launcher(args) -> int:
    command = ["cargo", "metadata", "--locked", "--format-version", "1", "--manifest-path", args.manifest_path]
    for target in args.target:
        command += ["--filter-platform", target]
    metadata = json.loads(subprocess.run(command, check=True, capture_output=True, text=True).stdout)
    texts = []
    notices = []
    missing = set()
    for package in shipped_crates(metadata):
        name = f"{package['name']} {package['version']}"
        found = crate_texts(package)
        for file, text in found:
            if file.lower().startswith("notice"):
                notices.append((name, text))
            else:
                texts.append(([name], text))
        if not any(not file.lower().startswith("notice") for file, _ in found):
            expression = package.get("license") or "see its repository"
            authors = ", ".join(package.get("authors") or []) or "its authors"
            texts.append(([name], f"Licence: {expression}. Copyright (c) {authors}.\n"
                          "The crate ships no licence file; the standard text of its licence follows "
                          "this list."))
            missing.update(spdx for spdx in ("MIT", "Apache-2.0") if spdx in expression)
    for spdx in sorted(missing):
        texts.append(([f"{spdx} (the standard text)"], read(STANDARD / f"{spdx}.txt")))
    for extra in args.extra:
        name, _, file = extra.partition("=")
        texts.append(([name], read(Path(file))))
    intro = ("dwe is under the MIT License (LICENSE). The launcher is built with the software below, "
             "under their own\nlicences, whose texts follow.")
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render(intro, [], notices, texts), encoding="utf-8")
    return 0


def check(args) -> int:
    folder = Path(args.folder)
    problems = []
    licence = folder / "LICENSE"
    if not licence.is_file() or "MIT License" not in read(licence):
        problems.append(f"{licence} is missing or is not the MIT License")
    notices = folder / "THIRD_PARTY_NOTICES.txt"
    if not notices.is_file() or not read(notices).startswith(HEADER) or "Components" not in read(notices):
        problems.append(f"{notices} is missing or was not written by third_party_notices.py")
    for problem in problems:
        print(f"third_party_notices: {problem}", file=sys.stderr)
    return 1 if problems else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    one = commands.add_parser("game")
    one.add_argument("--licenses", required=True)
    one.add_argument("--out", required=True)
    one.set_defaults(run=game)
    one = commands.add_parser("launcher")
    one.add_argument("--manifest-path", required=True)
    one.add_argument("--target", action="append", default=[])
    one.add_argument("--extra", action="append", default=[])
    one.add_argument("--out", required=True)
    one.set_defaults(run=launcher)
    one = commands.add_parser("check")
    one.add_argument("folder")
    one.set_defaults(run=check)
    args = parser.parse_args()
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
