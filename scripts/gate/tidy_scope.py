"""Which translation units an incremental clang-tidy of the gate checks, and why.

The reference is the newest full clang-tidy that passed on a commit of main within a day, with the same image and
tool. Everything that changed since that commit is checked again: the changed sources, and every source that includes
a changed header, directly or through other headers. A change that can affect every source (a public header, the
clang-tidy configuration, the build or the toolchain, a configure template) makes the whole repository the scope.
"""

from __future__ import annotations

import json
import re
import shlex
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from pathlib import PurePosixPath

FULL_TIDY_MAX_AGE = timedelta(hours=24)
SOURCE_SUFFIXES = (".cpp",)
HEADER_SUFFIXES = (".hpp", ".h")
PUBLIC_INCLUDE_DIR = "include"
TIDY_CONFIG_FILE = ".clang-tidy"
BUILD_FILE = "CMakeLists.txt"
# Changes to these affect the flags or the tools of every translation unit; scripts/dev.sh runs clang-tidy.
TOOLCHAIN_DIRS = (".devcontainer", "cmake", "triplets")
TOOLCHAIN_ROOT_FILES = (BUILD_FILE, "CMakePresets.json", "vcpkg.json", "scripts/dev.sh")
# A template that CMake's configure_file turns into a generated header in the build folder: the include graph of the
# sources cannot see what includes the generated file, so a change to one checks everything.
CONFIGURE_TEMPLATE_SUFFIX = ".in"
INCLUDE_FLAGS = ("-I", "-isystem", "-iquote")
INCLUDE_LINE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)

REASON_CHANGED = "changed"
REASON_BUILD_FILE = "its module's build file changed"
SKIP_NOT_COMPILED = "not in the compile commands of the preset"
SKIP_NOT_CPP = "not a C++ source or header"
SKIP_UNCHANGED = "unchanged since the reference and includes no changed header"


@dataclass(frozen=True)
class FullTidyMarker:
    """A full clang-tidy that passed: on which commit, when, and with which image and tool."""

    commit: str
    passed_at: datetime
    image_id: str
    tool_version: str

    def to_json(self) -> str:
        return json.dumps(
            {
                "commit": self.commit,
                "passed_at": self.passed_at.isoformat(),
                "image_id": self.image_id,
                "tool_version": self.tool_version,
            },
            indent=2,
        )

    @staticmethod
    def from_json(text: str) -> FullTidyMarker:
        fields = json.loads(text)
        return FullTidyMarker(
            commit=fields["commit"],
            passed_at=datetime.fromisoformat(fields["passed_at"]),
            image_id=fields["image_id"],
            tool_version=fields["tool_version"],
        )


@dataclass(frozen=True)
class Reference:
    """The marker an incremental tidy starts from, or why there is none."""

    marker: FullTidyMarker | None
    reason: str


def choose_reference(
    markers: Iterable[FullTidyMarker],
    *,
    now: datetime,
    image_id: str,
    tool_version: str,
    is_on_main_below_tip: Callable[[str], bool],
) -> Reference:
    """The newest marker that is recent, of the same image and tool, and on main below the tip."""
    rejected: list[str] = []
    for marker in sorted(markers, key=lambda candidate: candidate.passed_at, reverse=True):
        short = marker.commit[:12]
        if now - marker.passed_at > FULL_TIDY_MAX_AGE:
            rejected.append(f"{short} is older than {FULL_TIDY_MAX_AGE}")
        elif marker.image_id != image_id:
            rejected.append(f"{short} used another image")
        elif marker.tool_version != tool_version:
            rejected.append(f"{short} used another clang-tidy")
        elif not is_on_main_below_tip(marker.commit):
            rejected.append(f"{short} is not on main below the tip")
        else:
            return Reference(marker, f"full clang-tidy passed on {short} at {marker.passed_at.isoformat()}")
    if not rejected:
        return Reference(None, "no full clang-tidy has passed yet")
    return Reference(None, "no usable full clang-tidy marker (" + "; ".join(rejected) + ")")


def is_cpp(path: str) -> bool:
    return path.endswith(SOURCE_SUFFIXES + HEADER_SUFFIXES)


def is_public_header(path: str) -> bool:
    return path.endswith(HEADER_SUFFIXES) and PUBLIC_INCLUDE_DIR in PurePosixPath(path).parts[:-1]


def full_tidy_trigger(changed: Iterable[str]) -> str | None:
    """Why a change needs a full clang-tidy, or None when an incremental one covers it."""
    for path in sorted(changed):
        parts = PurePosixPath(path).parts
        if is_public_header(path):
            return f"public header {path} changed"
        if parts[-1] == TIDY_CONFIG_FILE:
            return f"clang-tidy configuration {path} changed"
        if parts[0] in TOOLCHAIN_DIRS or path in TOOLCHAIN_ROOT_FILES:
            return f"build or toolchain file {path} changed"
        if path.endswith(CONFIGURE_TEMPLATE_SUFFIX):
            return f"configure template {path} changed (it generates a file the include graph cannot follow)"
    return None


@dataclass(frozen=True)
class TranslationUnit:
    source: str
    include_dirs: tuple[str, ...]


