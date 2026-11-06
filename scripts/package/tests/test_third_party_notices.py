"""Tests of THIRD_PARTY_NOTICES.txt (scripts/package/third_party_notices.py)."""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import third_party_notices as notices  # noqa: E402

MIT = "MIT License\n\nCopyright (c) 2020 Someone\n\nPermission is hereby granted, free of charge, ...\n"
APACHE = "Apache License\nVersion 2.0, January 2004\n"


class Game(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.licenses = self.folder / "licenses"
        self.licenses.mkdir()

    def write(self, name, text):
        path = self.licenses / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def run_game(self):
        args = argparse.Namespace(licenses=str(self.licenses), out=str(self.folder / "THIRD_PARTY_NOTICES.txt"))
        with contextlib.redirect_stderr(io.StringIO()):
            return notices.game(args)

    def text(self):
        return (self.folder / "THIRD_PARTY_NOTICES.txt").read_text(encoding="utf-8")

    def test_every_text_and_the_credits(self):
        self.write("zlib.txt", "zlib licence text\n")
        self.write("freetype.txt", "The FreeType Project LICENSE\n")
        self.write("vulkan-loader.txt", APACHE)
        self.write("lavapipe/mesa.txt", "mesa text\n")
        self.assertEqual(self.run_game(), 0)
        text = self.text()
        for part in ("zlib licence text", "The FreeType Project LICENSE", "mesa text", "lavapipe/mesa",
                     "Portions of this software are copyright", "vulkan-loader:\nNo NOTICE file upstream.",
                     "Apache License 2.0 (the full text)"):
            self.assertIn(part, text)

    def test_an_unknown_apache_component_fails(self):
        self.write("new-port.txt", APACHE)
        self.assertEqual(self.run_game(), 1)

    def test_same_texts_are_written_once(self):
        self.write("a.txt", MIT)
        self.write("b.txt", MIT.replace("\n\n", "\n \n"))
        self.assertEqual(self.run_game(), 0)
        self.assertEqual(self.text().count("Permission is hereby granted"), 1)
        self.assertIn("a, b\n", self.text())

    def test_check(self):
        self.write("zlib.txt", "zlib licence text\n")
        self.assertEqual(self.run_game(), 0)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(notices.check(argparse.Namespace(folder=str(self.folder))), 1)
            (self.folder / "LICENSE").write_text(MIT, encoding="utf-8")
            self.assertEqual(notices.check(argparse.Namespace(folder=str(self.folder))), 0)
            (self.folder / "THIRD_PARTY_NOTICES.txt").unlink()
            self.assertEqual(notices.check(argparse.Namespace(folder=str(self.folder))), 1)


class Crates(unittest.TestCase):
    def test_normal_dependencies_only(self):
        def package(name):
            return {"id": name, "name": name, "version": "1.0.0", "manifest_path": f"/x/{name}/Cargo.toml"}

        def dep(name, kind=None):
            return {"pkg": name, "dep_kinds": [{"kind": kind}]}

        metadata = {
            "workspace_members": ["app"],
            "packages": [package(n) for n in ("app", "serde", "itoa", "cc", "doctest")],
            "resolve": {"nodes": [
                {"id": "app", "deps": [dep("serde"), dep("cc", "build"), dep("doctest", "dev")]},
                {"id": "serde", "deps": [dep("itoa")]},
                {"id": "itoa", "deps": []},
                {"id": "cc", "deps": []},
                {"id": "doctest", "deps": []},
            ]},
        }
        self.assertEqual([p["name"] for p in notices.shipped_crates(metadata)], ["itoa", "serde"])

    def test_licence_files(self):
        for name in ("LICENSE", "LICENSE-MIT", "license.txt", "COPYING", "NOTICE", "UNLICENSE", "COPYRIGHT"):
            self.assertTrue(notices.LICENCE_FILE.match(name), name)
        for name in ("README.md", "licensed.rs", "Cargo.toml"):
            self.assertFalse(notices.LICENCE_FILE.match(name), name)


if __name__ == "__main__":
    unittest.main()
