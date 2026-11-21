#!/usr/bin/env bash
# Keeps the team's translation of the original's text in step with the user's extracted English
# (docs/guides/translate.md). It reads the extracted text/en and the translation pack, prints what
# is missing, stale or broken, and writes the pack's .sources.lock. It never writes the English
# anywhere: what it prints is for the translator's terminal only.
set -euo pipefail
# awk reads the texts as bytes in every implementation (mawk, gawk, busybox): the widths below
# decode the UTF-8 themselves.
export LC_ALL=C

fail() {
  echo "translation.sh: $*" >&2
  exit 1
}

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
packs_dir="$root/content/packs"
localization_dir="$packs_dir/game/localization"
launcher_check="$root/apps/launcher/tools/i18n_check.mjs"
launcher_texts="$root/apps/launcher/ui/js/i18n"
reference_language="en"

# The translation pack of a language: content/packs/<code>-translation (id <code>_translation).
pack_of() {
  echo "$packs_dir/$1-translation"
}

# --lang <code> and --pack <folder> may come anywhere in the arguments; they win over the
# environment. The language picks its pack unless one is given.
language="${DWE_TRANSLATION_LANGUAGE:-es}"
pack="${DWE_TRANSLATION_PACK:-}"
arguments=()
while (($# > 0)); do
  case "$1" in
    --lang | --pack)
      (($# > 1)) || fail "$1 needs a value"
      if [[ "$1" == --lang ]]; then language="$2"; else pack="$2"; fi
      shift 2
      ;;
    --lang=*)
      language="${1#--lang=}"
      shift
      ;;
    --pack=*)
      pack="${1#--pack=}"
      shift
      ;;
    *)
      arguments+=("$1")
      shift
      ;;
  esac
done
# (The guard keeps an empty list working in bash 3.2, the one macOS ships.)
set -- ${arguments[@]+"${arguments[@]}"}
# A language code as the packs write it: ISO 639 in lower case, maybe _ and a region or script.
language_code='^[a-z]{2,3}(_[a-z0-9]{2,8})?$'
[[ "$language" =~ $language_code ]] || fail "not a language code: $language"
[[ "$language" != "$reference_language" ]] || fail "$reference_language is the original's language"
pack="${pack:-$(pack_of "$language")}"
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
# A line of a story textbox: the original's lines are 30 columns at most (not counting the tags); a
# wide character (Chinese, Japanese, Korean, full-width forms) takes two, so 15 of them fill a line.
# long reports the translated lines wider than this (or than the widest line of their English) and
# the texts with more lines than their English and than a box (4 rows) holds.
story_line_width=30
story_box_rows=4

