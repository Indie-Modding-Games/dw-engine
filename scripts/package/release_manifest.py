#!/usr/bin/env python3
"""Writes the manifests a release carries for the launcher (docs/modulos/apps/launcher.md).

    release_manifest.py game --package <file> --signature <file.sig> --version <v> --commit <c>
                             --extractor-version <n> [--platform linux-x86_64] [--merge <manifest>]
    release_manifest.py launcher --version <v> --notes <text> --platform <key> <file.sig> <url> ...
    release_manifest.py check --folder <dist> --pubkey <key.pub> --platform <key> ...
                              [--latest <latest.json>] [--repo <owner/name>] [--commit <sha>]
    release_manifest.py verify --pubkey <key.pub> <file> ...

game: game-manifest.json for a game package (its name, size, SHA-256 and signature); --merge adds
the package to an existing manifest of the same version (one manifest, several computers).
launcher: latest.json for the launcher's own updater (the Tauri updater format), one
--platform per bundle; the URL is the file's API URL in the release
(https://api.github.com/repos/<repo>/releases/assets/<id>), which the beta gateway
(services/beta-gateway) rewrites to its own address before a launcher sees it.
The manifest goes to the standard output.
check: the checks before a release is uploaded (scripts/release_local.sh): every file of the folder
but the manifests has its .sig, which the public key verifies (minisign, as the launcher checks
it), and a sane size; game-manifest.json and latest.json parse, name exactly the --platform keys and
match the files (size, SHA-256, signature); latest.json's URLs have the API form. latest.json may
also name a computer's installer kinds (<key>-<installer>, such as linux-x86_64-deb): Tauri's
updater takes that entry first, so a launcher installed from the .deb updates with a .deb, and
falls back to the plain key. It prints what it
checked and fails on the first list of problems.
verify: checks each file's .sig with the public key. Verifying needs the cryptography module.
"""
import argparse
import base64
import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

SEMVER = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$")
# A release file below this size is a broken build; GitHub refuses files of 2 GiB or more.
MIN_ASSET_SIZE = 1 << 20
MAX_ASSET_SIZE = (2 << 30) - 1


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


# The installer kinds Tauri's updater looks for before the plain <os>-<arch> key.
INSTALLERS = ("appimage", "deb", "rpm", "nsis", "msi", "app")


def latest_key_problems(keys, platforms) -> list[str]:
    """Why latest.json's keys do not fit the release's computers: every computer has its plain
    key, and an installer key (<key>-<installer>) only for one of them."""
    problems = []
    plain = set()
    for key in keys:
        base, _, installer = key.rpartition("-")
        if installer in INSTALLERS and base in platforms:
            continue
        plain.add(key)
    if plain != set(platforms):
        problems.append(f"latest.json: names {sorted(plain)}, not {sorted(platforms)}")
    return problems


def minisign_lines(text: str) -> list[str]:
    # Tauri's .sig and .pub files are base64 of minisign's text files.
    return base64.b64decode(text.strip()).decode("utf-8").splitlines()


