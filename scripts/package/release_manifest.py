#!/usr/bin/env python3
"""Writes the manifests a release carries for the launcher (docs/modulos/apps/launcher.md).

    release_manifest.py game --package <file> --signature <file.sig> --version <v> --commit <c>
                             --extractor-version <n> [--platform linux-x86_64] [--merge <manifest>]
    release_manifest.py launcher --version <v> --notes <text> --platform <key> <file.sig> <url> ...

game: game-manifest.json for a game package (its name, size, SHA-256 and signature); --merge adds
the package to an existing manifest of the same version (one manifest, several computers).
launcher: latest.json for the launcher's own updater (the Tauri updater format), one
--platform per bundle; the URL is the file's API URL in the release, so a private repository
works with a token.
The manifest goes to the standard output.
"""
import argparse
import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

SEMVER = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$")


def signature_text(path: Path) -> str:
    # tauri signer sign writes the .sig file already in base64, as latest.json wants it.
    return path.read_text(encoding="utf-8").strip()


def game(args) -> dict:
    if not SEMVER.match(args.version):
        sys.exit(f"release_manifest: {args.version} is not a semantic version")
    package = Path(args.package)
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    manifest = {"schema": 1, "version": args.version, "commit": args.commit,
                "extractor_version": args.extractor_version,
                "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                "packages": {}}
    if args.merge:
        manifest = json.loads(Path(args.merge).read_text(encoding="utf-8"))
        if manifest.get("version") != args.version:
            sys.exit("release_manifest: --merge names a manifest of another version")
    manifest["packages"][args.platform] = {"asset": package.name, "size": package.stat().st_size,
                                           "sha256": digest, "signature": signature_text(Path(args.signature))}
    return manifest


def launcher(args) -> dict:
    platforms = {}
    for key, sig, url in args.platform:
        platforms[key] = {"signature": signature_text(Path(sig)), "url": url}
    return {"version": args.version, "notes": args.notes,
            "pub_date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "platforms": platforms}


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    g = commands.add_parser("game")
    g.add_argument("--package", required=True)
    g.add_argument("--signature", required=True)
    g.add_argument("--version", required=True)
    g.add_argument("--commit", default="")
    g.add_argument("--extractor-version", type=int, required=True)
    g.add_argument("--platform", default="linux-x86_64")
    g.add_argument("--merge")
    l = commands.add_parser("launcher")
    l.add_argument("--version", required=True)
    l.add_argument("--notes", default="")
    l.add_argument("--platform", nargs=3, action="append", default=[], metavar=("KEY", "SIG", "URL"))
    args = parser.parse_args()
    manifest = game(args) if args.command == "game" else launcher(args)
    json.dump(manifest, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
