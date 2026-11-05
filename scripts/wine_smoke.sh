#!/usr/bin/env bash
# Smoke test of the Windows package under Wine, on Linux (docs/guides/beta-windows-macos.md):
# dwx.exe identifies the player's disc and dwe-game.exe starts with extracted content; screenshots
# of the display go to the output folder.
#
# Usage: scripts/wine_smoke.sh <package .tar.xz or unpacked folder> --display :<n> --out <folder>
#                              [--disc <image.cue>] [--content <folder>] [--seconds <n>]
#   --display   an X display the caller started (Xvfb :<n>); the game's window opens there
#   --disc      the player's disc image, mounted read only, for dwx.exe identify
#   --content   a content folder extracted with dwx (base/, ui_original/), mounted read only
#   --seconds   how long the game runs before the screenshots (default 90); Enter is pressed twice
#               after it, to leave the opening, and a last screenshot is taken
#   image       DWE_WINE_IMAGE (default dwe-wine-smoke), built from .devcontainer/wine.Dockerfile
#               when missing
# The bundled vulkan-1.dll finds no driver under Wine (Wine registers its Vulkan driver only for its
# own loader), so the test runs with Wine's built-in vulkan-1; on Windows the bundled one is used.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${DWE_WINE_IMAGE:-dwe-wine-smoke}"
package="" display="" out="" disc="" content="" seconds=90

package="${1:-}"
shift || true
while (($#)); do
  case "$1" in
    --display) display="$2" ;;
    --out) out="$2" ;;
    --disc) disc="$2" ;;
    --content) content="$2" ;;
    --seconds) seconds="$2" ;;
    *)
      echo "wine_smoke: unknown option $1" >&2
      exit 2
      ;;
  esac
  shift 2
done
[[ -n "$package" && -n "$display" && -n "$out" ]] || {
  sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit 2
}

docker image inspect "$image" >/dev/null 2>&1 \
  || docker build -f "$root/.devcontainer/wine.Dockerfile" -t "$image" "$root/.devcontainer"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
work="$out/wine-work"
rm -rf "$work"
mkdir -p "$work/prefix"
if [[ -d "$package" ]]; then
  game="$(cd "$package" && pwd)"
else
  mkdir -p "$work/package"
  tar -C "$work/package" --strip-components=1 -xJf "$package"
  game="$work/package"
fi

mounts=(-v "$game":/game:ro -v "$out":/out -v "$work/prefix":/prefix)
mounts+=(-v /tmp/.X11-unix:/tmp/.X11-unix)
identify=":"
if [[ -n "$disc" ]]; then
  mounts+=(-v "$(cd "$(dirname "$disc")" && pwd)":/disc:ro)
  image_name="Z:\\disc\\$(basename "$disc")"
  identify="wine /game/bin/dwx.exe identify '$image_name' | tee /out/dwx-identify.txt"
fi
arguments=""
if [[ -n "$content" ]]; then
  mounts+=(-v "$(cd "$content" && pwd)":/content:ro)
  arguments="--content 'Z:\\content'"
fi

# Every step has a deadline; the container goes away with the test.
timeout $((seconds + 180)) docker run --rm --network none -u "$(id -u):$(id -g)" -e HOME=/tmp/home \
  -e WINEPREFIX=/prefix -e WINEDEBUG=-all -e WINEDLLOVERRIDES="vulkan-1=b" -e DISPLAY="$display" \
  -e VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json "${mounts[@]}" "$image" bash -c "
    wineboot -i >/dev/null 2>&1
    $identify
    wine /game/bin/dwe-game.exe $arguments >/out/game.log 2>&1 &
    sleep $seconds
    import -display $display -window root /out/wine-game.png
    window=\$(xdotool search --name dwe | head -1)
    [ -n \"\$window\" ] && xdotool windowfocus \"\$window\" 2>/dev/null
    for press in 1 2; do xdotool key Return; sleep 8; done
    import -display $display -window root /out/wine-game-after-enter.png
  " || true
rm -rf "$work"
grep -E '^\[(error|info\] GPU|info\] content: pack)' "$out/game.log" || true
ls "$out"/*.png
