#!/usr/bin/env python3
"""The repository's packages and apps, their dependencies and what a change affects (scripts/dev.sh --filter and
--affected).

A package is a folder of packages/ or apps/ whose CMakeLists.txt declares a module or a program (dwe_add_module,
dwe_add_app); its name is the folder's. Its dependencies are the modules its CMakeLists.txt names (DEPS, dwe::<name>)
and the packages whose folders it names. The apps without CMake (the launcher, the studio, the beta gateway) are
packages without dependencies, and repo stands for the repository's own tests (the scripts').

    packages.py list                       name, folder and dependencies of every package
    packages.py select <spec>...           the packages the specs name, one per line
    packages.py affected [--base <ref>]    the packages changed since the merge base with <ref> (origin/main), in the
                                           working tree too, and every package that depends on them
    --kinds (select, affected)             each name followed by a tab and its kind: cmake, other or repo

A spec is a name (core), a name with its dependencies (game...), a name with its dependents (...core) or a folder
pattern (packages/game/*).
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

PACKAGE_ROOTS = ("packages", "apps")
REPO = "repo"
DECLARATION = re.compile(r"\b(dwe_add_module|dwe_add_app)\s*\(([^)]*)\)", re.S)
KEYWORDS = {"LAYER", "SOURCES", "DEPS", "PUBLIC_LIBS", "PRIVATE_LIBS", "TEST_LIBS", "OUTPUT_NAME"}
MODULE_REFERENCE = re.compile(r"\bdwe::(\w+)")
COMMENT = re.compile(r"#[^\n]*")
FOLDER_REFERENCE = re.compile(r"\b((?:packages|apps)/[\w./-]+)")
# A change here may affect every package: the build, the toolchain, the vendored libraries, the shared tests and the
# content they read.
GLOBAL_INPUTS = ("CMakeLists.txt", "CMakePresets.json", "vcpkg.json", ".devcontainer/", "tooling/", "packages/vendor/",
                 "tests/", "content/", "packages/CMakeLists.txt", "packages/engine/CMakeLists.txt",
                 "packages/game/CMakeLists.txt", "apps/CMakeLists.txt")
DOCUMENTATION = ("docs/",)


@dataclass
class Package:
    name: str
    folder: str
    cmake: bool
    deps: set[str] = field(default_factory=set)


def declared_deps(text: str) -> set[str]:
    """The modules a CMakeLists.txt names: the DEPS of its declarations and every dwe::<name>."""
    deps: set[str] = set(MODULE_REFERENCE.findall(text))
    for _, arguments in DECLARATION.findall(text):
        in_deps = False
        for token in arguments.split():
            if token in KEYWORDS:
                in_deps = token == "DEPS"
            elif in_deps:
                deps.add(token)
    return deps


def find_packages(root: Path) -> dict[str, Package]:
    packages: dict[str, Package] = {}
    texts: dict[str, str] = {}
    for top in PACKAGE_ROOTS:
        for build_file in sorted((root / top).glob("**/CMakeLists.txt")):
            text = COMMENT.sub("", build_file.read_text(encoding="utf-8"))
            if DECLARATION.search(text):
                folder = build_file.parent.relative_to(root).as_posix()
                packages[build_file.parent.name] = Package(build_file.parent.name, folder, True)
                texts[build_file.parent.name] = text
        for app in sorted((root / top).iterdir()) if top == "apps" else ():
            if app.is_dir() and not (app / "CMakeLists.txt").exists():
                packages[app.name] = Package(app.name, app.relative_to(root).as_posix(), False)
    by_folder = {package.folder: name for name, package in packages.items()}
    for name, text in texts.items():
        deps = {dep for dep in declared_deps(text) if dep in packages}
        for reference in FOLDER_REFERENCE.findall(text):
            owner = owner_of(reference, by_folder)
            if owner:
                deps.add(owner)
        packages[name].deps = deps - {name}
    packages[REPO] = Package(REPO, ".", False)
    return packages


def owner_of(path: str, by_folder: dict[str, str]) -> str | None:
    """The package whose folder holds path, or None."""
    for folder, name in by_folder.items():
        if path == folder or path.startswith(folder + "/"):
            return name
    return None


def dependents(packages: dict[str, Package], names: set[str]) -> set[str]:
    found = set(names)
    while True:
        more = {name for name, package in packages.items() if package.deps & found} - found
        if not more:
            return found
        found |= more


def dependencies(packages: dict[str, Package], names: set[str]) -> set[str]:
    found = set(names)
    while True:
        more = set().union(*(packages[name].deps for name in found)) - found
        if not more:
            return found
        found |= more


def select(packages: dict[str, Package], specs: list[str]) -> set[str]:
    chosen: set[str] = set()
    for spec in specs:
        if "/" in spec or "*" in spec:
            pattern = spec.removeprefix("./").rstrip("/")
            matched = {name for name, package in packages.items()
                       if fnmatch.fnmatch(package.folder, pattern) or package.folder.startswith(pattern + "/")}
            if not matched:
                raise SystemExit(f"packages.py: no package in {spec}")
            chosen |= matched
            continue
        name = spec.removeprefix("...").removesuffix("...")
        if name not in packages:
            raise SystemExit(f"packages.py: unknown package {name}; scripts/dev.sh packages lists them")
        if spec.startswith("..."):
            chosen |= dependents(packages, {name})
        elif spec.endswith("..."):
            chosen |= dependencies(packages, {name})
        else:
            chosen.add(name)
    return chosen


def affected_by(packages: dict[str, Package], changed: list[str]) -> set[str]:
    by_folder = {package.folder: name for name, package in packages.items() if name != REPO}
    touched: set[str] = set()
    for path in changed:
        if path.startswith(DOCUMENTATION) or path.endswith(".md"):
            continue
        if path in GLOBAL_INPUTS or path.startswith(tuple(p for p in GLOBAL_INPUTS if p.endswith("/"))):
            return set(packages)
        owner = owner_of(path, by_folder)
        touched.add(owner or REPO)
    return dependents(packages, touched)


def changed_files(root: Path, base: str) -> list[str]:
    def git(*args: str) -> list[str]:
        result = subprocess.run(["git", "-C", str(root), "-c", "core.quotePath=false", *args],
                                capture_output=True, text=True, check=True)
        return [line for line in result.stdout.splitlines() if line]

    merge_base = git("merge-base", base, "HEAD")[0]
    return sorted(set(git("diff", "--name-only", merge_base)) | set(git("ls-files", "--others", "--exclude-standard")))


def kind_of(package: Package) -> str:
    return REPO if package.name == REPO else "cmake" if package.cmake else "other"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list")
    select_parser = commands.add_parser("select")
    select_parser.add_argument("specs", nargs="+")
    affected_parser = commands.add_parser("affected")
    affected_parser.add_argument("--base", default="origin/main")
    for subparser in (select_parser, affected_parser):
        subparser.add_argument("--kinds", action="store_true")
    args = parser.parse_args(argv)

    packages = find_packages(args.root)
    if args.command == "list":
        for name in sorted(packages):
            package = packages[name]
            print(f"{name}\t{package.folder}\t{kind_of(package)}\t{' '.join(sorted(package.deps))}")
        return 0
    if args.command == "select":
        names = select(packages, args.specs)
    else:
        names = affected_by(packages, changed_files(args.root, args.base))
    for name in sorted(names):
        print(f"{name}\t{kind_of(packages[name])}" if args.kinds else name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
