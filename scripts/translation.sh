#!/usr/bin/env bash
# Keeps the team's translation of the original's text in step with the user's extracted English
# (docs/guides/traducir.md). It reads the extracted text/en and the translation pack, prints what is
# missing, stale or broken, and writes the pack's .sources.lock. It never writes the English
# anywhere: what it prints is for the translator's terminal only.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
language="${DWE_TRANSLATION_LANGUAGE:-es}"
pack="${DWE_TRANSLATION_PACK:-$root/content/packs/es-translation}"
reference_language="en"
translation_dir="$pack/text/$language"
lock_file="$translation_dir/.sources.lock"
# The tables of the executable and the overlays (block.slot keys, decimal slots) and the prefix of
# the story tables (story_NNN, one per event script, docs/formats/scripts.md#lua): text.<hex offset>
# keys, found in the English and in the translation.
fixed_sources=(main btl std vs kar fish trn)
story_prefix=story_
# The slot of a key, zero-padded: main.move_names.0012. A story key keeps its slot as extracted, the
# offset of its instruction in four hex digits (story_049.text.3b66).
slot_digits=4
# A line of a story textbox: the original's lines are 30 characters at most (not counting the tags);
# long reports the translated lines wider than this (or than the widest line of their English) and
# the texts with more lines than their English and than a box (4 rows) holds.
story_line_width=30
story_box_rows=4

# What is never written to the translation (docs/guides/traducir.md); the lock lists these ids so
# coverage tells them from what is not translated yet. kept_english: the names that stay in English,
# whole blocks (source.block). kept_original: what is not English words and stays as extracted:
# whole blocks, single ids (source.block.slot), every blank string and every string that is, but for
# its surrounding spaces, a name of kept_name_block (the speaker lines of the curling hints).
kept_english_blocks=(main.digimon_names)
kept_original_blocks=(
  main.tournament_grades
  main.script_menu_hiragana_ka_row main.script_menu_lowercase_rows main.singles
)
kept_name_block=main.digimon_names
kept_original_ids=(
  main.battle_ui_bits_labels.0000
  main.map_names.0018
  main.stat_labels.0000 main.stat_labels.0001
  main.main_menu_save_labels.0000 main.main_menu_save_labels.0003 main.main_menu_save_labels.0004
  main.main_menu_save_labels.0005 main.main_menu_save_labels.0006 main.main_menu_save_labels.0008
  main.main_menu_save_labels.0009 main.main_menu_save_labels.0010
  main.main_menu_save_messages.0004
)

usage() {
  cat <<EOF
Usage: scripts/translation.sh <command> [args]

  status [story]      Per source: strings, translated, kept in English, kept as the original,
                      missing, stale; the story tables add up in one row, or one row each with
                      "status story"
  missing [source]    The ids with no translation and not kept, with their English (terminal only);
                      source is a table (main, story_049) or "story" for every story table
  stale               The translated ids whose English changed since they were translated, with the
                      new English
  check               Every problem: keys the English does not have, tags that differ, kept ids that
                      are translated, stale ids; exits with 1 if there is any
  long                The translations longer than their English, with both lengths, to check that
                      they fit; for the story tables, the lines wider than the box's and the texts
                      with more lines than the box
  lock                Write $lock_file: the hash of each translated id's English (an id already in
                      the lock keeps its hash, so a stale one stays stale) and the kept ids
  accept <id>...      Mark the given ids as revised: their hash in the lock becomes the current
                      English's

Environment:
  DWE_CONTENT                 Content folder extracted with dwx (its
                              base/text/$reference_language is read; required)
  DWE_TRANSLATION_LANGUAGE    Language of the translation (default es)
  DWE_TRANSLATION_PACK        The translation pack (default content/packs/es-translation)
EOF
}

fail() {
  echo "translation.sh: $*" >&2
  exit 1
}

english_dir() {
  [[ -n "${DWE_CONTENT:-}" ]] || fail "DWE_CONTENT is not set"
  local dir="$DWE_CONTENT/base/text/$reference_language"
  [[ -d "$dir" ]] || fail "no extracted text in $dir"
  echo "$dir"
}

