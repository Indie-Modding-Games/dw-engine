"""Tests of the release manifests' checks (scripts/package/release_manifest.py)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import release_manifest  # noqa: E402

PLATFORMS = {"linux-x86_64", "windows-x86_64", "darwin-aarch64", "darwin-x86_64"}


class LatestKeys(unittest.TestCase):
    def test_plain_keys(self):
        self.assertEqual(release_manifest.latest_key_problems(set(PLATFORMS), PLATFORMS), [])

    def test_installer_keys_of_a_computer(self):
        keys = PLATFORMS | {"linux-x86_64-deb", "linux-x86_64-appimage"}
        self.assertEqual(release_manifest.latest_key_problems(keys, PLATFORMS), [])

    def test_installer_key_without_its_plain_key(self):
        keys = (PLATFORMS - {"linux-x86_64"}) | {"linux-x86_64-deb"}
        self.assertEqual(len(release_manifest.latest_key_problems(keys, PLATFORMS)), 1)

    def test_installer_key_of_another_computer(self):
        keys = PLATFORMS | {"linux-aarch64-deb"}
        self.assertEqual(len(release_manifest.latest_key_problems(keys, PLATFORMS)), 1)

    def test_missing_computer(self):
        keys = PLATFORMS - {"darwin-x86_64"}
        self.assertEqual(len(release_manifest.latest_key_problems(keys, PLATFORMS)), 1)


REPO = "owner/game"
FILES = {"dwe-launcher.AppImage", "dwe-launcher.deb"}


class BundleUrls(unittest.TestCase):
    def check(self, url):
        return release_manifest.bundle_url_problem("linux-x86_64", url, REPO, FILES)

    def test_download_url_of_a_folder_file(self):
        self.assertIsNone(self.check(f"https://github.com/{REPO}/releases/download/v0.1.0-beta.9/dwe-launcher.deb"))

    def test_api_asset_url(self):
        self.assertIsNone(self.check(f"https://api.github.com/repos/{REPO}/releases/assets/715331005"))

    def test_download_url_of_another_file(self):
        self.assertIsNotNone(self.check(f"https://github.com/{REPO}/releases/download/v0.1.0-beta.9/other.deb"))

    def test_other_repository(self):
        self.assertIsNotNone(self.check("https://github.com/someone/else/releases/download/v1/dwe-launcher.deb"))
        self.assertIsNotNone(self.check("https://api.github.com/repos/someone/else/releases/assets/1"))


if __name__ == "__main__":
    unittest.main()
