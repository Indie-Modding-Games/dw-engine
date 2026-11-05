#!/usr/bin/env bash
# Builds and tests the desktop launcher (apps/launcher, docs/modulos/apps/launcher.md) in its own
# image, so the host needs nothing but Docker. The launcher it builds runs on the host.
#
# Usage: scripts/launcher.sh <command> [args...]
#   image          build the image (dwe-launcher-dev, .devcontainer/launcher.Dockerfile)
#   test           cargo test (unit tests and the fake release server test) and clippy
#   build          the release program, build/launcher/release/dwe-launcher (no bundle)
#   bundle         the AppImage, the .deb and the updater files (needs TAURI_SIGNING_PRIVATE_KEY)
#   bundle-windows the Windows installer (NSIS, per user, WebView2 bootstrapper inside) and its
#                  signature, cross-built with cargo-xwin, in build/launcher/x86_64-pc-windows-msvc
#   shell [cmd]    a shell (or a command) in the image, in apps/launcher/src-tauri
# Environment:
#   DWE_LAUNCHER_PUBKEY   base64 public key to build with (the updates' signatures are checked
#                         against it); without it the build refuses every update
#   DWE_LAUNCHER_GATEWAY  the beta gateway's URL to build with (services/beta-gateway); without it
#                         the build points at https://beta-gateway.invalid, which never answers
#   DWE_LAUNCHER_LINKS    the main screen's links to build with, a JSON object of https URLs by
#   kind:
#                         {"repo": ..., "community": ..., "web": ...}; kinds left out are not shown
#                         (without it, no links)
#   DWE_BUILD_SLOTS       a folder of slot locks shared by every heavy build of the machine (at most
#                         two at a time); without it the build does not wait for a slot
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${DWE_LAUNCHER_IMAGE:-dwe-launcher-dev}"
app="$root/apps/launcher"
target_dir="$root/build/launcher"

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
  echo "launcher.sh: both build slots busy, waiting (up to ${DBUILD_WAIT:-3600}s)" >&2
  flock -w "${DBUILD_WAIT:-3600}" "$try" || {
    echo "launcher.sh: no build slot" >&2
    exit 75
  }
}

# The interface's fonts come from the game pack (one copy in the repository).
copy_fonts() {
  local fonts="$root/content/packs/game/ui/fonts"
  mkdir -p "$app/ui/fonts"
  cp "$fonts/noto_sans_regular.ttf" "$fonts/noto_sans_bold.ttf" "$fonts/noto_sans_license.txt" \
    "$app/ui/fonts/"
}

# The signing key and its password reach the container by name only (docker copies them from this
# environment), never on a command line, where ps would show them; set -x never expands them.
[[ -v TAURI_SIGNING_PRIVATE_KEY ]] || TAURI_SIGNING_PRIVATE_KEY=""
[[ -v TAURI_SIGNING_PRIVATE_KEY_PASSWORD ]] || TAURI_SIGNING_PRIVATE_KEY_PASSWORD=""
export TAURI_SIGNING_PRIVATE_KEY TAURI_SIGNING_PRIVATE_KEY_PASSWORD

run() {
  local tty=()
  [[ -t 0 && -t 1 ]] && tty=(-it)
  docker run --rm "${tty[@]}" -u "$(id -u):$(id -g)" -e HOME=/tmp/home \
    -e CARGO_TARGET_DIR="$target_dir" \
    -e DWE_LAUNCHER_PUBKEY="${DWE_LAUNCHER_PUBKEY:-}" \
    -e DWE_LAUNCHER_GATEWAY="${DWE_LAUNCHER_GATEWAY:-}" \
    -e DWE_LAUNCHER_LINKS="${DWE_LAUNCHER_LINKS:-}" \
    -e TAURI_SIGNING_PRIVATE_KEY -e TAURI_SIGNING_PRIVATE_KEY_PASSWORD \
    -e APPIMAGE_EXTRACT_AND_RUN=1 -e NO_STRIP=1 \
    -v dwe-launcher-cargo-registry:/opt/cargo/registry -v dwe-launcher-cargo-git:/opt/cargo/git \
    -v dwe-launcher-xwin:/opt/xwin-cache \
    -v "$root":"$root" -w "$app/src-tauri" "$image" "$@"
}

# tauri build reads the public key from the configuration; the repository has a placeholder.
config_override() {
  if [[ -n "${DWE_LAUNCHER_PUBKEY:-}" ]]; then
    printf '{"plugins":{"updater":{"pubkey":"%s"}}}' "$DWE_LAUNCHER_PUBKEY"
  else
    printf '{}'
  fi
}

command="${1:-}"
shift || true
case "$command" in
  image)
    docker build -f "$root/.devcontainer/launcher.Dockerfile" -t "$image" "$root/.devcontainer"
    ;;
  test)
    take_slot
    copy_fonts
    run bash -c 'cargo test --locked "$@" && cargo clippy --locked --all-targets -- -D warnings' \
      -- "$@"
    ;;
  build)
    take_slot
    copy_fonts
    run tauri build --no-bundle --config "$(config_override)" "$@"
    echo "launcher.sh: $target_dir/release/dwe-launcher"
    ;;
  bundle)
    take_slot
    copy_fonts
    run tauri build --config "$(config_override)" "$@"
    ;;
  bundle-windows)
    take_slot
    copy_fonts
    run tauri build --runner cargo-xwin --target x86_64-pc-windows-msvc \
      --config "$(config_override)" "$@"
    ;;
  shell)
    if (($#)); then run bash -c "$*"; else run bash; fi
    ;;
  -h | --help | "")
    usage
    ;;
  *)
    echo "launcher.sh: unknown command $command" >&2
    usage >&2
    exit 2
    ;;
esac