# Prints "id<TAB>quoted text" for every string of a flat text table ("block.slot": "text",
# docs/formats/textos.md), in file order; the text stays as YAML wrote it between the quotes.
read_text_file() {
  local source="$1" file="$2" story=0
  if [[ "$source" == "$story_prefix"* ]]; then story=1; fi
  awk -v source="$source" -v digits="$slot_digits" -v story="$story" '
        function fail(message) {
            printf "translation.sh: %s:%d: %s\n", FILENAME, FNR, message > "/dev/stderr"
            exit 2
        }
        function quoted(text) {
            if (text !~ /^".*"$/) fail("expected a double-quoted string")
            return substr(text, 2, length(text) - 2)
        }
        /^[[:space:]]*(#|$)/ { next }
        /^(source|strings):/ {
            fail("the extraction format before flat tables: extract the disc again")
        }
        story && /^"?[a-z0-9_]+\.[0-9a-f]+"?: / {
            key = $1; sub(/:$/, "", key); gsub(/"/, "", key)
            printf "%s.%s\t%s\n", source, key, quoted(substr($0, index($0, ": ") + 2))
            next
        }
        !story && /^"?[a-z0-9_]+\.[0-9]+"?: / {
            key = $1; sub(/:$/, "", key); gsub(/"/, "", key)
            split(key, parts, ".")
            value = substr($0, index($0, ": ") + 2)
            printf "%s.%s.%0" digits "d\t%s\n", source, parts[1], parts[2] + 0, quoted(value)
            next
        }
        { fail("unexpected line") }
    ' "$file"
}

# The sources: the fixed ones, then the story tables of the English and of the translation.
find_sources() {
  local dir file
  dir="$(english_dir)"
  sources=("${fixed_sources[@]}")
  while read -r file; do
    sources+=("$file")
  done < <(
    for file in "$dir/$story_prefix"*.yaml "$translation_dir/$story_prefix"*.yaml; do
      if [[ -f "$file" ]]; then basename "$file" .yaml; fi
    done | LC_ALL=C sort -u
  )
}

# Every string of the sources in a folder, as id<TAB>quoted text.
strings_in() {
  local dir="$1" source
  for source in "${sources[@]}"; do
    if [[ -f "$dir/$source.yaml" ]]; then read_text_file "$source" "$dir/$source.yaml"; fi
  done
}

# Every English string of the sources, as id<TAB>quoted text.
english_strings() {
  strings_in "$(english_dir)"
}

# Every translated string, as id<TAB>quoted text.
translated_strings() {
  strings_in "$translation_dir"
}

# An awk function that undoes the escapes of a YAML double-quoted string, and fails on one the
# extraction never writes.
awk_unquote='
    function unquote(text,    out, at, code) {
        out = ""
        while ((at = index(text, "\\")) > 0) {
            out = out substr(text, 1, at - 1)
            code = substr(text, at + 1, 1)
            if (code == "\\" || code == "\"" || code == "/") out = out code
            else if (code == "n") out = out "\n"
            else if (code == "t") out = out "\t"
            else { print "translation.sh: unsupported escape \\" code > "/dev/stderr"; exit 2 }
            text = substr(text, at + 2)
        }
        return out text
    }'

# id<TAB>sha1 of each English string: the hex SHA-1 of its UTF-8 text. awk unquotes each string into
# NUL-separated id and text, and one Python process hashes them all (a sha1sum per string takes
# minutes for the story tables).
english_hashes() {
  awk -F '\t' "$awk_unquote"'{ printf "%s%c%s%c", $1, 0, unquote($2), 0 }' "$scratch/english" \
    | python3 -c '
import hashlib, sys
fields = sys.stdin.buffer.read().split(b"\0")
for at in range(0, len(fields) - 1, 2):
    print(fields[at].decode() + "\t" + hashlib.sha1(fields[at + 1]).hexdigest())
'
}

# id<TAB>sha1 of each id in the lock's sources.
locked_hashes() {
  [[ -f "$lock_file" ]] || return 0
  awk '/^sources:/ { inside = 1; next } /^[a-z_]+:/ { inside = 0 }
         inside && /^  "/ {
             key = $1; sub(/:$/, "", key); gsub(/"/, "", key)
             value = $2; gsub(/"/, "", value)
             print key "\t" value
         }' "$lock_file"
}

# The pattern of the ids of some blocks (source.block).
blocks_pattern() {
  local IFS="|"
  local blocks="$*"
  echo "^(${blocks//./[.]})[.][0-9]+$"
}

# The kept ids, as kind<TAB>id with kind english or original (see kept_english_blocks).
kept_ids() {
  english_strings | awk -F '\t' -v english_pattern="$(blocks_pattern "${kept_english_blocks[@]}")" \
    -v original_pattern="$(blocks_pattern "${kept_original_blocks[@]}")" \
    -v name_block="$kept_name_block." \
    -v ids="${kept_original_ids[*]}" '
        function trimmed(text) { gsub(/^ +| +$/, "", text); return text }
        BEGIN { split(ids, list, " "); for (i in list) original_id[list[i]] = 1 }
        { id[NR] = $1; text[NR] = $2; if (index($1, name_block) == 1) kept_name[trimmed($2)] = 1 }
        END {
            for (i = 1; i <= NR; i++) {
                name = trimmed(text[i])
                if (id[i] ~ english_pattern) print "english\t" id[i]
                else if (id[i] ~ original_pattern || id[i] in original_id || name == "" \
                    || name in kept_name)
                    print "original\t" id[i]
            }
        }'
}

# Prints the ids of id<TAB>translation<TAB>English rows whose sets of {tags} and placeholders
# differ.
ids_with_other_codes() {
  awk -F '\t' "$awk_unquote"'
        function codes(text,    list, code, sorted, i, j, n) {
            text = unquote(text)
            gsub(/\{\{|\}\}/, "", text)
            n = 0
            while (match(text, /\{[^}]*\}/)) {
                code = substr(text, RSTART, RLENGTH)
                list[++n] = code
                text = substr(text, RSTART + RLENGTH)
            }
            for (i = 2; i <= n; i++) {
                code = list[i]
                for (j = i - 1; j >= 1 && list[j] > code; j--) list[j + 1] = list[j]
                list[j + 1] = code
            }
            sorted = ""
            for (i = 1; i <= n; i++) sorted = sorted " " list[i]
            return sorted
        }
        codes($2) != codes($3) { print $1 }'
}

# For the id<TAB>translation<TAB>English rows: prints id<TAB>characters of the translation > of the
# English for a longer translation of a table, and for a story text, id<TAB>line N: characters >
# limit for each line wider than story_line_width and its English's widest line, and
# id<TAB>lines: N > limit when it has more lines than its English and than a box holds. The
# characters of a line do not count its tags.
longer_translations() {
  awk -F '\t' -v story_id="^$story_prefix" -v width="$story_line_width" \
    -v rows="$story_box_rows" "$awk_unquote"'
        function characters(text,    bytes) {
            bytes = length(text)
            return bytes - gsub(/[\200-\277]/, "", text)
        }
        function visible(line) {
            gsub(/\{\{|\}\}/, "_", line); gsub(/\{[^}]*\}/, "", line)
            return characters(line)
        }
        function widest(text,    lines, n, i, most) {
            n = split(text, lines, "\n"); most = 0
            for (i = 1; i <= n; i++) if (visible(lines[i]) > most) most = visible(lines[i])
            return most
        }
        {
            translation = unquote($2); english = unquote($3)
            if ($1 !~ story_id) {
                if (characters(translation) > characters(english))
                    printf "%s\t%d > %d\n", $1, characters(translation), characters(english)
                next
            }
            limit = widest(english) > width ? widest(english) : width
            n = split(translation, lines, "\n")
            for (i = 1; i <= n; i++)
                if (visible(lines[i]) > limit)
                    printf "%s\tline %d: %d > %d\n", $1, i, visible(lines[i]), limit
            most = split(english, lines, "\n")
            if (most < rows) most = rows
            if (n > most) printf "%s\tlines: %d > %d\n", $1, n, most
        }'
}

# The rows of the left file whose id is not in the right file (both id<TAB>... and sorted by id).
rows_missing_from() {
  LC_ALL=C join -t $'\t' -v 1 <(LC_ALL=C sort "$1") <(LC_ALL=C sort "$2")
}

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

load() {
  find_sources
  english_strings >"$scratch/english"
  translated_strings >"$scratch/translated"
  kept_ids >"$scratch/kept_ids"
  awk -F '\t' '$1 == "english" { print $2 "\t" }' "$scratch/kept_ids" >"$scratch/kept_english"
  awk -F '\t' '$1 == "original" { print $2 "\t" }' "$scratch/kept_ids" >"$scratch/kept_original"
  cat "$scratch/kept_english" "$scratch/kept_original" >"$scratch/kept"
  locked_hashes >"$scratch/locked"
}

# The translated ids whose English hash differs from the locked one, as id<TAB>quoted English.
stale_rows() {
  english_hashes >"$scratch/hashes"
  LC_ALL=C join -t $'\t' <(LC_ALL=C sort "$scratch/locked") <(LC_ALL=C sort "$scratch/hashes") \
    | awk -F '\t' '$2 != $3 { print $1 }' >"$scratch/stale_ids"
  LC_ALL=C join -t $'\t' <(LC_ALL=C sort "$scratch/stale_ids") <(LC_ALL=C sort "$scratch/english")
}

# id<TAB>translation<TAB>English of every translated id.
translated_with_english() {
  LC_ALL=C join -t $'\t' <(LC_ALL=C sort "$scratch/translated") <(LC_ALL=C sort "$scratch/english")
}

missing_rows() {
  rows_missing_from "$scratch/english" "$scratch/translated" >"$scratch/untranslated"
  rows_missing_from "$scratch/untranslated" "$scratch/kept"
}

# The pattern of the ids of a source, or of every story table for "story".
source_pattern() {
  if [[ "$1" == "${story_prefix%_}" ]]; then
    echo "^${story_prefix}[0-9]+\."
  else
    echo "^$1\."
  fi
}

command_status() {
  (($# == 0)) || [[ "$*" == "${story_prefix%_}" ]] || fail "status takes no argument or story"
  load
  stale_rows >"$scratch/stale"
  missing_rows >"$scratch/missing"
  local columns=(english translated kept_english kept_original missing stale)
  local rows=("${fixed_sources[@]}" "${story_prefix%_}") source file
  if (($# > 0)); then rows=("${sources[@]:${#fixed_sources[@]}}"); fi
  printf '%-9s' source
  printf ' %13s' strings "${columns[@]:1}"
  echo
  for source in "${rows[@]}"; do
    printf '%-9s' "$source"
    for file in "${columns[@]}"; do
      printf ' %13s' "$(grep -cE "$(source_pattern "$source")" "$scratch/$file" || true)"
    done
    echo
  done
}

command_missing() {
  load
  local source="${1:-}"
  if [[ -z "$source" ]]; then
    missing_rows
  else
    missing_rows | grep -E "$(source_pattern "$source")" || true
  fi
}

command_stale() {
  load
  stale_rows
}

command_check() {
  load
  local problems=0 id
  while IFS=$'\t' read -r id _; do
    echo "not in the English: $id"
    problems=$((problems + 1))
  done < <(rows_missing_from "$scratch/translated" "$scratch/english")
  while IFS=$'\t' read -r id _; do
    echo "kept but translated: $id"
    problems=$((problems + 1))
  done < <(
    LC_ALL=C join -t $'\t' <(LC_ALL=C sort "$scratch/translated") <(LC_ALL=C sort "$scratch/kept")
  )
  while read -r id; do
    echo "tags differ from the English: $id"
    problems=$((problems + 1))
  done < <(translated_with_english | ids_with_other_codes)
  while IFS=$'\t' read -r id _; do
    echo "stale: $id"
    problems=$((problems + 1))
  done < <(stale_rows)
  while IFS=$'\t' read -r id _; do
    echo "translated but not in the lock: $id"
    problems=$((problems + 1))
  done < <(rows_missing_from "$scratch/translated" "$scratch/locked")
  echo "$problems problems"
  ((problems == 0))
}

command_long() {
  load
  translated_with_english | longer_translations
}

# Writes the lock's list of the kept ids of one kind (kept_english or kept_original).
write_kept_list() {
  local kind="$1"
  cut -f1 "$scratch/$kind" | LC_ALL=C sort \
    | awk -v kind="$kind" 'NR == 1 { print kind ":" }
                           { printf "  - \"%s\"\n", $1 }
                           END { if (NR == 0) print kind ": []" }'
}

write_lock() {
  local hashes="$1"
  {
    echo "# Written by scripts/translation.sh lock; do not edit by hand."
    echo "# sources: the hex SHA-1 of the $reference_language text each translation was written" \
      "from."
    LC_ALL=C join -t $'\t' <(cut -f1 "$scratch/translated" | LC_ALL=C sort) \
      <(LC_ALL=C sort "$hashes") \
      | awk -F '\t' 'NR == 1 { print "sources:" } { printf "  \"%s\": \"%s\"\n", $1, $2 }
                         END { if (NR == 0) print "sources: {}" }'
    write_kept_list kept_english
    write_kept_list kept_original
  } >"$lock_file"
}

command_lock() {
  load
  english_hashes >"$scratch/hashes"
  # A locked hash wins over the current one, so that only accept clears a stale id.
  cat "$scratch/locked" <(rows_missing_from "$scratch/hashes" "$scratch/locked") >"$scratch/merged"
  write_lock "$scratch/merged"
  echo "wrote $lock_file"
}

command_accept() {
  (($# > 0)) || fail "accept needs at least one id"
  load
  english_hashes >"$scratch/hashes"
  printf '%s\n' "$@" | LC_ALL=C sort >"$scratch/accepted"
  LC_ALL=C join -t $'\t' "$scratch/accepted" <(LC_ALL=C sort "$scratch/hashes") >"$scratch/fresh"
  [[ "$(wc -l <"$scratch/fresh")" -eq $# ]] || fail "some ids are not in the English"
  cat "$scratch/fresh" <(rows_missing_from "$scratch/locked" "$scratch/fresh") >"$scratch/merged"
  write_lock "$scratch/merged"
  echo "accepted $# ids in $lock_file"
}

command="${1:-}"
shift || true
case "$command" in
  status) command_status "$@" ;;
  missing) command_missing "$@" ;;
  stale) command_stale ;;
  check) command_check ;;
  long) command_long ;;
  lock) command_lock ;;
  accept) command_accept "$@" ;;
  *)
    usage
    [[ -z "$command" || "$command" == help ]]
    ;;
esac
