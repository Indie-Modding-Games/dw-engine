"""Tests of the scope of the gate's incremental clang-tidy (scripts/gate/tidy_scope.py)."""

from __future__ import annotations

import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import tidy_scope  # noqa: E402
from tidy_scope import FullTidyMarker, TranslationUnit  # noqa: E402

ROOT = "/repo"
NOW = datetime(2026, 10, 22, 12, 0, tzinfo=timezone.utc)
IMAGE = "sha256:image"
TOOL = "clang-tidy 21"


def marker(commit: str, hours_ago: float, image: str = IMAGE, tool: str = TOOL) -> FullTidyMarker:
    return FullTidyMarker(commit, NOW - timedelta(hours=hours_ago), image, tool)


def reference(markers, on_main=lambda commit: True):
    return tidy_scope.choose_reference(
        markers, now=NOW, image_id=IMAGE, tool_version=TOOL, is_on_main_below_tip=on_main
    )


class ReferenceTests(unittest.TestCase):
    def test_no_marker_means_a_full_tidy(self):
        self.assertIsNone(reference([]).marker)

    def test_the_newest_usable_marker_is_the_reference(self):
        chosen = reference([marker("old", 5), marker("new", 1)])
        self.assertEqual(chosen.marker.commit, "new")

    def test_a_marker_older_than_a_day_is_rejected(self):
        chosen = reference([marker("stale", 25)])
        self.assertIsNone(chosen.marker)
        self.assertIn("older than", chosen.reason)

    def test_a_marker_of_another_image_or_tool_is_rejected(self):
        self.assertIsNone(reference([marker("a", 1, image="sha256:other")]).marker)
        self.assertIsNone(reference([marker("a", 1, tool="clang-tidy 20")]).marker)

    def test_a_marker_off_main_falls_back_to_an_older_one_on_main(self):
        chosen = reference([marker("branch", 1), marker("main", 3)], on_main=lambda commit: commit == "main")
        self.assertEqual(chosen.marker.commit, "main")

    def test_markers_round_trip_through_json(self):
        original = marker("abc", 2)
        self.assertEqual(FullTidyMarker.from_json(original.to_json()), original)


class FullTriggerTests(unittest.TestCase):
    def test_a_public_header_needs_a_full_tidy(self):
        self.assertIn("public header", tidy_scope.full_tidy_trigger(["engine/core/include/dwe/core/log.hpp"]))

    def test_the_tidy_configuration_needs_a_full_tidy(self):
        self.assertIsNotNone(tidy_scope.full_tidy_trigger([".clang-tidy"]))
        self.assertIsNotNone(tidy_scope.full_tidy_trigger(["engine/render/.clang-tidy"]))

    def test_build_and_toolchain_files_need_a_full_tidy(self):
        for path in (".devcontainer/Dockerfile", "tooling/cmake/DweModule.cmake", "CMakeLists.txt", "CMakePresets.json",
                     "vcpkg.json", "tooling/triplets/x64-linux-libcxx.cmake", "tooling/vcpkg-ports/sdl3/portfile.cmake",
                     "scripts/dev.sh"):
            with self.subTest(path=path):
                self.assertIsNotNone(tidy_scope.full_tidy_trigger([path]))

    def test_a_configure_template_needs_a_full_tidy(self):
        reason = tidy_scope.full_tidy_trigger(["apps/dwx/src/bundled_recipes.hpp.in"])
        self.assertIsNotNone(reason)
        self.assertIn("configure template", reason)

    def test_private_sources_headers_and_other_files_do_not(self):
        changed = ["engine/core/src/log.cpp", "engine/core/src/detail.hpp", "engine/core/CMakeLists.txt",
                   "docs/guides/prueba-vertical.md"]
        self.assertIsNone(tidy_scope.full_tidy_trigger(changed))


class TranslationUnitTests(unittest.TestCase):
    def test_include_dirs_inside_the_repository_are_kept_relative(self):
        entries = [
            {
                "file": f"{ROOT}/engine/core/src/log.cpp",
                "command": f"clang++ -I{ROOT}/engine/core/include -isystem {ROOT}/build/ci/vcpkg/include "
                f"-I /usr/include -iquote{ROOT}/engine/core/src -c {ROOT}/engine/core/src/log.cpp",
            },
            {"file": "/elsewhere/x.cpp", "arguments": ["clang++", "-c", "/elsewhere/x.cpp"]},
        ]
        units = tidy_scope.translation_units(entries, ROOT)
        self.assertEqual(
            units,
            [TranslationUnit("engine/core/src/log.cpp",
                             ("engine/core/include", "build/ci/vcpkg/include", "engine/core/src"))],
        )


