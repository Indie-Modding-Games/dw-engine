"""Tests of the promotion of a pull request's packages (scripts/ci/packages.py)."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import packages  # noqa: E402

REPO = "owner/game"
TREE = "a" * 40
OTHER_TREE = "b" * 40
REPO_ID = 1426406232
FORK_ID = 999


def artifact(platform: str, run_id: int, *, head_repo: int = REPO_ID, expired: bool = False,
             created: str = "2026-11-10T16:32:51Z", tree: str = TREE) -> dict:
    """An entry of GET /repos/{repo}/actions/artifacts?name=..., as GitHub returns it."""
    return {
        "id": run_id * 10, "name": packages.artifact_name(platform, tree), "size_in_bytes": 53899754,
        "expired": expired, "created_at": created, "updated_at": created,
        "workflow_run": {"id": run_id, "repository_id": REPO_ID, "head_repository_id": head_repo,
                         "head_branch": "feature", "head_sha": "c" * 40},
    }


def run(run_id: int, *, path: str = packages.CI_WORKFLOW, event: str = "pull_request",
        head_repo: str = REPO) -> dict:
    """GET /repos/{repo}/actions/runs/{id} (the fields read)."""
    return {"id": run_id, "name": "CI", "path": path, "event": event, "status": "completed",
            "conclusion": "success", "head_repository": {"full_name": head_repo},
            "repository": {"full_name": REPO}}


def jobs(*, failed: tuple[str, ...] = (), missing: tuple[str, ...] = ()) -> dict:
    """GET /repos/{repo}/actions/runs/{id}/jobs: the package jobs and some others."""
    listed = [{"name": "linux", "status": "completed", "conclusion": "success"},
              {"name": "mapper", "status": "completed", "conclusion": "failure"}]
    for platform in packages.PLATFORMS:
        if platform in missing:
            continue
        conclusion = "failure" if platform in failed else "success"
        listed.append({"name": packages.job_name(platform), "status": "completed",
                       "conclusion": conclusion})
    return {"total_count": len(listed), "jobs": listed}


class FakeApi:
    """gh api by path; an unknown path fails like gh does on a 404."""

    def __init__(self, responses: dict[str, dict]):
        self.responses = responses
        self.calls: list[str] = []

    def __call__(self, path: str) -> dict:
        self.calls.append(path)
        if path not in self.responses:
            raise subprocess.CalledProcessError(1, ["gh", "api", path])
        return self.responses[path]


def artifacts_path(platform: str, tree: str = TREE) -> str:
    return f"repos/{REPO}/actions/artifacts?name={packages.artifact_name(platform, tree)}&per_page=100"


def runs_path(run_id: int) -> str:
    return f"repos/{REPO}/actions/runs/{run_id}"


def jobs_path(run_id: int) -> str:
    return f"repos/{REPO}/actions/runs/{run_id}/jobs?filter=latest&per_page=100"


def api_with(by_platform: dict[str, list[dict]], runs: dict[int, dict], run_jobs: dict[int, dict]) -> FakeApi:
    responses = {}
    for platform in packages.PLATFORMS:
        found = by_platform.get(platform, [])
        responses[artifacts_path(platform)] = {"total_count": len(found), "artifacts": found}
    for run_id, data in runs.items():
        responses[runs_path(run_id)] = data
    for run_id, data in run_jobs.items():
        responses[jobs_path(run_id)] = data
    return FakeApi(responses)


def every_platform(run_id: int, **kwargs) -> dict[str, list[dict]]:
    return {p: [artifact(p, run_id, **kwargs)] for p in packages.PLATFORMS}


class FindRun(unittest.TestCase):
    def setUp(self):
        self.log: list[str] = []

    def find(self, api) -> int | None:
        return packages.find_run(api, REPO, TREE, log=self.log.append)

    def test_a_run_with_every_package_is_found(self):
        api = api_with(every_platform(7), {7: run(7)}, {7: jobs()})
        self.assertEqual(self.find(api), 7)

    def test_a_failed_job_elsewhere_in_the_run_does_not_matter(self):
        # jobs() has a failed mapper job: only the package jobs count.
        api = api_with(every_platform(7), {7: run(7)}, {7: jobs()})
        self.assertEqual(self.find(api), 7)

    def test_no_artifacts_means_no_run(self):
        api = api_with({}, {}, {})
        self.assertIsNone(self.find(api))

    def test_a_missing_computer_means_no_run(self):
        found = every_platform(7)
        del found["darwin-x86_64"]
        api = api_with(found, {7: run(7)}, {7: jobs()})
        self.assertIsNone(self.find(api))
        self.assertIn("darwin-x86_64", "\n".join(self.log))

    def test_packages_of_two_runs_are_not_mixed(self):
        found = every_platform(7)
        found["darwin-x86_64"] = [artifact("darwin-x86_64", 8)]
        api = api_with(found, {7: run(7), 8: run(8)}, {7: jobs(), 8: jobs()})
        self.assertIsNone(self.find(api))

    def test_a_forks_packages_are_never_promoted(self):
        api = api_with(every_platform(7, head_repo=FORK_ID), {7: run(7)}, {7: jobs()})
        self.assertIsNone(self.find(api))
        api = api_with(every_platform(7), {7: run(7, head_repo="someone/game")}, {7: jobs()})
        self.assertIsNone(self.find(api))

    def test_expired_packages_are_skipped(self):
        api = api_with(every_platform(7, expired=True), {7: run(7)}, {7: jobs()})
        self.assertIsNone(self.find(api))

    def test_only_ci_pull_request_runs(self):
        api = api_with(every_platform(7), {7: run(7, path=".github/workflows/release.yml")}, {7: jobs()})
        self.assertIsNone(self.find(api))
        api = api_with(every_platform(7), {7: run(7, event="push")}, {7: jobs()})
        self.assertIsNone(self.find(api))

    def test_a_failed_package_job_rejects_the_run(self):
        api = api_with(every_platform(7), {7: run(7)}, {7: jobs(failed=("windows-x86_64",))})
        self.assertIsNone(self.find(api))
        self.assertIn("windows-x86_64", "\n".join(self.log))

    def test_the_newest_good_run_wins(self):
        found = {p: [artifact(p, 7, created="2026-11-10T10:00:00Z"),
                     artifact(p, 9, created="2026-11-10T12:00:00Z")] for p in packages.PLATFORMS}
        api = api_with(found, {7: run(7), 9: run(9)}, {7: jobs(), 9: jobs()})
        self.assertEqual(self.find(api), 9)
        # The newest is rejected: the older good one is next.
        api = api_with(found, {7: run(7), 9: run(9)}, {7: jobs(), 9: jobs(failed=("linux-x86_64",))})
        self.assertEqual(self.find(api), 7)

    def test_it_asks_for_the_trees_artifacts_by_name(self):
        api = api_with(every_platform(7), {7: run(7)}, {7: jobs()})
        self.find(api)
        self.assertIn(artifacts_path("linux-x86_64"), api.calls)
        self.assertTrue(all(OTHER_TREE not in call for call in api.calls))


class FindWithWait(unittest.TestCase):
    def test_it_waits_for_the_last_package_then_finds_it(self):
        clock = [0.0]
        late = every_platform(7)
        del late["darwin-x86_64"]
        incomplete = api_with(late, {7: run(7)}, {7: jobs()})
        complete = api_with(every_platform(7), {7: run(7)}, {7: jobs()})
        asked = []

        def api(path):
            asked.append(path)
            return (complete if clock[0] >= 30 else incomplete)(path)

        def sleep(seconds):
            clock[0] += seconds

        run_id = packages.find_with_wait(api, REPO, TREE, wait=60, every=15, sleep=sleep,
                                         clock=lambda: clock[0], log=lambda _: None)
        self.assertEqual(run_id, 7)
        self.assertEqual(clock[0], 30)

    def test_it_gives_up_after_the_wait(self):
        clock = [0.0]

        def sleep(seconds):
            clock[0] += seconds

        run_id = packages.find_with_wait(api_with({}, {}, {}), REPO, TREE, wait=60, every=15, sleep=sleep,
                                         clock=lambda: clock[0], log=lambda _: None)
        self.assertIsNone(run_id)
        self.assertLessEqual(clock[0], 60)

    def test_an_api_failure_falls_back_instead_of_failing(self):
        def broken(path):
            raise subprocess.CalledProcessError(1, ["gh", "api", path])

        log = []
        run_id = packages.find_with_wait(broken, REPO, TREE, wait=0, sleep=lambda _: None,
                                         clock=lambda: 0.0, log=log.append)
        self.assertIsNone(run_id)
        self.assertIn("cannot ask GitHub", "\n".join(log))


class Verify(unittest.TestCase):
    def setUp(self):
        self.folder = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.folder)

    def package(self, platform: str, tree: str = TREE, body: bytes = b"package") -> Path:
        where = self.folder / f"game-{platform}"
        where.mkdir(parents=True, exist_ok=True)
        tarball = where / f"dwe-beta-0.1.0-tree-{tree[:12]}-{platform}.tar.xz"
        tarball.write_bytes(body)
        source = packages.record(platform, tarball, tree, 12, "c" * 40)
        (where / packages.SOURCE_FILE).write_text(json.dumps(source), encoding="utf-8")
        return tarball

    def test_the_trees_packages_pass(self):
        for platform in packages.PLATFORMS:
            self.package(platform)
        self.assertEqual(packages.verify(self.folder, TREE, packages.PLATFORMS), [])

    def test_another_tree_fails(self):
        self.package("linux-x86_64", tree=OTHER_TREE)
        problems = packages.verify(self.folder, TREE, ["linux-x86_64"])
        self.assertTrue(any("not " + TREE in p for p in problems), problems)

    def test_a_changed_file_fails(self):
        self.package("linux-x86_64").write_bytes(b"something else")
        problems = packages.verify(self.folder, TREE, ["linux-x86_64"])
        self.assertTrue(any("SHA-256" in p for p in problems), problems)

    def test_a_missing_package_or_record_fails(self):
        self.assertTrue(packages.verify(self.folder, TREE, ["darwin-aarch64"]))
        self.package("darwin-aarch64")
        (self.folder / "game-darwin-aarch64" / packages.SOURCE_FILE).unlink()
        problems = packages.verify(self.folder, TREE, ["darwin-aarch64"])
        self.assertTrue(any("source.json" in p for p in problems), problems)

    def test_the_record(self):
        tarball = self.package("windows-x86_64")
        source = json.loads((tarball.parent / packages.SOURCE_FILE).read_text(encoding="utf-8"))
        self.assertEqual(source["source"], "tree-" + TREE[:12])
        self.assertEqual((source["pr"], source["size"]), (12, len(b"package")))


if __name__ == "__main__":
    unittest.main()
