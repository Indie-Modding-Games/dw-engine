#!/usr/bin/env bash
# The development image (dev) or the launcher's (launcher) for a CI job: pulled from GitHub's
# container registry when it holds the one built from these .devcontainer files, built otherwise.
# With --push, an image built here is pushed for the next runs (release.yml, on main; a pull
# request only pulls). The image gets the local name scripts/dev.sh and scripts/launcher.sh use.
#
# Usage: scripts/ci/image.sh dev|launcher [--push]
# Needs GITHUB_REPOSITORY; GH_TOKEN and GITHUB_ACTOR to log in to the registry.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
kind="${1:?usage: image.sh dev|launcher [--push]}"
push=false
[[ "${2:-}" == "--push" ]] && push=true
case "$kind" in
  dev)
    dockerfile="Dockerfile"
    local_name="${DWE_IMAGE:-dwe-dev}"
    package="dwe-dev"
    ;;
  launcher)
    dockerfile="launcher.Dockerfile"
    local_name="${DWE_LAUNCHER_IMAGE:-dwe-launcher-dev}"
    package="dwe-launcher-dev"
    ;;
  *)
    echo "image.sh: dev or launcher, not $kind" >&2
    exit 1
    ;;
esac
repo="${GITHUB_REPOSITORY:?set GITHUB_REPOSITORY to owner/name}"
# The tag is the hash of every file in the image's build folder, as git has them.
tag="$(git -C "$root" ls-files -s .devcontainer | sha256sum | cut -c1-16)"
remote="ghcr.io/${repo%%/*}/$package:$tag"
remote="${remote,,}"

if [[ -n "${GH_TOKEN:-}" && -n "${GITHUB_ACTOR:-}" ]]; then
  printf '%s' "$GH_TOKEN" \
    | docker login ghcr.io -u "$GITHUB_ACTOR" --password-stdin >/dev/null 2>&1 \
    || echo "image.sh: could not log in to ghcr.io; trying without"
fi
if docker pull -q "$remote" >/dev/null 2>&1; then
  docker tag "$remote" "$local_name"
  echo "image.sh: $local_name is $remote"
  exit 0
fi
echo "image.sh: $remote is not in the registry; building it"
# The label links the package to the repository, so it takes the repository's access.
docker build -f "$root/.devcontainer/$dockerfile" -t "$local_name" \
  --label "org.opencontainers.image.source=https://github.com/$repo" "$root/.devcontainer"
if $push; then
  docker tag "$local_name" "$remote"
  docker push -q "$remote" >/dev/null && echo "image.sh: pushed $remote" \
    || echo "image.sh: could not push $remote; the next run builds it again"
fi
