#!/usr/bin/env python3
"""The game packages a pull request's CI builds and release.yml promotes (docs/modulos/apps/launcher.md).

  source                    the name a package built from HEAD carries (VERSION, bug reports):
                            tree-<12 hex digits of HEAD's tree>
  record --platform P --tarball F [--pr N] [--head SHA] --out source.json
                            writes what a package is: its computer, the tree it was built from,
                            the pull request and its head commit, the file's name, size and SHA-256
  find --repo R --tree T [--wait S]
                            prints the id of a same-repository ci.yml run whose package jobs built
                            every computer's package of tree T, or nothing; it asks again for up to
                            S seconds while the packages are still on their way
  verify --folder D --tree T --platform P ...
                            checks that D/game-<P>/ holds one package of T with its source.json,
                            for every P

A squash merge of a pull request that is up to date with main gives main a new commit with the
pull request's tree: the tree, not the commit, says the packages are main's. `find` reads GitHub's
API through `gh api` (GH_TOKEN).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLATFORMS = ("linux-x86_64", "windows-x86_64", "darwin-aarch64", "darwin-x86_64")
CI_WORKFLOW = ".github/workflows/ci.yml"
SOURCE_FILE = "source.json"


def artifact_name(platform: str, tree: str) -> str:
    return f"package-{platform}-{tree}"


def job_name(platform: str) -> str:
    """ci.yml's package job of a computer (its `name:`)."""
    return f"package ({platform})"


def source_name(tree: str) -> str:
    return f"tree-{tree[:12]}"


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True,
                          text=True).stdout.strip()


