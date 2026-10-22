#!/usr/bin/env python3
"""The decisions of scripts/dev.sh check-gate: the scope of clang-tidy and the reuse of a cached cook.

It runs on the host, next to git, and writes what it decided as `gate:` lines (the gate's log) and as files that
dev.sh reads. It never runs clang-tidy or dwx itself.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timedelta
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cook_cache  # noqa: E402
import tidy_scope  # noqa: E402

MAIN_BRANCH = "main"
MODE_FULL = "full"
MODE_INCREMENTAL = "incremental"
TIDY_MARKERS_DIR = "tidy-full"
COOK_CACHE_DIR = "cook"
MARKER_SUFFIX = ".json"
MARKER_KEEP = timedelta(days=7)
EXIT_HIT = 0
EXIT_MISS = 3


def git(repo: Path, *arguments: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *arguments], check=True, capture_output=True, text=True).stdout


def is_ancestor(repo: Path, commit: str, descendant: str) -> bool:
    result = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor", commit, descendant], capture_output=True, check=False
    )
    return result.returncode == 0


def on_main_below_tip(repo: Path, main: str):
    return lambda commit: is_ancestor(repo, commit, main) and is_ancestor(repo, commit, "HEAD")


def changed_since(repo: Path, commit: str) -> list[str]:
    """The files that differ between a commit and the working tree, uncommitted and untracked ones included."""
    tracked = git(repo, "diff", "--name-only", "--no-renames", "-z", commit)
    untracked = git(repo, "ls-files", "--others", "--exclude-standard", "-z")
    return sorted({path for path in (tracked + untracked).split("\0") if path})


def tree_is_clean(repo: Path) -> bool:
    return git(repo, "status", "--porcelain").strip() == ""


def now() -> datetime:
    return datetime.now().astimezone()


def emit(log, line: str) -> None:
    print(line)
    log.write(line + "\n")


# --- clang-tidy ---------------------------------------------------------------------------------------------------


def read_markers(state: Path) -> list[tidy_scope.FullTidyMarker]:
    folder = state / TIDY_MARKERS_DIR
    if not folder.is_dir():
        return []
    return [tidy_scope.FullTidyMarker.from_json(path.read_text()) for path in folder.glob("*" + MARKER_SUFFIX)]


def repository_sources(repo: Path) -> dict[str, str]:
    names = git(repo, "ls-files", "-z", "--", "*.cpp", "*.hpp", "*.h").split("\0")
    return {name: (repo / name).read_text(errors="replace") for name in names if name and (repo / name).is_file()}


def tidy_plan(arguments: argparse.Namespace) -> int:
    repo = Path(arguments.repo)
    with open(arguments.log, "w") as log:
        full_reason = None
        reference = None
        if arguments.mode == MODE_FULL:
            full_reason = "requested (DWE_GATE_TIDY=full)"
        else:
            reference = tidy_scope.choose_reference(
                read_markers(Path(arguments.state)),
                now=now(),
                image_id=arguments.image_id,
                tool_version=arguments.tool_version,
                is_on_main_below_tip=on_main_below_tip(repo, arguments.main),
            )
            if reference.marker is None:
                full_reason = reference.reason
        changed: list[str] = []
        if full_reason is None and reference is not None and reference.marker is not None:
            changed = changed_since(repo, reference.marker.commit)
            full_reason = tidy_scope.full_tidy_trigger(changed)
        if full_reason is not None:
            emit(log, f"gate: tidy=full reason={full_reason}")
            Path(arguments.scope_out).write_text(MODE_FULL + "\n")
            Path(arguments.list_out).write_text("")
            return 0
        assert reference is not None and reference.marker is not None
        units = tidy_scope.translation_units(json.loads(Path(arguments.compile_commands).read_text()), str(repo))
        include_dirs = {folder for unit in units for folder in unit.include_dirs}
        includers = tidy_scope.includers_by_header(repository_sources(repo), include_dirs)
        plan = tidy_scope.plan_incremental(changed, units, includers)
        unchanged = tidy_scope.unchanged_sources(plan, units)
        emit(
            log,
            f"gate: tidy=incremental reason={reference.reason}; {len(changed)} files changed since; "
            f"{len(plan.checked)} sources checked, {len(unchanged)} skipped as unchanged",
        )
        for source, reason in plan.checked.items():
            emit(log, f"tidy: check {source} ({reason})")
        for path, reason in plan.skipped.items():
            emit(log, f"tidy: not checked {path} ({reason})")
        for source in unchanged:
            log.write(f"tidy: skip {source} ({tidy_scope.SKIP_UNCHANGED})\n")
        Path(arguments.scope_out).write_text(MODE_INCREMENTAL + "\n")
        Path(arguments.list_out).write_text("".join(source + "\n" for source in plan.checked))
    return 0


def tidy_mark(arguments: argparse.Namespace) -> int:
    repo = Path(arguments.repo)
    if not tree_is_clean(repo):
        print("gate: tidy-marker not written: the working tree has changes")
        return 0
    commit = git(repo, "rev-parse", "HEAD").strip()
    marker = tidy_scope.FullTidyMarker(commit, now(), arguments.image_id, arguments.tool_version)
    folder = Path(arguments.state) / TIDY_MARKERS_DIR
    folder.mkdir(parents=True, exist_ok=True)
    staging = folder / f".{commit}{MARKER_SUFFIX}"
    staging.write_text(marker.to_json())
    staging.replace(folder / f"{commit}{MARKER_SUFFIX}")
    for old in read_markers(Path(arguments.state)):
        if marker.passed_at - old.passed_at > MARKER_KEEP:
            (folder / f"{old.commit}{MARKER_SUFFIX}").unlink(missing_ok=True)
    print(f"gate: tidy-marker {commit} at {marker.passed_at.isoformat()}")
    return 0


# --- cook ---------------------------------------------------------------------------------------------------------


def cook_key_command(arguments: argparse.Namespace) -> int:
    repo = Path(arguments.repo)
    build_files = {name for name in git(repo, "ls-files", "-z", "--", "*CMakeLists.txt").split("\0") if name}
    ninja_inputs = Path(arguments.ninja_inputs).read_text().splitlines()
    ninja_inputs += cook_cache.ninja_deps_paths(Path(arguments.ninja_deps).read_text().splitlines())
    modules = cook_cache.tool_input_dirs(ninja_inputs, str(repo), arguments.build_dir, build_files)
    paths = modules + cook_cache.ancestor_build_files(modules, build_files)
    tree_ids = {path: git(repo, "rev-parse", f"HEAD:{path}").strip() for path in paths}
    tree_ids.update(
        {path: git(repo, "rev-parse", f"HEAD:{path}").strip() for path in cook_cache.FIXED_TOOL_INPUTS}
    )
    key = cook_cache.cook_key(
        content_digest=cook_cache.folder_digest(Path(arguments.content)),
        tree_ids=tree_ids,
        image_id=arguments.image_id,
        preset=arguments.preset,
    )
    Path(arguments.key_out).write_text(key.to_json())
    print(f"gate: cook-key {key.digest}")
    for name, value in sorted(key.parts.items()):
        print(f"gate: cook-key part {name}={value}")
    return 0


def cook_restore(arguments: argparse.Namespace) -> int:
    repo = Path(arguments.repo)
    key = cook_cache.CookKey.from_json(Path(arguments.key).read_text())
    cache = Path(arguments.state) / COOK_CACHE_DIR
    if arguments.mode == MODE_FULL:
        print(f"gate: cook=miss key={key.digest} reason=requested (DWE_GATE_COOK=full)")
        return EXIT_MISS
    if not tree_is_clean(repo):
        print(f"gate: cook=miss key={key.digest} reason=the working tree has changes, so its trees are not the key's")
        return EXIT_MISS
    lookup = cook_cache.look_up(cache, key, now=now(), is_on_main_below_tip=on_main_below_tip(repo, arguments.main))
    if lookup.manifest is None:
        print(f"gate: cook=miss key={key.digest} reason={lookup.reason}")
        return EXIT_MISS
    trigger = cook_cache.full_cook_trigger(changed_since(repo, lookup.manifest.commit), cook_cache.tree_paths(key))
    if trigger is not None:
        print(f"gate: cook=miss key={key.digest} reason={trigger}")
        return EXIT_MISS
    mismatch = cook_cache.restore(cache, key, lookup.manifest, Path(arguments.to))
    if mismatch is not None:
        print(f"gate: cook=miss key={key.digest} reason={mismatch}")
        return EXIT_MISS
    print(
        f"gate: cook=hit key={key.digest} reason={lookup.reason}; archive sha256 "
        f"{lookup.manifest.archive_digest} and folder sha256 {lookup.manifest.folder_digest} verified"
    )
    return EXIT_HIT


def cook_store(arguments: argparse.Namespace) -> int:
    repo = Path(arguments.repo)
    if not tree_is_clean(repo):
        print("gate: cook-cache not written: the working tree has changes")
        return 0
    key = cook_cache.CookKey.from_json(Path(arguments.key).read_text())
    commit = git(repo, "rev-parse", "HEAD").strip()
    manifest = cook_cache.store(
        Path(arguments.state) / COOK_CACHE_DIR, key, Path(arguments.cooked), commit=commit, now=now()
    )
    print(f"gate: cook-cache stored key={key.digest} commit={commit} archive sha256 {manifest.archive_digest}")
    return 0


# --- command line -------------------------------------------------------------------------------------------------


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    commands = root.add_subparsers(dest="command", required=True)

    def command(name: str, handler, *options: str) -> argparse.ArgumentParser:
        sub = commands.add_parser(name)
        sub.set_defaults(handler=handler)
        for option in options:
            sub.add_argument(f"--{option}", required=True)
        sub.add_argument("--main", default=MAIN_BRANCH)
        return sub

    plan = command(
        "tidy-plan", tidy_plan, "repo", "state", "compile-commands", "image-id", "tool-version", "list-out",
        "scope-out", "log",
    )
    plan.add_argument("--mode", choices=(MODE_FULL, MODE_INCREMENTAL), required=True)
    command("tidy-mark", tidy_mark, "repo", "state", "image-id", "tool-version")
    command(
        "cook-key", cook_key_command, "repo", "build-dir", "ninja-inputs", "ninja-deps", "content", "image-id",
        "preset", "key-out",
    )
    restore = command("cook-restore", cook_restore, "repo", "state", "key", "to")
    restore.add_argument("--mode", choices=(MODE_FULL, MODE_INCREMENTAL), required=True)
    command("cook-store", cook_store, "repo", "state", "key", "cooked")
    return root


def main() -> int:
    arguments = parser().parse_args()
    return arguments.handler(arguments)


if __name__ == "__main__":
    sys.exit(main())
