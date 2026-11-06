"""Tests of the packages, their dependencies and what a change affects (scripts/packages.py)."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import packages  # noqa: E402

REPOSITORY = Path(__file__).resolve().parents[2]
TREE = {
    "packages/engine/core/CMakeLists.txt": "dwe_add_module(core\n    LAYER core\n    SOURCES src/a.cpp)\n",
    "packages/engine/assets/CMakeLists.txt": "dwe_add_module(assets LAYER systems SOURCES src/a.cpp DEPS core)\n",
    "packages/game/field/CMakeLists.txt": "dwe_add_module(field\n    LAYER game\n    DEPS assets\n"
                                          "    PRIVATE_LIBS yaml-cpp::yaml-cpp)\n",
    "packages/session/CMakeLists.txt": "dwe_add_module(session LAYER apps DEPS field)\n"
                                       "target_include_directories(x PRIVATE\n"
                                       "    ${PROJECT_SOURCE_DIR}/packages/engine/core/tests)\n",
    "apps/game/CMakeLists.txt": "dwe_add_app(game LAYER apps SOURCES src/main.cpp DEPS session)\n"
                                "target_link_libraries(dwe_game_tests PRIVATE dwe::assets dwe_test_main)\n",
    "packages/CMakeLists.txt": "add_subdirectory(engine)\n",
    "packages/vendor/cgltf/CMakeLists.txt": "add_library(dwe_cgltf STATIC cgltf.cpp)\n",
    "apps/launcher/package.json": "{}\n",
}


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        root = Path(self.folder.name)
        for path, text in TREE.items():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text(text, encoding="utf-8")
        self.packages = packages.find_packages(root)

    def tearDown(self):
        self.folder.cleanup()

    def test_a_package_is_a_folder_that_declares_a_module_or_a_program(self):
        self.assertEqual(sorted(self.packages), ["assets", "core", "field", "game", "launcher", "repo", "session"])
        self.assertEqual(self.packages["field"].folder, "packages/game/field")
        self.assertFalse(self.packages["launcher"].cmake)

    def test_the_dependencies_are_the_modules_and_folders_its_build_file_names(self):
        self.assertEqual(self.packages["field"].deps, {"assets"})
        self.assertEqual(self.packages["session"].deps, {"field", "core"})
        self.assertEqual(self.packages["game"].deps, {"session", "assets"})

    def test_specs_name_a_package_its_dependencies_its_dependents_or_a_folder(self):
        self.assertEqual(packages.select(self.packages, ["field"]), {"field"})
        self.assertEqual(packages.select(self.packages, ["field..."]), {"field", "assets", "core"})
        self.assertEqual(packages.select(self.packages, ["...field"]), {"field", "session", "game"})
        self.assertEqual(packages.select(self.packages, ["packages/engine/*"]), {"core", "assets"})
        with self.assertRaises(SystemExit):
            packages.select(self.packages, ["physics"])

    def test_a_change_affects_its_package_and_every_dependent(self):
        self.assertEqual(packages.affected_by(self.packages, ["packages/engine/assets/src/a.cpp"]),
                         {"assets", "field", "session", "game"})
        self.assertEqual(packages.affected_by(self.packages, ["apps/launcher/ui/app.js"]), {"launcher"})
        self.assertEqual(packages.affected_by(self.packages, ["scripts/dev.sh"]), {"repo"})

    def test_documentation_affects_nothing_and_the_build_affects_everything(self):
        self.assertEqual(packages.affected_by(self.packages, ["docs/index.md", "packages/session/README.md"]), set())
        self.assertEqual(packages.affected_by(self.packages, ["tooling/cmake/DweModule.cmake"]), set(self.packages))
        self.assertEqual(packages.affected_by(self.packages, ["packages/CMakeLists.txt"]), set(self.packages))
        self.assertEqual(packages.affected_by(self.packages, ["packages/vendor/cgltf/cgltf.cpp"]), set(self.packages))


class RepositoryTests(unittest.TestCase):
    def test_the_engine_uses_only_the_engine_and_no_package_uses_an_app(self):
        found = packages.find_packages(REPOSITORY)
        for name, package in found.items():
            for dep in package.deps:
                folder = found[dep].folder
                if package.folder.startswith("packages/engine/"):
                    self.assertTrue(folder.startswith("packages/engine/"), f"{name} uses {dep}")
                if package.folder.startswith("packages/"):
                    self.assertFalse(folder.startswith("apps/"), f"{name} uses {dep}")


if __name__ == "__main__":
    unittest.main()
