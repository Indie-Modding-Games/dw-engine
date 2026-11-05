#!/usr/bin/env bash
# Builds a beta release on this computer: the same files as .github/workflows/release.yml makes for
# Linux and Windows (macOS needs a Mac), with the same scripts and Docker images. The game packages
# (scripts/package_linux.sh, package_windows.sh), signed with scripts/package_release.sh into one
# game-manifest.json; the launcher's AppImage, .deb and Windows installer (scripts/launcher.sh
# bundle, bundle-windows) with the beta gateway and the public key built in, and their updater
# signatures; and latest.json. Then it checks them all and, with --upload, puts them in a DRAFT
# prerelease of the repository that someone reviews and publishes by hand
# (docs/modulos/apps/launcher.md).
#
# Usage: scripts/release_local.sh --tag <vX.Y.Z-...> [options]
#   --tag <tag>          the release's tag and title, for example v0.1.0-beta.1 (required)
#   --notes-file <md>    the release notes (Markdown; the launcher shows them as its news)
#   --key <path>         the private signing key (default ~/.dwe-signing/dwe-signing.key)
#   --pubkey <path>      its public key (default <key>.pub); it must be the repository's
#                        DWE_SIGNING_PUBKEY variable, which the launchers handed out trust
#   --gateway <url>      the beta gateway built into the launcher (default: the repository's
#                        DWE_BETA_GATEWAY variable, read with gh)
#   --out <folder>       where everything goes, inside the repository's build/ (default
#                        build/release-local/<tag>); the files to upload end in <out>/dist
#   --upload             also create the draft prerelease on GitHub (on the commit of origin/main,
#                        which must be this checkout's, clean) and upload every file of <out>/dist
#   --replace-draft      with --upload: delete a DRAFT release of the same tag first; a published
#                        release or an existing tag is never touched
#   --no-build           with --upload: upload what an earlier run left in --out (checked again)
#   --dry-run-key <dir>  tests only: generate a throwaway key in <dir> (its password stays in
#                        memory) and build with it, without asking anything; refuses --upload
# The key's password is asked once, on the terminal, at the start. The key and the password reach
# the signing containers only through TAURI_SIGNING_PRIVATE_KEY and
# TAURI_SIGNING_PRIVATE_KEY_PASSWORD (never on a command line, in a file or in the log) and are
# dropped when the script ends. The log and the timings go to <out>/logs/. DWE_BUILD_SLOTS is left
# to scripts/launcher.sh.
set -euo pipefail
# Nothing may trace the key: not this shell (bash -x), not its children (SHELLOPTS, BASH_ENV).
set +x
export -n SHELLOPTS 2>/dev/null || true
unset BASH_ENV ENV

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo="Indie-Modding-Games/dw-engine"
platforms=(linux-x86_64 windows-x86_64)
# The development and launcher images by their own names, as the workflow builds them, and no disc
# or extracted content anywhere near a release.
unset DWE_IMAGE DWE_LAUNCHER_IMAGE DWE_DISC DWE_CONTENT
unset TAURI_SIGNING_PRIVATE_KEY TAURI_SIGNING_PRIVATE_KEY_PASSWORD TAURI_SIGNING_PRIVATE_KEY_PATH

tag="" notes_file="" key="" pubkey="" gateway="" out="" upload=0 replace_draft=0 build=1
dry_run_dir=""

usage() {
  sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

die() {
  echo "release_local: $*" >&2
  exit 1
}

# The value after an option, or a stop when there is none.
value() {
  [[ $# -ge 2 ]] || die "$1 needs a value"
  printf '%s' "$2"
}

while (($#)); do
  case "$1" in
    --tag)
      tag="$(value "$@")"
      shift
      ;;
    --notes-file)
      notes_file="$(value "$@")"
      shift
      ;;
    --key)
      key="$(value "$@")"
      shift
      ;;
    --pubkey)
      pubkey="$(value "$@")"
      shift
      ;;
    --gateway)
      gateway="$(value "$@")"
      shift
      ;;
    --out)
      out="$(value "$@")"
      shift
      ;;
    --dry-run-key)
      dry_run_dir="$(value "$@")"
      shift
      ;;
    --upload) upload=1 ;;
    --replace-draft) replace_draft=1 ;;
    --no-build) build=0 ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      die "unknown option $1"
      ;;
  esac
  shift
