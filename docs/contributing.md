# How to contribute

## Before you start

1. Read the [documentation home page](index.md) and the page for your role.
2. Read the [legal asset policy](legal/assets.md). It is not optional: a single file from the original game in the
   repository puts the whole project at risk.

## Workflow

- **A single branch, `main`.** Work starts from a branch off `main` and goes into `main` through a pull request. The
  merge is always a *squash*: one commit per pull request, with the pull request title as the message (write it like
  a commit). The branch is deleted automatically on merge.
- **A pull request is a batch.** Each pull request groups several functions and fixes that are already tested, not one
  per small fix: every merge publishes a test release, so one pull request is one release. Inside the branch there can
  be as many commits as needed; the pull request description lists what it brings.
- **Required checks** (`.github/workflows/ci.yml`), only on pull requests and only for the newest commit:

  | Check | What it looks at | Time with caches |
  |---|---|---|
  | `linux` | format and lint of the files the pull request changes, build (debug, warnings as errors) and all tests | ~8–10 min |
  | `windows` | the same build for Windows (cross-compiled with clang-cl), tests built but not run | ~5–7 min |
  | `launcher-version` | if the launcher changed since the newest published release, its version goes up ([The launcher version](modules/apps/launcher.md#publishing-a-release)) | seconds |

  A pull request that only touches documentation builds nothing: `linux` and `windows` show as skipped, which counts
  as passed. When the launcher, the gateway or the release scripts change, `launcher` also runs its tests.
  If the merge is going to publish a release, the `package (<computer>)` jobs also build the game packages for the
  four computers (they are not required); the merge publishes those same files in 2–4 min instead of building them
  again. For that, the pull request has to be **up to date with `main`** (`main` requires it) and it is best to merge
  it once its `package` jobs have finished: otherwise the merge builds everything again (~10–12 min)
  ([Test release](modules/apps/launcher.md#test-release-every-merge-into-main)).
  Outside of pull requests are clang-tidy, the documentation site and the tests on a real Windows
  (`platform-check.yml`, every Monday): `scripts/dev.sh check-full` runs them locally.
- **Releases:** every merge into `main` publishes a test release on its own (the launcher's "Testing" channel); one that
  has already been tested is promoted to stable by hand, with the same files. An urgent fix follows the same path.
  Details in [Publishing a release](modules/apps/launcher.md#publishing-a-release).
- **Commits:** [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/), `type(scope): what changes` (see
  [commit conventions](conventions/commits.md)).

## A PR is done when

- it passes the required checks; locally, `scripts/dev.sh check-commit` builds and runs the tests and
  `scripts/dev.sh check-full` adds format, lint, clang-tidy and the documentation site
  ([Setting up the environment](getting-started/setup.md#check-before-a-pr)); while you work,
  `scripts/dev.sh test --affected` runs only the tests of the packages you touched and of those that depend on them,
  and `scripts/dev.sh test --filter <package>` those of one package ([build and test a package](getting-started/setup.md#build-and-test-one-package));
- if the launcher changes (`apps/launcher/ui` or `apps/launcher/src-tauri`), its version goes up in `tauri.conf.json`,
  `Cargo.toml` and `Cargo.lock`;
- it brings tests for the behavior it changes;
- it **updates `docs/`** if an architecture, an API, a format or a rule changes;
- it brings an ADR if it makes an architecture decision (see [how ADRs work](adr/index.md));
- it adds no original asset ([legal policy](legal/assets.md)).

## Language

- Code, identifiers, comments and commit messages: **English**.
- Documentation: **English**, with the Spanish translation in `docs/es/` (same file names and headings).

## Code comments

Keep comments in code rare: the docs explain how things work. No file or function header comments, no decomp or issue
citations and no step-by-step narration; a short line is fine only where the code would mislead without its "why".
