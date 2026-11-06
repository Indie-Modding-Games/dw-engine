#!/usr/bin/env python3
"""The release workflows' decisions (.github/workflows/release.yml and promote.yml).

  version [--count N]       the version of a build of HEAD: <CMakeLists.txt's version>-beta.<N>,
                            N the commits on HEAD (main only grows, one commit per merged pull
                            request, so every build of main has a higher N)
  newer <a> <b>             succeeds when version a is above version b (semver order)
  launcher-check            the launcher's version against the newest release's: never lower, and
                            higher when the launcher changed since that release
  notes                     the release notes in Spanish, from GitHub's generated notes

docs/modulos/apps/launcher.md explains the versions and the channels.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?$")
PROJECT_VERSION = re.compile(r"project\(\s*dwe\s+VERSION\s+(\d+\.\d+\.\d+)", re.IGNORECASE)

# GitHub's generated notes (releases/generate-notes) in English, and their Spanish.
NOTES_WORDS = [
    ("## What's Changed", "## Cambios"),
    ("## New Contributors", "## Primeras contribuciones"),
    (" made their first contribution in ", " colabora por primera vez en "),
    ("**Full Changelog**", "**Todos los cambios**"),
]
NOTES_LINE = re.compile(r"^\* (.*) by (@[\w-]+(?:\[bot\])?) in (\S+)$")


def version_key(text: str) -> tuple:
    """Semver precedence: a prerelease ranks below its release; its numeric identifiers compare
    as numbers and rank below text ones."""
    match = SEMVER.match(text)
    if not match:
        raise ValueError(f"not a version: {text}")
    core = tuple(int(n) for n in match.group(1, 2, 3))
    pre = match.group(4)
    if pre is None:
        return core + ((1,),)
    parts = tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in pre.split("."))
    return core + ((0, parts),)


def is_newer(a: str, b: str) -> bool:
    return version_key(a) > version_key(b)


def build_version(cmake_lists: str, count: int) -> str:
    match = PROJECT_VERSION.search(cmake_lists)
    if not match:
        raise ValueError("CMakeLists.txt has no project(dwe VERSION x.y.z)")
    return f"{match.group(1)}-beta.{count}"


def launcher_problem(version: str, previous: str, changed: bool, tag: str) -> str | None:
    """Why the launcher's version cannot ship, or None. Installed launchers update only to a
    higher version than theirs (tauri's updater compares latest.json's version)."""
    if version_key(version) < version_key(previous):
        return f"The launcher is {version}, lower than {previous} in {tag}: no launcher would update"
    if changed and version == previous:
        return (f"The launcher changed since {tag} but is still {version}: installed launchers would "
                "not update. Raise the version in tauri.conf.json, Cargo.toml and Cargo.lock")
    return None


def spanish_notes(kind: str, version: str, commit: str, generated: str, tag: str = "") -> str:
    if kind == "testing":
        header = (f"Versión de prueba **{version}** (commit `{commit[:8]}`). Llega a quien tiene el "
                  "canal «Pruebas» en el launcher.")
    else:
        header = (f"Versión estable **{version}**: la versión de prueba {tag}, ya probada, con los "
                  "mismos archivos. Llega a los dos canales del launcher.")
    lines = []
    for line in generated.strip().splitlines():
        for english, spanish in NOTES_WORDS:
            line = line.replace(english, spanish)
        match = NOTES_LINE.match(line)
        if match:
            line = f"* {match.group(1)} ({match.group(2)}, {match.group(3)})"
        lines.append(line)
    body = "\n".join(lines).strip()
    return f"{header}\n\n{body}\n" if body else f"{header}\n"


def git_count() -> int:
    out = subprocess.run(["git", "-C", str(ROOT), "rev-list", "--count", "HEAD"], check=True,
                         capture_output=True, text=True).stdout
    return int(out.strip())


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("version")
    version.add_argument("--count", type=int)
    newer = commands.add_parser("newer")
    newer.add_argument("a")
    newer.add_argument("b")
    launcher = commands.add_parser("launcher-check")
    launcher.add_argument("--version", required=True)
    launcher.add_argument("--previous", required=True)
    launcher.add_argument("--tag", required=True)
    launcher.add_argument("--changed", choices=["0", "1"], required=True)
    notes = commands.add_parser("notes")
    notes.add_argument("--kind", choices=["testing", "stable"], required=True)
    notes.add_argument("--version", required=True)
    notes.add_argument("--commit", required=True)
    notes.add_argument("--tag", default="")
    notes.add_argument("--generated", type=Path, required=True)
    args = parser.parse_args(argv)

    if args.command == "version":
        count = args.count if args.count is not None else git_count()
        print(build_version((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"), count))
        return 0
    if args.command == "newer":
        return 0 if is_newer(args.a, args.b) else 1
    if args.command == "launcher-check":
        problem = launcher_problem(args.version, args.previous, args.changed == "1", args.tag)
        if problem:
            print(problem, file=sys.stderr)
            return 1
        state = "changed" if args.changed == "1" else "unchanged"
        print(f"The launcher is {args.version} ({args.tag} had {args.previous}; {state} since)")
        return 0
    generated = args.generated.read_text(encoding="utf-8")
    sys.stdout.write(spanish_notes(args.kind, args.version, args.commit, generated, args.tag))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
