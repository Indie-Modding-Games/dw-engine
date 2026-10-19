#!/usr/bin/env bash
# Development entry point. Builds and tests run inside the development container, so the host needs nothing but
# Docker; the game itself runs on the host, which has the GPU and the display.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="dwe-dev"
# The vcpkg binary cache: a named volume by default, or a host folder when DWE_CACHE_DIR is set (CI caches it).
cache_volume="dwe-vcpkg-cache"
cache_dir="${DWE_CACHE_DIR:-}"
preset="${PRESET:-linux-clang-debug}"

usage() {
    cat <<EOF
Usage: scripts/dev.sh <command> [args]

  image               Build the development image
  shell               Open a shell in the container
  configure           Configure the preset (PRESET=$preset)
  build               Build the preset
  test                Run the tests of the preset
  format [--check]    Format the sources with clang-format
  tidy [args]         Run clang-tidy on the sources, passing it args
  run [args]          Run the game on the host
EOF
}

in_container() {
    # -i always, so commands can read piped input; -t only when there is a terminal.
    local tty_flags=(-i)
    if [[ -t 0 && -t 1 ]]; then tty_flags=(-it); fi
    docker run --rm "${tty_flags[@]}" \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp/home \
        -e VCPKG_DOWNLOADS=/cache/vcpkg/downloads \
        -v "$root:$root" -w "$root" \
        -v "$(cache_mount):/cache/vcpkg" \
        "$image" "$@"
}

cache_mount() {
    if [[ -n "$cache_dir" ]]; then echo "$cache_dir"; else echo "$cache_volume"; fi
}

ensure_cache_volume() {
    if [[ -n "$cache_dir" ]]; then
        mkdir -p "$cache_dir"
        return
    fi
    docker volume inspect "$cache_volume" >/dev/null 2>&1 || docker volume create "$cache_volume" >/dev/null
    # The volume is created by root; let the host user write the binary cache.
    docker run --rm -v "$cache_volume:/cache/vcpkg" "$image" chmod a+rwx /cache/vcpkg
}

sources() {
    git -C "$root" ls-files --cached --others --exclude-standard -- '*.cpp' '*.hpp'
}

command="${1:-}"
shift || true

case "$command" in
    image)
        docker build -t "$image" -f "$root/.devcontainer/Dockerfile" "$root/.devcontainer"
        ;;
    shell)
        ensure_cache_volume
        in_container bash
        ;;
    configure)
        ensure_cache_volume
        in_container cmake --preset "$preset" "$@"
        ;;
    build)
        in_container cmake --build --preset "$preset" "$@"
        ;;
    test)
        in_container ctest --preset "$preset" "$@"
        ;;
    format)
        if [[ "${1:-}" == "--check" ]]; then
            sources | in_container xargs -r clang-format --dry-run --Werror
        else
            sources | in_container xargs -r clang-format -i
        fi
        ;;
    tidy)
        sources | grep '\.cpp$' | in_container xargs -r clang-tidy -p "build/$preset" --quiet "$@"
        ;;
    run)
        build_dir="$root/build/$preset"
        installed="$build_dir/vcpkg_installed/x64-linux-libcxx"
        if [[ -d "$installed/share/vulkan/explicit_layer.d" ]]; then
            export VK_LAYER_PATH="$installed/share/vulkan/explicit_layer.d"
            # The layer manifest names the library without a path. Search the release libraries first: the debug
            # build of the validation layer is many times slower and says so on every start.
            export LD_LIBRARY_PATH="$installed/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        fi
        exec "$build_dir/apps/game/dwe-game" "$@"
        ;;
    *)
        usage
        [[ -z "$command" ]] && exit 0 || exit 1
        ;;
esac
