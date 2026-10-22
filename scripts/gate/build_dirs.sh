# The one way scripts/gate.sh deletes a folder: a build folder of this worktree, never anything else. Sourced.

# remove_build_dir <repository root> <folder>: deletes the folder when, resolved (symbolic links too), it is strictly
# inside <root>/build, and build/ is a real folder of the worktree, not a symbolic link to somewhere else; refuses
# anything else with a message and status 1. A missing folder is nothing to do.
remove_build_dir() {
    local root="$1" folder="$2" builds resolved
    if [[ -L "$root/build" ]]; then
        echo "refusing to delete $folder: $root/build is a symbolic link" >&2
        return 1
    fi
    builds="$(realpath -m "$root/build")"
    resolved="$(realpath -m "$folder")"
    if [[ "$resolved" != "$builds/"?* ]]; then
        echo "refusing to delete $folder: not inside $builds" >&2
        return 1
    fi
    [[ -e "$folder" || -L "$folder" ]] || return 0
    rm -rf -- "$resolved"
}
