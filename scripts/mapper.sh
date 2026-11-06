#!/usr/bin/env bash
# Builds and tests the map editor (apps/mapper, docs/modulos/apps/mapper.md) and its crates
# (packages/rust) in the launcher's image, so the host needs nothing but Docker. The tests need no
# GPU and no screen; the program it builds runs on the host.
#
# Usage: scripts/mapper.sh <command> [args...]
#   image           build the image (dwe-launcher-dev, .devcontainer/launcher.Dockerfile)
#   test            cargo fmt --check, cargo test and clippy (-D warnings) over the workspace
#   build           the release program, build/mapper/release/dwe-mapper
#   run [args]      build, then run the program on the host with these arguments
#   shell [cmd]     a shell (or a command) in the image, in apps/mapper
#   camera-vectors  regenerate packages/rust/dwe-map/tests/data/camera_vectors.expected with the
#                   engine's map camera (C++, in the dwe-dev image)
# Environment:
#   DWE_MAPPER_CARGO_DIR  host folder for cargo's downloads (registry/ and git/); default: the
#                         dwe-mapper-cargo-* volumes. CI caches it
#   DWE_BUILD_SLOTS       a folder of slot locks shared by every heavy build of the machine (at most
#                         two at a time); without it the build does not wait for a slot
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${DWE_MAPPER_IMAGE:-dwe-launcher-dev}"
cpp_image="${DWE_DEV_IMAGE:-dwe-dev}"
app="$root/apps/mapper"
target_dir="$root/build/mapper"

usage() {
  sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

# One of two machine-wide build slots, held until this script ends.
take_slot() {
  [[ -n "${DWE_BUILD_SLOTS:-}" ]] || return 0
  mkdir -p "$DWE_BUILD_SLOTS"
  local slot try
  for slot in 1 2; do
    exec {try}>"$DWE_BUILD_SLOTS/slot$slot"
    if flock -n "$try"; then return 0; fi
    exec {try}>&-
  done
  exec {try}>"$DWE_BUILD_SLOTS/slot$((RANDOM % 2 + 1))"
  echo "mapper.sh: both build slots busy, waiting (up to ${DBUILD_WAIT:-3600}s)" >&2
  flock -w "${DBUILD_WAIT:-3600}" "$try" || {
    echo "mapper.sh: no build slot" >&2
    exit 75
  }
}

# Host folders (made here, so they belong to the host user) or the named volumes.
cargo_registry="dwe-mapper-cargo-registry"
cargo_git="dwe-mapper-cargo-git"
if [[ -n "${DWE_MAPPER_CARGO_DIR:-}" ]]; then
  mkdir -p "$DWE_MAPPER_CARGO_DIR/registry" "$DWE_MAPPER_CARGO_DIR/git"
  cargo_registry="$(cd "$DWE_MAPPER_CARGO_DIR" && pwd)/registry"
  cargo_git="$(cd "$DWE_MAPPER_CARGO_DIR" && pwd)/git"
fi

run() {
  local tty=()
  [[ -t 0 && -t 1 ]] && tty=(-it)
  docker run --rm "${tty[@]}" -u "$(id -u):$(id -g)" -e HOME=/tmp/home \
    -e CARGO_TARGET_DIR="$target_dir" \
    -v "$cargo_registry:/opt/cargo/registry" -v "$cargo_git:/opt/cargo/git" \
    -v "$root":"$root" -w "$app" "$image" "$@"
}

# The engine's map camera (packages/engine/render) over the test inputs, built with glm in a
# throwaway container: the expected answers of the camera tests.
camera_vectors() {
  local data="packages/rust/dwe-map/tests/data"
  docker run --rm -u root -e DEBIAN_FRONTEND=noninteractive -v "$root":/src:ro "$cpp_image" \
    bash -c '
    set -euo pipefail
    apt-get update -qq >/dev/null && apt-get install -y -qq libglm-dev >/dev/null
    cd /src
    clang++ -std=c++2c -O1 -DGLM_FORCE_RADIANS -DGLM_FORCE_DEPTH_ZERO_TO_ONE \
      -Ipackages/engine/render/include -Ipackages/engine/assets/include \
      -Ipackages/engine/core/include \
      packages/rust/dwe-map/tests/cpp/camera_vectors.cpp \
      packages/engine/render/src/map_camera.cpp -o /tmp/camera_vectors
    /tmp/camera_vectors < "$1/camera_vectors.txt"' -- "$data" >"$root/$data/camera_vectors.expected"
  echo "mapper.sh: $data/camera_vectors.expected"
}

command="${1:-}"
shift || true
case "$command" in
  image)
    docker build -f "$root/.devcontainer/launcher.Dockerfile" -t "$image" "$root/.devcontainer"
    ;;
  test)
    take_slot
    run bash -c 'cargo fmt --all --check &&
      cargo test --locked --workspace "$@" &&
      cargo clippy --locked --workspace --all-targets -- -D warnings' -- "$@"
    ;;
  build)
    take_slot
    run cargo build --locked --release "$@"
    echo "mapper.sh: $target_dir/release/dwe-mapper"
    ;;
  run)
    # The slot is only held while building (the subshell keeps it).
    (
      take_slot
      run cargo build --locked --release
    )
    exec "$target_dir/release/dwe-mapper" "$@"
    ;;
  shell)
    if (($#)); then run bash -c "$*"; else run bash; fi
    ;;
  camera-vectors)
    take_slot
    camera_vectors
    ;;
  -h | --help | "")
    usage
    ;;
  *)
    echo "mapper.sh: unknown command $command" >&2
    usage >&2
    exit 2
    ;;
esac
