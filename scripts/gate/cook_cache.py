"""The gate's cache of cooked content: when the extracted pack and the cook tool are unchanged, reuse the last cook.

The key covers every input of the cook: the bytes of the extracted pack, the sources of every module the cook tool
is built from (each by its git tree: the modules of its link inputs and of every header its objects included), the
build files above those modules, the build and toolchain files, the development image and the preset. An entry is only
reused when it is recent and was cooked on a commit of main below the tip, and the restored copy must match the hashes
recorded when the entry was stored; anything else is a miss, and the gate cooks from scratch.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path, PurePosixPath

ENTRY_MAX_AGE = timedelta(hours=24)
MAX_ENTRIES = 2
MANIFEST_FILE = "manifest.json"
STAGING_PREFIX = ".staging-"
FOLDER_CONTENT = "folder"
ARCHIVE_CONTENT = "archive"
ARCHIVE_NAME = "base.pak"
BUILD_FILE = "CMakeLists.txt"
BUILD_OUTPUT_DIR = "build"
HASH_CHUNK_BYTES = 1 << 20
# Inputs of every cook besides the modules of the tool: the build, the toolchain, the cook procedure itself and the
# recipes built into the tool (apps/dwx/CMakeLists.txt).
FIXED_TOOL_INPUTS = (
    ".devcontainer",
    "content/enhance",
    "tooling/cmake",
    "tooling/triplets",
    BUILD_FILE,
    "CMakePresets.json",
    "vcpkg.json",
    "scripts/dev.sh",
)


def module_dir_of(path: str, build_files: set[str]) -> str | None:
    """The folder of the nearest build file above a repository file, or None when only the root one is."""
    for parent in PurePosixPath(path).parents:
        folder = parent.as_posix()
        if folder == ".":
            return None
        if f"{folder}/{BUILD_FILE}" in build_files:
            return folder
    return None


def ninja_deps_paths(lines: Iterable[str]) -> list[str]:
    """The files that `ninja -t deps` lists for objects (the indented lines), without the objects' own header lines."""
    return [line.strip() for line in lines if line[:1].isspace() and line.strip()]


def ancestor_build_files(modules: Iterable[str], build_files: set[str]) -> list[str]:
    """The build files in the folders above the modules, below the root one (a fixed input): they add the modules."""
    found: set[str] = set()
    for module in modules:
        for parent in PurePosixPath(module).parents:
            candidate = f"{parent.as_posix()}/{BUILD_FILE}"
            if parent.as_posix() != "." and candidate in build_files:
                found.add(candidate)
    return sorted(found)


def tool_input_dirs(ninja_inputs: Iterable[str], repo_root: str, build_dir: str, build_files: set[str]) -> list[str]:
    """The repository modules of the files the cook tool is built from: `ninja -t inputs` of its target, and the
    headers that `ninja -t deps` lists for its objects (a header-only module has no input of its own)."""
    root = Path(repo_root)
    build = Path(build_dir)
    modules: set[str] = set()
    for line in ninja_inputs:
        name = line.strip()
        if not name:
            continue
        path = Path(os.path.normpath(build / name if not os.path.isabs(name) else name))
        if not path.is_relative_to(root):
            continue
        relative = path.relative_to(root).as_posix()
        if relative.split("/", 1)[0] == BUILD_OUTPUT_DIR:
            continue
        module = module_dir_of(relative, build_files)
        if module is not None:
            modules.add(module)
    return sorted(modules)


