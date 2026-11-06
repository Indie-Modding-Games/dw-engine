"""Tests of the release workflows' decisions (scripts/ci/release.py)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import release  # noqa: E402


class VersionOrder(unittest.TestCase):
    def test_build_numbers_compare_as_numbers(self):
        self.assertTrue(release.is_newer("0.1.0-beta.1030", "0.1.0-beta.1016"))
        self.assertTrue(release.is_newer("0.1.0-beta.1000", "0.1.0-beta.999"))
        self.assertTrue(release.is_newer("0.1.0-beta.10", "0.1.0-beta.5"))

    def test_a_release_ranks_above_its_prereleases_and_below_the_next(self):
        self.assertTrue(release.is_newer("0.1.0", "0.1.0-beta.9999"))
        self.assertTrue(release.is_newer("0.2.0-beta.1", "0.1.0"))
        self.assertTrue(release.is_newer("0.2.0-beta.1", "0.1.9-beta.5000"))

    def test_equal_is_not_newer(self):
        self.assertFalse(release.is_newer("0.1.3", "0.1.3"))
        self.assertFalse(release.is_newer("0.1.0-beta.7", "0.1.0-beta.7"))

    def test_text_identifiers_rank_above_numbers(self):
        self.assertTrue(release.is_newer("1.0.0-beta", "1.0.0-7"))
        self.assertTrue(release.is_newer("1.0.0-beta.2", "1.0.0-alpha.9"))

    def test_not_a_version(self):
        with self.assertRaises(ValueError):
            release.version_key("v0.1.0")


class BuildVersion(unittest.TestCase):
    def test_project_version_and_commit_count(self):
        cmake = 'cmake_minimum_required(VERSION 3.30)\nproject(dwe VERSION 0.1.0 LANGUAGES CXX)\n'
        self.assertEqual(release.build_version(cmake, 1030), "0.1.0-beta.1030")

    def test_the_repository_has_a_version(self):
        cmake = (release.ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertRegex(release.build_version(cmake, 1), r"^\d+\.\d+\.\d+-beta\.1$")

    def test_no_project_version(self):
        with self.assertRaises(ValueError):
            release.build_version("project(other)", 1)


class LauncherVersion(unittest.TestCase):
    def test_unchanged_launcher_may_keep_its_version(self):
        self.assertIsNone(release.launcher_problem("0.1.3", "0.1.3", False, "v1"))

    def test_changed_launcher_must_rise(self):
        self.assertIn("still 0.1.3", release.launcher_problem("0.1.3", "0.1.3", True, "v1"))
        self.assertIsNone(release.launcher_problem("0.1.4", "0.1.3", True, "v1"))

    def test_never_lower(self):
        self.assertIn("lower", release.launcher_problem("0.1.2", "0.1.3", False, "v1"))


class Notes(unittest.TestCase):
    GENERATED = (
        "## What's Changed\n"
        "* Fix the shop by @someone in https://github.com/o/r/pull/12\n"
        "* Bump a tool by @dependabot[bot] in https://github.com/o/r/pull/13\n"
        "\n## New Contributors\n"
        "* @someone made their first contribution in https://github.com/o/r/pull/12\n"
        "\n**Full Changelog**: https://github.com/o/r/compare/v1...v2\n"
    )

    def test_testing_notes_in_spanish(self):
        text = release.spanish_notes("testing", "0.1.0-beta.1030", "0123456789abcdef", self.GENERATED)
        self.assertTrue(text.startswith("Versión de prueba **0.1.0-beta.1030** (commit `01234567`)"))
        self.assertIn("## Cambios\n* Fix the shop (@someone, https://github.com/o/r/pull/12)", text)
        self.assertIn("* Bump a tool (@dependabot[bot], https://github.com/o/r/pull/13)", text)
        self.assertIn("@someone colabora por primera vez en https://github.com/o/r/pull/12", text)
        self.assertIn("**Todos los cambios**: https://github.com/o/r/compare/v1...v2", text)
        self.assertNotIn("What's Changed", text)

    def test_stable_notes_name_the_tested_version(self):
        text = release.spanish_notes("stable", "0.1.0-beta.1030", "0123", "", "v0.1.0-beta.1030")
        self.assertEqual(text, "Versión estable **0.1.0-beta.1030**: la versión de prueba v0.1.0-beta.1030, "
                         "ya probada, con los mismos archivos. Llega a los dos canales del launcher.\n")


if __name__ == "__main__":
    unittest.main()