SOURCES = {
    "engine/core/include/dwe/core/log.hpp": '#include "dwe/core/types.hpp"\n',
    "engine/core/include/dwe/core/types.hpp": "",
    "engine/core/src/log.cpp": '#include "dwe/core/log.hpp"\n#include "format.hpp"\n',
    "engine/core/src/format.hpp": '#include "../src/escape.hpp"\n',
    "engine/core/src/escape.hpp": "",
    "engine/core/src/clock.cpp": '#include <dwe/core/types.hpp>\n',
    "engine/core/tests/log_tests.cpp": '#  include "dwe/core/log.hpp"\n#ifdef _WIN32\n#include "win.hpp"\n#endif\n',
    "engine/core/tests/win.hpp": "",
    "game/field/src/field.cpp": '#include "format.hpp"\n',
}
UNITS = [
    TranslationUnit(source, ("engine/core/include",))
    for source in ("engine/core/src/log.cpp", "engine/core/src/clock.cpp", "engine/core/tests/log_tests.cpp",
                   "game/field/src/field.cpp")
]


class IncludeGraphTests(unittest.TestCase):
    def setUp(self):
        self.includers = tidy_scope.includers_by_header(SOURCES, ["engine/core/include"])

    def test_quoted_and_angled_includes_resolve_against_include_dirs(self):
        self.assertEqual(self.includers["engine/core/include/dwe/core/types.hpp"],
                         {"engine/core/include/dwe/core/log.hpp", "engine/core/src/clock.cpp"})

    def test_includes_resolve_against_the_includers_folder_with_dot_dot(self):
        self.assertEqual(self.includers["engine/core/src/escape.hpp"], {"engine/core/src/format.hpp"})

    def test_a_header_of_the_same_name_in_another_folder_is_not_confused(self):
        self.assertEqual(self.includers["engine/core/src/format.hpp"], {"engine/core/src/log.cpp"})

    def test_conditional_includes_count(self):
        self.assertEqual(self.includers["engine/core/tests/win.hpp"], {"engine/core/tests/log_tests.cpp"})


class PlanTests(unittest.TestCase):
    def plan(self, changed):
        includers = tidy_scope.includers_by_header(SOURCES, ["engine/core/include"])
        return tidy_scope.plan_incremental(changed, UNITS, includers)

    def test_a_changed_source_is_checked(self):
        plan = self.plan(["engine/core/src/clock.cpp"])
        self.assertEqual(plan.checked, {"engine/core/src/clock.cpp": tidy_scope.REASON_CHANGED})

    def test_a_private_header_checks_every_source_that_includes_it_transitively(self):
        plan = self.plan(["engine/core/src/escape.hpp"])
        self.assertEqual(list(plan.checked), ["engine/core/src/log.cpp"])
        self.assertEqual(plan.checked["engine/core/src/log.cpp"],
                         "includes changed engine/core/src/escape.hpp via engine/core/src/format.hpp")

    def test_a_conditionally_included_header_checks_its_includer(self):
        self.assertEqual(list(self.plan(["engine/core/tests/win.hpp"]).checked), ["engine/core/tests/log_tests.cpp"])

    def test_a_module_build_file_checks_every_source_of_the_module(self):
        plan = self.plan(["engine/core/CMakeLists.txt"])
        self.assertEqual(sorted(plan.checked),
                         ["engine/core/src/clock.cpp", "engine/core/src/log.cpp", "engine/core/tests/log_tests.cpp"])

    def test_sources_outside_the_compile_commands_and_other_files_are_skipped_with_a_reason(self):
        plan = self.plan(["apps/game/src/windows_only.cpp", "docs/guides/prueba-vertical.md"])
        self.assertEqual(plan.checked, {})
        self.assertEqual(plan.skipped["apps/game/src/windows_only.cpp"], tidy_scope.SKIP_NOT_COMPILED)
        self.assertEqual(plan.skipped["docs/guides/prueba-vertical.md"], tidy_scope.SKIP_NOT_CPP)

    def test_unchanged_sources_are_listed_as_skipped(self):
        plan = self.plan(["engine/core/src/clock.cpp"])
        self.assertEqual(tidy_scope.unchanged_sources(plan, UNITS),
                         ["engine/core/src/log.cpp", "engine/core/tests/log_tests.cpp", "game/field/src/field.cpp"])


if __name__ == "__main__":
    unittest.main()
