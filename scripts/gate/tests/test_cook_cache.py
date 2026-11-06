"""Tests of the gate's cache of cooked content (scripts/gate/cook_cache.py)."""

from __future__ import annotations

import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import cook_cache  # noqa: E402

NOW = datetime(2026, 10, 22, 12, 0, tzinfo=timezone.utc)
ROOT = "/repo"
BUILD_DIR = "/repo/build/ci"
BUILD_FILES = {"CMakeLists.txt", "apps/CMakeLists.txt", "apps/dwx/CMakeLists.txt", "packages/formats/CMakeLists.txt",
               "packages/CMakeLists.txt", "packages/engine/CMakeLists.txt", "packages/engine/core/CMakeLists.txt",
               "packages/engine/math/CMakeLists.txt"}


def key(content: str = "content", **trees: str) -> cook_cache.CookKey:
    return cook_cache.cook_key(content_digest=content, tree_ids=trees or {"apps/dwx": "t1"}, image_id="img",
                               preset="ci")


def write_cook(folder: Path, archive_bytes: bytes = b"pak", asset_bytes: bytes = b"asset") -> Path:
    (folder / cook_cache.FOLDER_CONTENT / "base" / "maps").mkdir(parents=True)
    (folder / cook_cache.FOLDER_CONTENT / "base" / "maps" / "a.ktx2").write_bytes(asset_bytes)
    (folder / cook_cache.ARCHIVE_CONTENT).mkdir(parents=True)
    (folder / cook_cache.ARCHIVE_CONTENT / cook_cache.ARCHIVE_NAME).write_bytes(archive_bytes)
    return folder


class ToolInputTests(unittest.TestCase):
    def test_ninja_inputs_map_to_the_modules_of_their_build_files(self):
        inputs = [
            "/repo/apps/dwx/src/main.cpp",
            "../../packages/formats/src/pak.cpp",
            "/repo/packages/engine/core/src/log.cpp",
            "apps/dwx/CMakeFiles/dwx.dir/src/main.cpp.o",
            "vcpkg_installed/x64-linux-libcxx/lib/libzstd.a",
            "/usr/lib/libc++.a",
            "",
        ]
        self.assertEqual(cook_cache.tool_input_dirs(inputs, ROOT, BUILD_DIR, BUILD_FILES),
                         ["apps/dwx", "packages/engine/core", "packages/formats"])

    def test_headers_of_the_objects_add_their_modules_header_only_ones_too(self):
        deps = [
            "apps/dwx/CMakeFiles/dwx.dir/src/main.cpp.o: #deps 3, deps mtime 1 (VALID)",
            "    ../../apps/dwx/src/main.cpp",
            "    ../../packages/engine/math/include/dwe/math/vec.hpp",
            "    /usr/include/c++/v1/vector",
            "",
        ]
        paths = cook_cache.ninja_deps_paths(deps)
        self.assertEqual(paths, ["../../apps/dwx/src/main.cpp", "../../packages/engine/math/include/dwe/math/vec.hpp",
                                 "/usr/include/c++/v1/vector"])
        self.assertEqual(cook_cache.tool_input_dirs(paths, ROOT, BUILD_DIR, BUILD_FILES),
                         ["apps/dwx", "packages/engine/math"])

    def test_the_build_files_above_the_modules_are_inputs_but_the_root_one_is_fixed(self):
        self.assertEqual(cook_cache.ancestor_build_files(["apps/dwx", "packages/engine/core", "packages/engine/math",
                                                          "packages/formats"],
                                                         BUILD_FILES),
                         ["apps/CMakeLists.txt", "packages/CMakeLists.txt", "packages/engine/CMakeLists.txt"])

    def test_a_file_under_only_the_root_build_file_has_no_module(self):
        self.assertIsNone(cook_cache.module_dir_of("tests/main.cpp", BUILD_FILES))
        self.assertEqual(cook_cache.module_dir_of("apps/dwx/src/a/b.cpp", BUILD_FILES), "apps/dwx")


class KeyTests(unittest.TestCase):
    def test_every_part_changes_the_key(self):
        base = key()
        self.assertNotEqual(base.digest, key(content="other").digest)
        self.assertNotEqual(base.digest, key(**{"apps/dwx": "t2"}).digest)
        self.assertNotEqual(base.digest, cook_cache.cook_key(content_digest="content", tree_ids={"apps/dwx": "t1"},
                                                             image_id="img2", preset="ci").digest)
        self.assertEqual(base.digest, key().digest)

    def test_keys_round_trip_through_json_and_list_their_trees(self):
        original = key(**{"apps/dwx": "t1", "cmake": "t3"})
        self.assertEqual(cook_cache.CookKey.from_json(original.to_json()).digest, original.digest)
        self.assertEqual(cook_cache.tree_paths(original), ["apps/dwx", "cmake"])