done

# The arguments.
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]] \
  || die "--tag must look like v0.1.0-beta.1"
[[ -z "$notes_file" || -f "$notes_file" ]] || die "no notes file $notes_file"
((replace_draft == 0 || upload == 1)) || die "--replace-draft goes with --upload"
((build == 1 || upload == 1)) || die "--no-build goes with --upload"
if [[ -n "$dry_run_dir" ]]; then
  ((upload == 0)) || die "--dry-run-key never uploads"
  ((build == 1)) || die "--dry-run-key builds"
  [[ -z "$key$pubkey" ]] || die "--dry-run-key makes its own key; drop --key and --pubkey"
  mkdir -p "$dry_run_dir"
  dry_run_dir="$(cd "$dry_run_dir" && pwd)"
  [[ "$dry_run_dir/" != "$HOME/.dwe-signing/"* ]] \
    || die "--dry-run-key must not be the real key's folder"
  key="$dry_run_dir/throwaway.key"
  pubkey="$key.pub"
else
  key="${key:-$HOME/.dwe-signing/dwe-signing.key}"
  pubkey="${pubkey:-$key.pub}"
  if ((build)); then
    [[ -f "$key" ]] || die "no signing key at $key (--key)"
  fi
  [[ -f "$pubkey" ]] || die "no public key at $pubkey (--pubkey)"
fi
out="${out:-$root/build/release-local/$tag}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
[[ "$out/" == "$root/build/"?* ]] || die "--out must be a folder inside $root/build"
dist="$out/dist"
logs="$out/logs"
mkdir -p "$logs"
log="$logs/release-local-$(date +%Y%m%d-%H%M%S).log"
# Everything from here on is logged; the key and its password are never printed.
exec > >(tee -a "$log") 2>&1

# The timings of every step, printed at the end.
timings=()
step_name=""
step_start=0
script_start="$SECONDS"
step() {
  step_done
  step_name="$1"
  step_start="$SECONDS"
  echo
  echo "release_local: == $step_name"
}
step_done() {
  [[ -n "$step_name" ]] || return 0
  timings+=("$(printf '%5ds  %s' "$((SECONDS - step_start))" "$step_name")")
  step_name=""
}

signing_key=""
signing_password=""
work=""
cleanup() {
  signing_key=""
  signing_password=""
  unset signing_key signing_password
  [[ -z "$work" ]] || rm -rf "$work"
}
trap cleanup EXIT