def file_digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(HASH_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def folder_digest(folder: Path) -> str:
    """One hash of every file of a folder: its relative path and its bytes, in a fixed order."""
    digest = hashlib.sha256()
    for path in sorted(folder.rglob("*"), key=lambda candidate: candidate.relative_to(folder).as_posix()):
        if path.is_file():
            digest.update(path.relative_to(folder).as_posix().encode())
            digest.update(b"\0")
            digest.update(file_digest(path).encode())
            digest.update(b"\n")
    return digest.hexdigest()


@dataclass(frozen=True)
class CookKey:
    """Every input of a cook by name, and their joint hash, which names the cache entry."""

    parts: Mapping[str, str]

    @property
    def digest(self) -> str:
        text = "".join(f"{name}={value}\n" for name, value in sorted(self.parts.items()))
        return hashlib.sha256(text.encode()).hexdigest()

    def to_json(self) -> str:
        return json.dumps(dict(sorted(self.parts.items())), indent=2)

    @staticmethod
    def from_json(text: str) -> CookKey:
        return CookKey(json.loads(text))


def cook_key(
    *,
    content_digest: str,
    tree_ids: Mapping[str, str],
    image_id: str,
    preset: str,
) -> CookKey:
    parts = {"content": content_digest, "image": image_id, "preset": preset}
    parts.update({f"tree:{path}": tree for path, tree in tree_ids.items()})
    return CookKey(parts)


def tree_paths(key: CookKey) -> list[str]:
    """The repository paths whose git trees the key covers."""
    prefix = "tree:"
    return sorted(name[len(prefix) :] for name in key.parts if name.startswith(prefix))


def full_cook_trigger(changed: Iterable[str], tool_paths: Iterable[str]) -> str | None:
    """Why a change since the cached cook needs a cook from scratch: it touches the cook tool or the toolchain."""
    paths = sorted(set(tool_paths))
    for path in sorted(changed):
        for tool_path in paths:
            if path == tool_path or path.startswith(tool_path + "/"):
                return f"{path} changed (input of the cook: {tool_path})"
    return None


@dataclass(frozen=True)
class Manifest:
    """What a cache entry recorded when it was stored."""

    key_digest: str
    commit: str
    cooked_at: datetime
    archive_digest: str
    folder_digest: str

    def to_json(self) -> str:
        return json.dumps(
            {
                "key": self.key_digest,
                "commit": self.commit,
                "cooked_at": self.cooked_at.isoformat(),
                "archive_sha256": self.archive_digest,
                "folder_sha256": self.folder_digest,
            },
            indent=2,
        )

    @staticmethod
    def from_json(text: str) -> Manifest:
        fields = json.loads(text)
        return Manifest(
            key_digest=fields["key"],
            commit=fields["commit"],
            cooked_at=datetime.fromisoformat(fields["cooked_at"]),
            archive_digest=fields["archive_sha256"],
            folder_digest=fields["folder_sha256"],
        )


@dataclass(frozen=True)
class Lookup:
    """A usable entry, or why there is none."""

    manifest: Manifest | None
    reason: str


def entry_dir(cache_dir: Path, key: CookKey) -> Path:
    return cache_dir / key.digest


def look_up(
    cache_dir: Path,
    key: CookKey,
    *,
    now: datetime,
    is_on_main_below_tip: Callable[[str], bool],
) -> Lookup:
    """The entry of the key, when it is recent and was cooked on a commit of main below the tip."""
    manifest_path = entry_dir(cache_dir, key) / MANIFEST_FILE
    if not manifest_path.is_file():
        return Lookup(None, "no cached cook with this key")
    manifest = Manifest.from_json(manifest_path.read_text())
    short = manifest.commit[:12]
    if manifest.key_digest != key.digest:
        return Lookup(None, "the cached manifest names another key")
    if now - manifest.cooked_at > ENTRY_MAX_AGE:
        return Lookup(None, f"the cached cook of {short} is older than {ENTRY_MAX_AGE}")
    if not is_on_main_below_tip(manifest.commit):
        return Lookup(None, f"the cached cook of {short} is not on main below the tip")
    return Lookup(manifest, f"cooked on {short} at {manifest.cooked_at.isoformat()}")


def cooked_digests(cooked: Path) -> tuple[str, str]:
    """The hashes of a cook's output: its archive, and its cooked pack folder."""
    return file_digest(cooked / ARCHIVE_CONTENT / ARCHIVE_NAME), folder_digest(cooked / FOLDER_CONTENT)


def restore(cache_dir: Path, key: CookKey, manifest: Manifest, destination: Path) -> str | None:
    """Copies the entry into an empty destination and checks it against the manifest; the mismatch, or None."""
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(entry_dir(cache_dir, key), destination, ignore=shutil.ignore_patterns(MANIFEST_FILE))
    archive, folder = cooked_digests(destination)
    if archive != manifest.archive_digest:
        return f"the restored archive's sha256 {archive} is not the recorded {manifest.archive_digest}"
    if folder != manifest.folder_digest:
        return f"the restored folder's sha256 {folder} is not the recorded {manifest.folder_digest}"
    return None


def store(cache_dir: Path, key: CookKey, cooked: Path, *, commit: str, now: datetime) -> Manifest:
    """Copies a validated cook into the cache: into a staging folder first, renamed into place once complete."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    staging = cache_dir / f"{STAGING_PREFIX}{os.getpid()}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()
    for content in (FOLDER_CONTENT, ARCHIVE_CONTENT):
        shutil.copytree(cooked / content, staging / content)
    archive, folder = cooked_digests(staging)
    manifest = Manifest(key.digest, commit, now, archive, folder)
    (staging / MANIFEST_FILE).write_text(manifest.to_json())
    target = entry_dir(cache_dir, key)
    if target.exists():
        shutil.rmtree(target)
    staging.rename(target)
    prune(cache_dir, keep=target)
    return manifest


def prune(cache_dir: Path, *, keep: Path) -> list[Path]:
    """Removes stale staging folders and all but the newest entries; returns what it removed."""
    removed: list[Path] = []
    entries: list[tuple[datetime, Path]] = []
    for child in cache_dir.iterdir():
        if child.name.startswith(STAGING_PREFIX):
            removed.append(child)
        elif (child / MANIFEST_FILE).is_file() and child != keep:
            entries.append((Manifest.from_json((child / MANIFEST_FILE).read_text()).cooked_at, child))
    entries.sort(reverse=True)
    removed.extend(path for _, path in entries[MAX_ENTRIES - 1 :])
    for path in removed:
        shutil.rmtree(path)
    return removed
