# Commits and pull requests

## Commits

- **One pull request, one commit on `main`.** The *squash* merge joins the branch's commits, so as many as needed are
  made inside it. The stash is shared between worktrees, so it is not used to save progress.
- **Before opening the pull request, run `scripts/dev.sh check-commit`**; `check-full` is optional and looks at more
  than the CI.
- The message follows [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) in **English**: `type(scope): description`, with the
  description lowercase and in the imperative: `feat(vfs): add the virtual file system`,
  `fix(map): load map collision from packs`. Types: feat, fix, docs, refactor, test, chore, ci, build, perf, style.
- The body, if any, and the PR description are short: a few bullets at most. The PR title is the commit line.
- No franchise, console or company names in commit messages or in PR titles. Generic terms are used.

## Pull requests

- Short branches from `main`. It is squash-merged, and the resulting commit carries the PR title.
- **The description, as short as possible:** one or two sentences on what it does and one line on how it was checked.
- The CI checks (`linux`, `windows`, `launcher-version`) must pass; a review approval is not required.
- If the PR changes the architecture, an API, a format or a rule, it brings its change in `docs/` and, if needed, an ADR.