def head_tree() -> str:
    return git("rev-parse", "HEAD^{tree}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def record(platform: str, tarball: Path, tree: str, pr: int | None, head: str) -> dict:
    return {"schema": 1, "platform": platform, "tree": tree, "source": source_name(tree), "pr": pr,
            "head_sha": head, "package": tarball.name, "size": tarball.stat().st_size,
            "sha256": sha256(tarball)}


# gh api <path> as JSON; the tests replace it.
Api = Callable[[str], dict]


def gh_api(path: str) -> dict:
    out = subprocess.run(["gh", "api", path], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def run_problem(api: Api, repo: str, run_id: int, platforms) -> str | None:
    """Why a run's packages cannot be promoted, or None: a ci.yml run of a pull request from this
    repository (a fork's run has no say in a release), whose package job of every computer
    succeeded."""
    run = api(f"repos/{repo}/actions/runs/{run_id}")
    if run.get("path") != CI_WORKFLOW:
        return f"run {run_id} is {run.get('path')}, not {CI_WORKFLOW}"
    if run.get("event") != "pull_request":
        return f"run {run_id} ran on {run.get('event')}, not a pull request"
    if (run.get("head_repository") or {}).get("full_name") != repo:
        return f"run {run_id} is a fork's"
    jobs = api(f"repos/{repo}/actions/runs/{run_id}/jobs?filter=latest&per_page=100").get("jobs", [])
    outcome = {job.get("name"): job.get("conclusion") for job in jobs}
    failed = [p for p in platforms if outcome.get(job_name(p)) != "success"]
    if failed:
        return f"run {run_id}: the package job of {', '.join(failed)} did not succeed"
    return None


def find_run(api: Api, repo: str, tree: str, platforms=PLATFORMS, log=print) -> int | None:
    """The newest run with every computer's package of the tree, or None."""
    runs: dict[int, set[str]] = {}
    created: dict[int, str] = {}
    for platform in platforms:
        found = api(f"repos/{repo}/actions/artifacts?name={artifact_name(platform, tree)}&per_page=100")
        for artifact in found.get("artifacts", []):
            run = artifact.get("workflow_run") or {}
            if artifact.get("expired") or run.get("id") is None:
                continue
            # A fork's pull request runs in this repository but from the fork's head.
            if run.get("head_repository_id") != run.get("repository_id"):
                continue
            runs.setdefault(run["id"], set()).add(platform)
            created[run["id"]] = max(created.get(run["id"], ""), artifact.get("created_at", ""))
    complete = [r for r, have in runs.items() if have == set(platforms)]
    for run_id in sorted(complete, key=lambda r: (created[r], r), reverse=True):
        problem = run_problem(api, repo, run_id, platforms)
        if problem is None:
            return run_id
        log(problem)
    partial = {r: sorted(set(platforms) - have) for r, have in runs.items() if have != set(platforms)}
    for run_id, missing in partial.items():
        log(f"run {run_id} has no package yet for {', '.join(missing)}")
    return None


def find_with_wait(api: Api, repo: str, tree: str, wait: float, every: float = 15,
                   sleep=time.sleep, clock=time.monotonic, log=print) -> int | None:
    """find_run, asked again until `wait` seconds have passed: a package job that is still
    uploading when main's run starts is worth a short wait, not a full build."""
    deadline = clock() + wait
    while True:
        try:
            run_id = find_run(api, repo, tree, log=log)
        except (subprocess.CalledProcessError, ValueError, KeyError, TypeError) as error:
            # GitHub's API failing is no reason to stop a release: it is built instead.
            log(f"cannot ask GitHub ({error})")
            run_id = None
        if run_id is not None or clock() + every > deadline:
            return run_id
        log(f"no complete set of packages for tree {tree[:12]} yet; asking again in {every:.0f} s")
        sleep(every)


def verify(folder: Path, tree: str, platforms) -> list[str]:
    """Why the packages in folder/game-<platform>/ are not the tree's, or [] when they all are."""
    problems = []
    for platform in platforms:
        where = folder / f"game-{platform}"
        tarballs = sorted(where.glob(f"dwe-beta-*-{platform}.tar.xz"))
        if len(tarballs) != 1:
            problems.append(f"{platform}: expected one package in {where}, found {len(tarballs)}")
            continue
        try:
            source = json.loads((where / SOURCE_FILE).read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            problems.append(f"{platform}: no readable {SOURCE_FILE} ({error})")
            continue
        tarball = tarballs[0]
        if source.get("platform") != platform:
            problems.append(f"{platform}: {SOURCE_FILE} is {source.get('platform')}'s")
        if source.get("tree") != tree:
            problems.append(f"{platform}: built from tree {source.get('tree')}, not {tree}")
        if source.get("package") != tarball.name:
            problems.append(f"{platform}: {SOURCE_FILE} names {source.get('package')}, not {tarball.name}")
        elif source.get("sha256") != sha256(tarball):
            problems.append(f"{platform}: {tarball.name} is not the file that was built (SHA-256)")
    return problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("source")
    rec = commands.add_parser("record")
    rec.add_argument("--platform", choices=PLATFORMS, required=True)
    rec.add_argument("--tarball", type=Path, required=True)
    rec.add_argument("--pr", type=int)
    rec.add_argument("--head", default="")
    rec.add_argument("--out", type=Path, required=True)
    find = commands.add_parser("find")
    find.add_argument("--repo", required=True)
    find.add_argument("--tree", required=True)
    find.add_argument("--wait", type=float, default=0)
    check = commands.add_parser("verify")
    check.add_argument("--folder", type=Path, required=True)
    check.add_argument("--tree", required=True)
    check.add_argument("--platform", action="append", choices=PLATFORMS, required=True)
    args = parser.parse_args(argv)

    if args.command == "source":
        print(source_name(head_tree()))
        return 0
    if args.command == "record":
        data = record(args.platform, args.tarball, head_tree(), args.pr, args.head or git("rev-parse", "HEAD"))
        args.out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"packages: {args.tarball.name} is tree {data['tree'][:12]} ({args.platform})")
        return 0
    if args.command == "find":
        def log(text: str) -> None:
            print(f"packages: {text}", file=sys.stderr)

        run_id = find_with_wait(gh_api, args.repo, args.tree, args.wait, log=log)
        if run_id is not None:
            print(run_id)
        return 0
    problems = verify(args.folder, args.tree, args.platform)
    if problems:
        print("packages: not the tree's packages:\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    print(f"packages: {', '.join(args.platform)} built from tree {args.tree[:12]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