class FolderDigestTests(unittest.TestCase):
    def test_the_digest_follows_bytes_and_names_not_timestamps(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            write_cook(Path(first))
            write_cook(Path(second))
            self.assertEqual(cook_cache.folder_digest(Path(first)), cook_cache.folder_digest(Path(second)))
            (Path(second) / "folder" / "base" / "maps" / "a.ktx2").write_bytes(b"other")
            self.assertNotEqual(cook_cache.folder_digest(Path(first)), cook_cache.folder_digest(Path(second)))

    def test_a_renamed_file_changes_the_digest(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            (Path(first) / "a").write_bytes(b"x")
            (Path(second) / "b").write_bytes(b"x")
            self.assertNotEqual(cook_cache.folder_digest(Path(first)), cook_cache.folder_digest(Path(second)))


class TriggerTests(unittest.TestCase):
    def test_a_change_inside_a_tool_input_needs_a_full_cook(self):
        reason = cook_cache.full_cook_trigger(["docs/a.md", "apps/dwx/src/cook.cpp"], ["apps/dwx", "cmake"])
        self.assertIn("apps/dwx/src/cook.cpp", reason)

    def test_other_changes_and_prefix_lookalikes_do_not(self):
        self.assertIsNone(cook_cache.full_cook_trigger(["docs/a.md", "apps/dwxtra/a.cpp"], ["apps/dwx"]))


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.cooked = write_cook(self.root / "cooked")
        self.key = key()

    def tearDown(self):
        self.temporary.cleanup()

    def look_up(self, now=NOW, on_main=lambda commit: True):
        return cook_cache.look_up(self.cache, self.key, now=now, is_on_main_below_tip=on_main)

    def test_a_stored_cook_is_found_and_restored_byte_identical(self):
        stored = cook_cache.store(self.cache, self.key, self.cooked, commit="c1", now=NOW)
        lookup = self.look_up(now=NOW + timedelta(hours=1))
        self.assertEqual(lookup.manifest, stored)
        destination = self.root / "gate-cooked"
        self.assertIsNone(cook_cache.restore(self.cache, self.key, lookup.manifest, destination))
        self.assertEqual(cook_cache.cooked_digests(destination), cook_cache.cooked_digests(self.cooked))
        self.assertFalse((destination / cook_cache.MANIFEST_FILE).exists())

    def test_another_key_misses(self):
        cook_cache.store(self.cache, self.key, self.cooked, commit="c1", now=NOW)
        self.assertIsNone(cook_cache.look_up(self.cache, key(content="new"), now=NOW,
                                             is_on_main_below_tip=lambda commit: True).manifest)

    def test_an_entry_older_than_a_day_misses(self):
        cook_cache.store(self.cache, self.key, self.cooked, commit="c1", now=NOW)
        self.assertIn("older than", self.look_up(now=NOW + timedelta(hours=25)).reason)

    def test_an_entry_off_main_misses(self):
        cook_cache.store(self.cache, self.key, self.cooked, commit="c1", now=NOW)
        self.assertIn("not on main", self.look_up(on_main=lambda commit: False).reason)

    def test_a_corrupted_archive_or_folder_is_reported_on_restore(self):
        manifest = cook_cache.store(self.cache, self.key, self.cooked, commit="c1", now=NOW)
        entry = cook_cache.entry_dir(self.cache, self.key)
        (entry / "archive" / "base.pak").write_bytes(b"tampered")
        self.assertIn("archive", cook_cache.restore(self.cache, self.key, manifest, self.root / "out"))
        (entry / "archive" / "base.pak").write_bytes(b"pak")
        (entry / "folder" / "base" / "extra").write_bytes(b"x")
        self.assertIn("folder", cook_cache.restore(self.cache, self.key, manifest, self.root / "out"))

    def test_store_leaves_no_staging_and_keeps_only_the_newest_entries(self):
        for index in range(cook_cache.MAX_ENTRIES + 2):
            cook_cache.store(self.cache, key(content=str(index)), self.cooked, commit="c", now=NOW + timedelta(index))
        names = sorted(child.name for child in self.cache.iterdir())
        self.assertEqual(len(names), cook_cache.MAX_ENTRIES)
        self.assertFalse(any(name.startswith(cook_cache.STAGING_PREFIX) for name in names))
        self.assertIn(key(content=str(cook_cache.MAX_ENTRIES + 1)).digest, names)
        self.assertIn(key(content=str(cook_cache.MAX_ENTRIES)).digest, names)


if __name__ == "__main__":
    unittest.main()