def verify_signature(public_key: str, path: Path, signature: str) -> str:
    """Returns why the minisign signature (a .sig file's text) does not verify path, or ""."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    try:
        key = base64.b64decode(minisign_lines(public_key)[1])
        lines = minisign_lines(signature)
        sig = base64.b64decode(lines[1])
        trusted = lines[2].removeprefix("trusted comment: ").encode("utf-8")
        global_sig = base64.b64decode(lines[3])
    except (ValueError, IndexError) as error:
        return f"unreadable signature or key ({error})"
    if len(key) != 42 or key[:2] != b"Ed" or len(sig) != 74 or sig[:2] not in (b"Ed", b"ED"):
        return "not a minisign Ed25519 signature or key"
    if sig[2:10] != key[2:10]:
        return "signed by another key (key ids differ)"
    data = path.read_bytes()
    # "ED" signs the BLAKE2b-512 of the file (what Tauri writes), "Ed" the file itself.
    message = hashlib.blake2b(data).digest() if sig[:2] == b"ED" else data
    verifier = Ed25519PublicKey.from_public_bytes(key[10:])
    try:
        verifier.verify(sig[10:], message)
        verifier.verify(global_sig, sig[10:] + trusted)
    except InvalidSignature:
        return "the signature does not verify with the public key"
    return ""


def check(args) -> None:
    folder = Path(args.folder)
    public_key = Path(args.pubkey).read_text(encoding="utf-8")
    platforms = set(args.platform)
    problems = []
    signatures = {}
    files = sorted(p for p in folder.iterdir() if p.is_file())
    for path in files:
        if path.suffix == ".sig":
            if not path.with_suffix("").is_file():
                problems.append(f"{path.name}: a signature without its file")
            continue
        if path.suffix == ".json":
            continue
        size = path.stat().st_size
        if not MIN_ASSET_SIZE <= size <= MAX_ASSET_SIZE:
            problems.append(f"{path.name}: {size} bytes is not a sane size")
        sig = path.with_name(path.name + ".sig")
        if not sig.is_file():
            problems.append(f"{path.name}: no {sig.name}")
            continue
        signatures[path.name] = signature_text(sig)
        why = verify_signature(public_key, path, signatures[path.name])
        if why:
            problems.append(f"{path.name}: {why}")
        else:
            print(f"release_manifest: {path.name} ({size} bytes) signature verified")

    manifest_path = folder / "game-manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema") != 1 or not SEMVER.match(str(manifest.get("version"))):
            problems.append("game-manifest.json: no schema 1 or no semantic version")
        if not isinstance(manifest.get("extractor_version"), int):
            problems.append("game-manifest.json: no extractor_version")
        commit = str(manifest.get("commit", ""))
        if args.commit and (len(commit) < 7 or not args.commit.startswith(commit)):
            problems.append(f"game-manifest.json: commit {commit!r} is not {args.commit}")
        packages = manifest.get("packages", {})
        if set(packages) != platforms:
            problems.append(f"game-manifest.json: names {sorted(packages)}, not {sorted(platforms)}")
        for key, package in packages.items():
            path = folder / package["asset"]
            if not path.is_file():
                problems.append(f"game-manifest.json: {key} names {package['asset']}, not in the folder")
                continue
            if package["size"] != path.stat().st_size:
                problems.append(f"game-manifest.json: {key} has the wrong size")
            if package["sha256"] != hashlib.sha256(path.read_bytes()).hexdigest():
                problems.append(f"game-manifest.json: {key} has the wrong SHA-256")
            if package["signature"] != signatures.get(path.name):
                problems.append(f"game-manifest.json: {key} has another signature than {path.name}.sig")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        problems.append(f"game-manifest.json: unreadable ({error!r})")

    if args.latest:
        url = re.compile(rf"^https://api\.github\.com/repos/{re.escape(args.repo)}/releases/assets/\d+$")
        try:
            latest = json.loads(Path(args.latest).read_text(encoding="utf-8"))
            if not SEMVER.match(str(latest.get("version"))):
                problems.append("latest.json: no semantic version")
            bundles = latest.get("platforms", {})
            problems += latest_key_problems(bundles, platforms)
            for key, bundle in bundles.items():
                if bundle["signature"] not in signatures.values():
                    problems.append(f"latest.json: {key}'s signature is not one of the folder's files")
                if not url.match(bundle["url"]):
                    problems.append(f"latest.json: {key}'s URL {bundle['url']} is not an API asset URL")
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            problems.append(f"latest.json: unreadable ({error!r})")

    if problems:
        sys.exit("release_manifest: the release is not ready:\n  " + "\n  ".join(problems))
    print(f"release_manifest: {len(signatures)} files signed and verified, manifests name "
          f"{', '.join(sorted(platforms))}")


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
    c = commands.add_parser("check")
    c.add_argument("--folder", required=True)
    c.add_argument("--pubkey", required=True)
    c.add_argument("--platform", action="append", required=True)
    c.add_argument("--latest")
    c.add_argument("--repo", default="Indie-Modding-Games/dw-engine")
    c.add_argument("--commit", default="")
    v = commands.add_parser("verify")
    v.add_argument("--pubkey", required=True)
    v.add_argument("files", nargs="+")
    args = parser.parse_args()
    if args.command == "check":
        check(args)
        return
    if args.command == "verify":
        public_key = Path(args.pubkey).read_text(encoding="utf-8")
        for name in args.files:
            path = Path(name)
            sig = path.with_name(path.name + ".sig")
            if not sig.is_file():
                sys.exit(f"release_manifest: {path.name}: no {sig.name}")
            why = verify_signature(public_key, path, signature_text(sig))
            if why:
                sys.exit(f"release_manifest: {path.name}: {why}")
        return
    manifest = game(args) if args.command == "game" else launcher(args)
    json.dump(manifest, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