def translation_units(compile_commands: list[Mapping[str, object]], repo_root: str) -> list[TranslationUnit]:
    """The repository's translation units, with their include folders inside the repository, repository-relative."""
    root = PurePosixPath(repo_root)
    units: list[TranslationUnit] = []
    for entry in compile_commands:
        source = _relative_to(PurePosixPath(str(entry["file"])), root)
        if source is None:
            continue
        units.append(TranslationUnit(source, tuple(_include_dirs(_arguments(entry), root))))
    return units


def _arguments(entry: Mapping[str, object]) -> list[str]:
    if "arguments" in entry:
        return [str(argument) for argument in entry["arguments"]]  # type: ignore[union-attr]
    return shlex.split(str(entry["command"]))


def _include_dirs(arguments: list[str], root: PurePosixPath) -> list[str]:
    dirs: list[str] = []
    pending_flag = False
    for argument in arguments:
        folder: str | None = None
        if pending_flag:
            folder, pending_flag = argument, False
        elif argument in INCLUDE_FLAGS:
            pending_flag = True
        else:
            folder = next((argument[len(flag) :] for flag in INCLUDE_FLAGS if argument.startswith(flag)), None)
        if folder:
            relative = _relative_to(PurePosixPath(folder), root)
            if relative is not None:
                dirs.append(relative)
    return dirs


def _relative_to(path: PurePosixPath, root: PurePosixPath) -> str | None:
    if not path.is_relative_to(root):
        return None
    relative = path.relative_to(root).as_posix()
    return relative if relative != "." else ""


def includers_by_header(sources: Mapping[str, str], include_dirs: Iterable[str]) -> dict[str, set[str]]:
    """For each repository file, the repository files that include it directly.

    An include resolves against the includer's folder and every include folder of the repository; when several files
    match, each counts, and includes inside conditional blocks count too, so the graph errs towards more dependents.
    """
    known = set(sources)
    folders = sorted(set(include_dirs))
    includers: dict[str, set[str]] = {}
    for includer, text in sources.items():
        own_folder = PurePosixPath(includer).parent.as_posix()
        for name in INCLUDE_LINE.findall(text):
            for candidate in _candidates(name, own_folder, folders):
                if candidate in known and candidate != includer:
                    includers.setdefault(candidate, set()).add(includer)
    return includers


def _candidates(name: str, own_folder: str, folders: list[str]) -> set[str]:
    candidates = set()
    for folder in [own_folder, *folders]:
        joined = PurePosixPath(folder, name) if folder not in ("", ".") else PurePosixPath(name)
        candidates.add(_normalized(joined))
    return candidates


def _normalized(path: PurePosixPath) -> str:
    parts: list[str] = []
    for part in path.parts:
        if part == "..":
            if parts:
                parts.pop()
        elif part != ".":
            parts.append(part)
    return "/".join(parts)


@dataclass
class TidyPlan:
    """The sources an incremental clang-tidy checks, each with its reason, and the files it leaves out."""

    checked: dict[str, str] = field(default_factory=dict)
    skipped: dict[str, str] = field(default_factory=dict)


def plan_incremental(
    changed: Iterable[str],
    units: Iterable[TranslationUnit],
    includers: Mapping[str, set[str]],
) -> TidyPlan:
    """The translation units to check after the given changes, none of which needs a full clang-tidy."""
    compiled = {unit.source for unit in units}
    plan = TidyPlan()
    for path in sorted(set(changed)):
        if PurePosixPath(path).name == BUILD_FILE:
            module = PurePosixPath(path).parent.as_posix()
            for source in sorted(compiled):
                if source.startswith(module + "/"):
                    plan.checked.setdefault(source, f"{REASON_BUILD_FILE} ({path})")
        elif not is_cpp(path):
            plan.skipped[path] = SKIP_NOT_CPP
        elif path.endswith(SOURCE_SUFFIXES):
            if path in compiled:
                plan.checked.setdefault(path, REASON_CHANGED)
            else:
                plan.skipped[path] = SKIP_NOT_COMPILED
        else:
            for source, chain in _dependent_sources(path, includers, compiled).items():
                plan.checked.setdefault(source, "includes changed " + " via ".join(chain))
            plan.skipped.setdefault(path, "header: checked through the sources that include it")
    return plan


def _dependent_sources(header: str, includers: Mapping[str, set[str]], compiled: set[str]) -> dict[str, list[str]]:
    """Every compiled source that includes the header, directly or not, with the shortest include chain to it."""
    chains: dict[str, list[str]] = {header: [header]}
    frontier = [header]
    while frontier:
        next_frontier: list[str] = []
        for included in frontier:
            for includer in sorted(includers.get(included, ())):
                if includer not in chains:
                    chains[includer] = _extended_chain(chains[included], includer)
                    next_frontier.append(includer)
        frontier = next_frontier
    return {source: chain for source, chain in chains.items() if source in compiled}


def _extended_chain(chain: list[str], includer: str) -> list[str]:
    """The include chain to a header, shown through headers only: the source that ends it is the one checked."""
    return [*chain, includer] if includer.endswith(HEADER_SUFFIXES) else chain


def unchanged_sources(plan: TidyPlan, units: Iterable[TranslationUnit]) -> list[str]:
    """The compiled sources the plan leaves out, all for the same reason: SKIP_UNCHANGED."""
    return sorted({unit.source for unit in units} - plan.checked.keys())
