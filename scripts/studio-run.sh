#!/usr/bin/env bash
# DWE Studio's run command: build the game and extractor, refresh the disc cache, and use its full
# base pack beside the project's game and translation packs. The source content/base may be an
# older extraction, so it must not win the base pack id for this development run.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
preset="${PRESET:-linux-clang-debug}"
build="$root/build/$preset"
user_data="${XDG_DATA_HOME:-$HOME/.local/share}/indie-modding-games/dwe"
config="$root/dev-assets.yaml"
if [[ ! -f "$config" ]]; then
  config="$user_data/dev-assets.yaml"
fi

scripts/dev.sh build --target dwe-game dwx

if [[ ! -f "$config" ]]; then
  echo "DWE Studio: configura disc_image en dev-assets.yaml para actualizar la base del juego." >&2
  exit 1
fi

disc="$(sed -n 's/^[[:space:]]*disc_image:[[:space:]]*//p' "$config" | head -n 1)"
disc="${disc%$'\r'}"
if [[ "$disc" == \"*\" ]]; then
  disc="${disc#\"}"
  disc="${disc%\"}"
elif [[ "$disc" == \'*\' ]]; then
  disc="${disc#\'}"
  disc="${disc%\'}"
fi
disc="${disc:-${DWE_DISC:-}}"
if [[ -z "$disc" || ! -f "$disc" ]]; then
  echo "DWE Studio: no encuentra la imagen indicada por disc_image en $config." >&2
  exit 1
fi

cache_folder="$("$build/apps/dwx/dwx" extract --cache "$user_data/dev-cache" "$disc")"
runtime_content="$root/build/studio-content"
mkdir -p "$runtime_content"
rm -rf -- "$runtime_content/base" "$runtime_content/packs"
if ! cp -al "$cache_folder/base" "$runtime_content/base"; then
  rm -rf -- "$runtime_content/base"
  cp -a "$cache_folder/base" "$runtime_content/base"
fi
if ! cp -al "$root/content/packs" "$runtime_content/packs"; then
  rm -rf -- "$runtime_content/packs"
  cp -a "$root/content/packs" "$runtime_content/packs"
fi

exec "$root/scripts/dev.sh" run --content "$runtime_content" --tuning --dev-assets "$@"