# The one file a glob names: an earlier build's file next to it would be ambiguous.
only_one() {
  [[ $# -eq 1 && -f "$1" ]] || die "expected one file, found: $*"
  printf '%s' "$1"
}

# Runs a command with the key and its password in its environment, and only that command.
signed() {
  TAURI_SIGNING_PRIVATE_KEY="$signing_key" \
    TAURI_SIGNING_PRIVATE_KEY_PASSWORD="$signing_password" "$@"
}

# The commit the release is built from and points at.
step "checks before building"
echo "release_local: tag $tag, out $out, log $log"
head="$(git -C "$root" rev-parse HEAD)"
dirty="$(git -C "$root" status --porcelain --untracked-files=no)"
if ((upload)); then
  command -v gh >/dev/null || die "--upload needs the GitHub CLI (gh)"
  gh auth status >/dev/null 2>&1 || die "gh is not logged in (gh auth login)"
  git -C "$root" fetch --quiet origin main
  target="$(git -C "$root" rev-parse origin/main)"
  [[ -z "$dirty" ]] || die "the checkout has changes; a release is built from a clean origin/main"
  [[ "$head" == "$target" ]] \
    || die "HEAD ($head) is not origin/main ($target); check out origin/main first"
  # An existing tag or published release is never touched; a draft only with --replace-draft.
  if git -C "$root" ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null; then
    die "the tag $tag already exists on GitHub; pick another one"
  fi
  if is_draft="$(gh release view "$tag" -R "$repo" --json isDraft --jq .isDraft 2>/dev/null)"; then
    [[ "$is_draft" == true ]] || die "a published release $tag exists; it is never replaced"
    ((replace_draft)) || die "a draft release $tag exists; --replace-draft deletes it first"
  fi
else
  target="$head"
  [[ -z "$dirty" ]] \
    || echo "release_local: WARNING: the checkout has changes; the packages say -dirty"
  if git -C "$root" rev-parse -q --verify origin/main >/dev/null \
    && [[ "$head" != "$(git -C "$root" rev-parse origin/main)" ]]; then
    echo "release_local: WARNING: HEAD is not origin/main; --upload would refuse this build"
  fi
fi
echo "release_local: commit $target"

if [[ -z "$gateway" ]]; then
  gateway="$(gh variable get DWE_BETA_GATEWAY -R "$repo")" \
    || die "cannot read the DWE_BETA_GATEWAY variable; pass --gateway"
fi
[[ "$gateway" =~ ^https://[^[:space:]]+$ ]] || die "the gateway must be an https URL: $gateway"
echo "release_local: gateway $gateway"
avail="$(df -Pk "$out" | awk 'NR == 2 { print int($4 / 1048576) }')"
((build == 0 || avail >= 25)) \
  || echo "release_local: WARNING: only ${avail} GB free; a build takes ~25 GB"

# The key: a throwaway one for tests, or the real one and its password, asked once.
if [[ -n "$dry_run_dir" ]]; then
  step "throwaway key"
  signing_password="throwaway-$(head -c 18 /dev/urandom | base64 | tr -d '/+=')"
  work="$(mktemp -d "$out/.key-XXXXXX")"
  # tauri signer runs in the launcher image, which sees only the repository: the key is made there
  # and moved out.
  TAURI_SIGNING_PRIVATE_KEY_PASSWORD="$signing_password" "$root/scripts/launcher.sh" shell \
    "tauri signer generate --ci -p \"\$TAURI_SIGNING_PRIVATE_KEY_PASSWORD\" \
    -w '$work/throwaway.key' >/dev/null"
  mv -f "$work/throwaway.key" "$key"
  mv -f "$work/throwaway.key.pub" "$pubkey"
  rm -rf "$work"
  work=""
  echo "release_local: throwaway key in $dry_run_dir"
fi
public_key="$(tr -d '[:space:]' <"$pubkey")"
[[ -n "$public_key" ]] || die "$pubkey is empty"
if [[ -z "$dry_run_dir" ]]; then
  # The launchers handed out trust the repository's public key: a release signed with another key
  # would never install.
  if repo_key="$(gh variable get DWE_SIGNING_PUBKEY -R "$repo" 2>/dev/null)"; then
    [[ "$(tr -d '[:space:]' <<<"$repo_key")" == "$public_key" ]] \
      || die "$pubkey is not the repository's DWE_SIGNING_PUBKEY"
  elif ((upload)); then
    die "cannot read the DWE_SIGNING_PUBKEY variable to compare the public key"
  else
    echo "release_local: WARNING: cannot read DWE_SIGNING_PUBKEY; the public key is not compared"
  fi
fi

if ((build)); then
  if [[ -z "$dry_run_dir" ]]; then
    [[ -r /dev/tty ]] || die "the key's password is asked on a terminal; run it from one"
    signing_key="$(<"$key")"
    printf 'Password of %s: ' "$key" >/dev/tty
    IFS= read -rs signing_password </dev/tty
    printf '\n' >/dev/tty
  else
    signing_key="$(<"$key")"
  fi
  # A wrong password or a key that is not the public key's fails here, not after an hour of build.
  work="$(mktemp -d "$out/.probe-XXXXXX")"
  head -c 4096 /dev/urandom >"$work/probe"
  signed "$root/scripts/launcher.sh" shell "tauri signer sign '$work/probe' >/dev/null" \
    || die "the key does not sign (wrong password?)"
  python3 "$root/scripts/package/release_manifest.py" verify --pubkey "$pubkey" "$work/probe" \
    || die "the key's signature does not verify with $pubkey"
  rm -rf "$work"
  work=""
  echo "release_local: the key signs and $pubkey verifies it"
fi

if ((build)); then
  export DWE_LAUNCHER_GATEWAY="$gateway" DWE_LAUNCHER_PUBKEY="$public_key"

  step "images"
  "$root/scripts/dev.sh" image
  "$root/scripts/launcher.sh" image
  "$root/scripts/dev.sh" shell -c 'test -d /opt/xwin/sysroot' </dev/null \
    || die "no Windows libraries: run scripts/dev.sh windows-sdk once (it asks for the licence)"

  rm -rf "$dist" "$out/game"
  mkdir -p "$dist"

  step "game package linux-x86_64"
  "$root/scripts/package_linux.sh" --out "$out/game/linux-x86_64" </dev/null

  step "game package windows-x86_64"
  "$root/scripts/package_windows.sh" --out "$out/game/windows-x86_64" </dev/null

  # Old bundles of an earlier build would be picked up with the new ones.
  launcher_out="$root/build/launcher"
  linux_bundle="$launcher_out/release/bundle"
  windows_bundle="$launcher_out/x86_64-pc-windows-msvc/release/bundle"
  rm -rf "$linux_bundle" "$windows_bundle"

  step "launcher AppImage and .deb"
  signed "$root/scripts/launcher.sh" bundle </dev/null
  cp "$linux_bundle"/appimage/*.AppImage "$linux_bundle"/appimage/*.AppImage.sig \
    "$linux_bundle"/deb/*.deb "$dist/"

  step "launcher Windows installer"
  signed "$root/scripts/launcher.sh" bundle-windows </dev/null
  cp "$windows_bundle"/nsis/*-setup.exe "$windows_bundle"/nsis/*-setup.exe.sig "$dist/"

  step "signatures and game-manifest.json"
  # tauri signs only the updater's files; the .deb gets its signature here, so every file a tester
  # downloads can be checked.
  for deb in "$dist"/*.deb; do
    signed "$root/scripts/launcher.sh" shell "tauri signer sign '$deb' >/dev/null" </dev/null
  done
  for platform in "${platforms[@]}"; do
    tarball="$(only_one "$out/game/$platform"/dwe-beta-*-"$platform".tar.xz)"
    signed "$root/scripts/package_release.sh" --platform "$platform" --tarball "$tarball" \
      --out "$dist" </dev/null
  done
  signing_key=""
  signing_password=""
fi

# The checks before anything is uploaded.
step "checks of the files"
[[ -d "$dist" ]] || die "nothing built in $dist"
appimage="$(only_one "$dist"/*.AppImage)"
setup="$(only_one "$dist"/*-setup.exe)"
version="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' \
  "$root/apps/launcher/src-tauri/tauri.conf.json")"
manifest_launcher() {
  python3 "$root/scripts/package/release_manifest.py" launcher --version "$version" --notes "$tag" \
    --platform linux-x86_64 "$appimage.sig" "$1" --platform windows-x86_64 "$setup.sig" "$2"
}
# latest.json names the bundles by their API URLs, which exist only after the upload: the preview
# has the same form with asset 0.
# The packages name the commit they were built from; a build of a changed checkout says -dirty,
# which only a build without --upload may.
commit_check=(--commit "$target")
[[ -z "$dirty" || "$upload" == 1 ]] || commit_check=()
preview="$out/latest.preview.json"
pending="https://api.github.com/repos/$repo/releases/assets/0"
manifest_launcher "$pending" "$pending" >"$preview"
python3 "$root/scripts/package/release_manifest.py" check --folder "$dist" --pubkey "$pubkey" \
  --platform linux-x86_64 --platform windows-x86_64 --latest "$preview" --repo "$repo" \
  "${commit_check[@]}"

# Nothing read from a disc in any package: the packagers' own check, again on what ships.
# shellcheck source=scripts/package/no_disc_data.sh
source "$root/scripts/package/no_disc_data.sh"
work="$(mktemp -d "$out/.check-XXXXXX")"
for tarball in "$dist"/dwe-game-*.tar.xz; do
  mkdir -p "$work/game"
  tar -C "$work/game" -xJf "$tarball"
  package_no_disc_data "$work/game" "$(basename "$tarball")" \
    || die "$(basename "$tarball") holds disc data"
  rm -rf "$work/game"
done
for deb in "$dist"/*.deb; do
  dpkg-deb -c "$deb" | awk '{ printf "%s%c", $6, 0 }' | policy_forbidden_paths_in >&2 \
    && die "$(basename "$deb") holds disc data"
  # The program a tester runs has the gateway and the public key built in.
  dpkg-deb --fsys-tarfile "$deb" | tar -x -C "$work" --wildcards '*/bin/*'
done
binaries=("$work"/usr/bin/* "$root/build/launcher/x86_64-pc-windows-msvc/release/dwe-launcher.exe")
for binary in "${binaries[@]}"; do
  [[ -f "$binary" ]] || die "no launcher program at $binary"
  grep -q -a -F "$gateway" "$binary" \
    || die "$(basename "$binary") does not have the gateway $gateway"
  grep -q -a -F "$public_key" "$binary" || die "$(basename "$binary") does not have the public key"
done
rm -rf "$work"
work=""
echo "release_local: no disc data; the launchers have the gateway and the public key"
step_done

if ((upload)); then
  step "draft prerelease"
  notes="$out/notes.md"
  if [[ -n "$notes_file" ]]; then cp "$notes_file" "$notes"; else : >"$notes"; fi
  old_draft="$(gh release view "$tag" -R "$repo" --json isDraft --jq .isDraft 2>/dev/null || true)"
  if ((replace_draft)) && [[ "$old_draft" == true ]]; then
    gh release delete "$tag" -R "$repo" --yes
    echo "release_local: the old draft $tag is deleted"
  fi
  gh release create "$tag" -R "$repo" --draft --prerelease --title "$tag" --notes-file "$notes" \
    --target "$target"
  gh release upload "$tag" -R "$repo" "$dist"/*

  # latest.json with the API URLs of the uploaded bundles, which the gateway points at itself.
  asset_url() {
    gh api "repos/$repo/releases" --paginate \
      --jq ".[] | select(.tag_name == \"$tag\") | .assets[] | select(.name == \"$1\") | .url"
  }
  manifest_launcher "$(asset_url "$(basename "$appimage")")" "$(asset_url "$(basename "$setup")")" \
    >"$dist/latest.json"
  python3 "$root/scripts/package/release_manifest.py" check --folder "$dist" --pubkey "$pubkey" \
    --platform linux-x86_64 --platform windows-x86_64 --latest "$dist/latest.json" --repo "$repo" \
    "${commit_check[@]}"
  grep -q '/releases/assets/0"' "$dist/latest.json" && die "latest.json has no asset URL"
  gh release upload "$tag" -R "$repo" "$dist/latest.json"
  draft_url="$(gh release view "$tag" -R "$repo" --json url --jq .url)"
  echo "release_local: draft ready to review: $draft_url"
  step_done
fi

echo
echo "release_local: files in $dist:"
find "$dist" -maxdepth 1 -type f -printf '  %12s  %f\n' | sort -k2
echo "release_local: timings:"
printf '  %s\n' "${timings[@]}"
printf '  %5ds  total\n' "$((SECONDS - script_start))"
