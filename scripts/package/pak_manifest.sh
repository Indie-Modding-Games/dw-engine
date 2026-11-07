# shellcheck shell=bash
# Reads the manifest of a .pak archive with POSIX tools only, for the package checks
# (no_disc_data.sh and scripts/package_macos.sh).
#
# Usage: source it, then `package_pak_is_base <archive>`.

# Whether a .pak archive holds the base pack: its manifest (offset and size at 0x30 and 0x38,
# docs/formats/pak.md) says id: base.
package_pak_is_base() {
  local archive="$1" offset size manifest
  offset="$(od -An -t u8 -j 48 -N 8 "$archive" | tr -d ' ')"
  size="$(od -An -t u8 -j 56 -N 8 "$archive" | tr -d ' ')"
  [[ -n "$offset" && -n "$size" ]] || return 1
  manifest="$(tail -c +"$((offset + 1))" "$archive" | head -c "$size")"
  grep -q '^id: base$' <<<"$manifest"
}