# What is never written to the translation (docs/guides/translate.md); the lock lists these ids so
# coverage tells them from what is not translated yet. kept_english: the names that stay in English,
# whole blocks (source.block). kept_original: what is not English words and stays as extracted:
# whole blocks, single ids (source.block.slot), every blank string and every string that is, but for
# its surrounding spaces, a name of kept_name_block (the speaker lines of the curling hints).
kept_english_blocks=(main.digimon_names)
# The ids of kept_english_blocks that are a title in English words, not a name: they are translated.
# Slot 0 is the player's placeholder until the opening names the player (getSpeakerName, 0xFD).
translated_name_ids=(main.digimon_names.0000 main.digimon_names.0118)
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
Usage: scripts/translation.sh [--lang <code>] [--pack <folder>] <command> [args]

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
  coverage [code...]  Every language (or the given ones) side by side: per source, translated of
                      the strings, kept, missing and stale, then the engine's own texts
                      (localization/<code> keys of English's) and the launcher's

Options:
  --lang <code>               Language of the translation (default es): its pack is
                              content/packs/<code>-translation and its texts text/<code>
  --pack <folder>             The translation pack, when it is not the language's

Environment:
  DWE_CONTENT                 Content folder extracted with dwx (its
                              base/text/$reference_language is read; required)
  DWE_TRANSLATION_LANGUAGE    Language of the translation, as --lang (default es)
  DWE_TRANSLATION_PACK        The translation pack, as --pack (default the language's)
EOF
}

english_dir() {
  [[ -n "${DWE_CONTENT:-}" ]] || fail "DWE_CONTENT is not set"
  local dir="$DWE_CONTENT/base/text/$reference_language"
  [[ -d "$dir" ]] || fail "no extracted text in $dir"
  echo "$dir"
}

# Prints "id<TAB>quoted text" for every string of a flat text table ("block.slot": "text",
# docs/formats/texts.md), in file order; the text stays as YAML wrote it between the quotes.
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

# Every translated string, as id<TAB>quoted text (of the language's folder, or of the one given).
translated_strings() {
  strings_in "${1:-$translation_dir}"
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

# id<TAB>sha1 of each id in the sources of a lock (the language's by default).
locked_hashes() {
  local lock="${1:-$lock_file}"
  [[ -f "$lock" ]] || return 0
  awk '/^sources:/ { inside = 1; next } /^[a-z_]+:/ { inside = 0 }
         inside && /^  "/ {
             key = $1; sub(/:$/, "", key); gsub(/"/, "", key)
             value = $2; gsub(/"/, "", value)
             print key "\t" value
         }' "$lock"
}

# The pattern of the ids of some blocks (source.block).
blocks_pattern() {
  local IFS="|"
  local blocks="$*"
  echo "^(${blocks//./[.]})[.][0-9]+$"
}

own_names() {
  awk -F '\t' -v block="$kept_name_block." -v titles="${translated_name_ids[*]}" '
        BEGIN { n = split(titles, list, " "); for (i = 1; i <= n; i++) title[list[i]] = 1 }
        index($1, block) == 1 && !($1 in title) { found = 1 }
        END { exit !found }' "$1"
}

# The kept ids of the loaded English, as kind<TAB>id with kind english or original (see
# kept_english_blocks).
kept_ids() {
  local names=0
  if [[ -n "${1:-}" ]] && own_names "$1"; then names=1; fi
  awk -F '\t' -v english_pattern="$(blocks_pattern "${kept_english_blocks[@]}")" \
    -v own_names="$names" \
    -v original_pattern="$(blocks_pattern "${kept_original_blocks[@]}")" \
    -v name_block="$kept_name_block." \
    -v ids="${kept_original_ids[*]}" -v titles="${translated_name_ids[*]}" '
        function trimmed(text) { gsub(/^ +| +$/, "", text); return text }
        BEGIN {
            split(ids, list, " "); for (i in list) original_id[list[i]] = 1
            split(titles, list, " "); for (i in list) title_id[list[i]] = 1
        }
        {
            id[NR] = $1; text[NR] = $2
            if (!own_names && index($1, name_block) == 1 && !($1 in title_id))
                kept_name[trimmed($2)] = 1
        }
        END {
            for (i = 1; i <= NR; i++) {
                name = trimmed(text[i])
                if (id[i] in title_id) continue
                if (id[i] ~ english_pattern && !own_names) print "english\t" id[i]
                else if (id[i] ~ original_pattern || id[i] in original_id || name == "" \
                    || name in kept_name)
                    print "original\t" id[i]
            }
        }' "$scratch/english"
}

# Prints the ids of id<TAB>translation<TAB>English rows whose sets of {tags} and placeholders
# differ. A {prompt:<action>} marker (the device's key or button, docs/architecture/ui.md) is the
# translation's own: it names in a marker what the English names in words ("X button").
ids_with_other_codes() {
  awk -F '\t' "$awk_unquote"'
        function codes(text,    list, code, sorted, i, j, n) {
            text = unquote(text)
            gsub(/\{\{|\}\}/, "", text)
            gsub(/\{prompt:[a-z_]+\}/, "", text)
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

# An awk function that gives the display width of a UTF-8 text in columns: one per character, two
# per East Asian wide or full-width one (the ranges of Unicode's EastAsianWidth W and F that a text
# of the game can use: Hangul, CJK, kana, full-width forms, emoji).
awk_width='
    BEGIN {
        for (i = 128; i < 256; i++) byte_value[sprintf("%c", i)] = i
        split("4352-4447 11904-12350 12353-13311 13312-19903 19968-40959 40960-42191 " \
              "43360-43391 44032-55203 63744-64255 65040-65049 65072-65135 65280-65376 " \
              "65504-65510 127744-128591 129280-129535 131072-262141", wide_list, " ")
        for (i in wide_list) {
            split(wide_list[i], bounds, "-")
            wide_from[i] = bounds[1] + 0; wide_to[i] = bounds[2] + 0
        }
    }
    function byte_of(character) {
        return (character in byte_value) ? byte_value[character] : 0
    }
    function wide(point,    i) {
        for (i in wide_from) if (point >= wide_from[i] && point <= wide_to[i]) return 1
        return 0
    }
    function display_width(text,    n, i, lead, point, columns) {
        # Below U+0800 (no lead byte of three or four) every character is one column.
        if (text !~ /[\340-\367]/) return length(text) - gsub(/[\200-\277]/, "", text)
        n = length(text); columns = 0
        for (i = 1; i <= n; i++) {
            lead = byte_of(substr(text, i, 1))
            if (lead >= 128 && lead < 192) continue
            if (lead < 224) { columns++; continue }
            if (lead < 240) {
                point = (lead - 224) * 4096 + (byte_of(substr(text, i + 1, 1)) - 128) * 64 \
                    + byte_of(substr(text, i + 2, 1)) - 128
            } else {
                point = (lead - 240) * 262144 + (byte_of(substr(text, i + 1, 1)) - 128) * 4096 \
                    + (byte_of(substr(text, i + 2, 1)) - 128) * 64 \
                    + byte_of(substr(text, i + 3, 1)) - 128
            }
            columns += wide(point) ? 2 : 1
        }
        return columns
    }'

# For the id<TAB>translation<TAB>English rows: prints id<TAB>width of the translation > of the
# English for a longer translation of a table, and for a story text, id<TAB>line N: width > limit
# for each line wider than story_line_width and its English's widest line, and id<TAB>lines: N >
# limit when it has more lines than its English and than a box holds. Widths are display columns
# (awk_width) and do not count a line's tags.
longer_translations() {
  awk -F '\t' -v story_id="^$story_prefix" -v width="$story_line_width" \
    -v rows="$story_box_rows" "$awk_unquote$awk_width"'
        function visible(line) {
            gsub(/\{\{|\}\}/, "_", line); gsub(/\{[^}]*\}/, "", line)
            return display_width(line)
        }
        function widest(text,    lines, n, i, most) {
            n = split(text, lines, "\n"); most = 0
            for (i = 1; i <= n; i++) if (visible(lines[i]) > most) most = visible(lines[i])
            return most
        }
        {
            translation = unquote($2); english = unquote($3)
            if ($1 !~ story_id) {
                if (display_width(translation) > display_width(english))
                    printf "%s\t%d > %d\n", $1, display_width(translation), display_width(english)
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
  kept_ids "$scratch/translated" >"$scratch/kept_ids"
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

# lock and accept write in the pack: it must exist (docs/guides/translate.md#a-new-language).
need_pack() {
  [[ -f "$pack/pack.yaml" ]] || fail "no translation pack in $pack: create its pack.yaml first"
  mkdir -p "$translation_dir"
}

command_lock() {
  need_pack
  load
  english_hashes >"$scratch/hashes"
  # A locked hash wins over the current one, so that only accept clears a stale id.
  cat "$scratch/locked" <(rows_missing_from "$scratch/hashes" "$scratch/locked") >"$scratch/merged"
  write_lock "$scratch/merged"
  echo "wrote $lock_file"
}

command_accept() {
  (($# > 0)) || fail "accept needs at least one id"
  need_pack
  load
  english_hashes >"$scratch/hashes"
  printf '%s\n' "$@" | LC_ALL=C sort >"$scratch/accepted"
  LC_ALL=C join -t $'\t' "$scratch/accepted" <(LC_ALL=C sort "$scratch/hashes") >"$scratch/fresh"
  [[ "$(wc -l <"$scratch/fresh")" -eq $# ]] || fail "some ids are not in the English"
  cat "$scratch/fresh" <(rows_missing_from "$scratch/locked" "$scratch/fresh") >"$scratch/merged"
  write_lock "$scratch/merged"
  echo "accepted $# ids in $lock_file"
}

# The languages of the game besides English: the engine's localization folders, the translation
# packs and the launcher's texts.
known_languages() {
  local path
  {
    for path in "$localization_dir"/*/; do basename "$path"; done
    for path in "$packs_dir"/*-translation/; do basename "$path" -translation; done
    for path in "$launcher_texts"/*.js; do basename "$path" .js; done
  } | grep -E "$language_code" | grep -vx "$reference_language" | sort -u || true
}

# found/total: the keys of English's localization tables (localization/en/<table>.yaml) that a
# language's tables have; a missing key shows in English.
localization_keys() {
  local reference="$localization_dir/$reference_language" files
  [[ -d "$reference" ]] || { echo "-" && return 0; }
  shopt -s nullglob
  files=("$reference"/*.yaml "$localization_dir/$1"/*.yaml)
  shopt -u nullglob
  awk -v reference="$reference/" '
        /^[A-Za-z0-9_.-]+: / {
            table = FILENAME; sub(/.*\//, "", table)
            key = table SUBSEP substr($1, 1, length($1) - 1)
            if (index(FILENAME, reference) == 1) english[key] = 1
            else found[key] = 1
        }
        END {
            for (key in english) { total++; if (key in found) have++ }
            printf "%d/%d\n", have, total
        }' "${files[@]}"
}

# found/total: the launcher's texts of a language (apps/launcher/ui/js/i18n/<code>.js) against
# English's, as its checker counts them; - without node or without the file.
launcher_keys() {
  local counted=""
  if [[ -f "$launcher_texts/$1.js" ]] && command -v node >/dev/null; then
    counted="$(node "$launcher_check" "$1" 2>/dev/null \
      | awk -v code="$1:" '$1 == code { print $2; exit }' || true)"
  fi
  echo "${counted:--}"
}

command_coverage() {
  local languages=("$@") code folder source file
  if ((${#languages[@]} == 0)); then
    while read -r code; do languages+=("$code"); done < <(known_languages)
  fi
  for code in "${languages[@]}"; do
    [[ "$code" =~ $language_code ]] || fail "not a language code: $code"
  done
  find_sources
  english_strings >"$scratch/english"
  english_hashes >"$scratch/hashes"
  cut -f1 "$scratch/english" | sort -u >"$scratch/strings"
  local columns=(strings translated kept missing stale)
  local rows=("${fixed_sources[@]}" "${story_prefix%_}" all) pattern counts
  echo "The original's texts (text/<code> of content/packs/<code>-translation); done is the"
  echo "translated share of the strings that are not kept"
  printf '%-8s %-9s' language source
  printf ' %10s' "${columns[@]}" "done"
  echo
  for code in "${languages[@]}"; do
    folder="$(pack_of "$code")/text/$code"
    translated_strings "$folder" >"$scratch/translated_rows"
    kept_ids "$scratch/translated_rows" | cut -f2 | sort -u >"$scratch/kept"
    cut -f1 "$scratch/translated_rows" | sort -u | comm -12 - "$scratch/strings" \
      >"$scratch/translated"
    comm -23 "$scratch/strings" "$scratch/translated" | comm -23 - "$scratch/kept" \
      >"$scratch/missing"
    join -t $'\t' <(locked_hashes "$folder/.sources.lock" | sort) <(sort "$scratch/hashes") \
      | awk -F '\t' '$2 != $3 { print $1 }' >"$scratch/stale"
    for source in "${rows[@]}"; do
      pattern="$(source_pattern "$source")"
      if [[ "$source" == all ]]; then pattern=.; fi
      counts=()
      for file in "${columns[@]}"; do
        counts+=("$(grep -cE "$pattern" "$scratch/$file" || true)")
      done
      printf '%-8s %-9s' "$code" "$source"
      printf ' %10s' "${counts[@]}"
      awk -v strings="${counts[0]}" -v translated="${counts[1]}" -v kept="${counts[2]}" 'BEGIN {
            if (strings == kept) print "          -"
            else printf " %9.1f%%\n", 100 * translated / (strings - kept)
          }'
    done
  done
  echo
  echo "The engine's texts (content/packs/game/localization/<code>) and the launcher's"
  echo "(apps/launcher/ui/js/i18n/<code>.js): keys the language has of English's"
  printf '%-8s %-14s %12s %12s\n' language pack engine launcher
  for code in "${languages[@]}"; do
    folder="$(pack_of "$code")"
    if [[ -f "$folder/pack.yaml" ]]; then folder="${folder##*/}"; else folder="(none)"; fi
    printf '%-8s %-14s %12s %12s\n' "$code" "$folder" "$(localization_keys "$code")" \
      "$(launcher_keys "$code")"
  done
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
  coverage) command_coverage "$@" ;;
  *)
    usage
    [[ -z "$command" || "$command" == help ]]
    ;;
esac
