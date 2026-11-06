# shellcheck shell=bash
# The publication policy (docs/legal/assets.md) that git's hooks check, sourced by pre-commit and
# commit-msg: the words no added line or commit message may use, the paths no commit may add, the
# identity every commit is authored by, and what a commit message must and must not say. Fast and
# without Docker: only git, grep and awk.

# The identity every commit is authored by.
# shellcheck disable=SC2034 # read by pre-commit
policy_identity="Rivarux <2211799+Rivarux@users.noreply.github.com>"

# The names of the franchise, consoles and companies, in lowercase. A word counts only on its own:
# inside a longer identifier (letters, digits and _ around it) it does not.
policy_banned_words="digimon|mojya|tamer|psx|playstation|ps1|sony|bandai|microsoft|nintendo"

# The paths no commit may add or change, in lowercase (* also matches /): disc images and the files
# of the original, emulator savestates and memory cards, traces and the scratch folders of runs,
# and Claude's own folder.
policy_forbidden_paths=(
  '*.iso' '*.bin' '*.cue' '*.img' '*.chd' '*.pbp' '*.mdf' '*.mds' '*.ccd' '*.sub' '*.ecm' '*.nrg'
  '*.tim' '*.tmd' '*.vh' '*.vb' '*.vab' '*.seq' '*.str' '*.xa' '*psx.exe' '*slus_*' '*scus_*'
  '*sles_*' '*sces_*' 'content/base/*' 'content/cooked/*'
  '*.sav' '*.sstate' '*.state' '*.mcd' '*.mcr' '*.srm'
  '*.jsonl' '_work/*' '*/_work/*'
  '.claude/*' '*/.claude/*'
)

# The allowlist of the banned words: lines of "<path regex><TAB><text regex>". In an added line of
# a file whose path matches, the text that matches is not checked. allow adds them.
policy_allowed=""

# allow <text regex> <glob>...: the text regex (extended, lowercase) is allowed in the files that
# match a glob (* also matches /). Every entry says why.
allow() {
  local text="$1" glob path
  shift
  for glob in "$@"; do
    path="${glob//./\\.}"
    path="${path//\*/.*}"
    policy_allowed+="^${path}\$"$'\t'"$text"$'\n'
  done
}

# The hooks' own list and its test: they spell the words to check them.
allow "$policy_banned_words" scripts/hooks/policy.sh scripts/style/tests/test_policy_hooks.sh
# The story API's list of the words no generated name may contain (the same words, which the Lua
# emitter reads from there), and the tests that feed that list.
allow "$policy_banned_words" packages/formats/include/dwe/formats/script/story_api_names.hpp
allow 'forbiddenwordin\(.*' 'packages/formats/tests/*'
# The decomp's source file that the port cites as "tamer.c: function": a file name, not ours.
allow 'tamer\.c' '*'
# A cited hardware reference and the name of a file on the disc.
allow 'psx-spx|psx\.exe' '*'
# Gamepad families: the player plugs in these pads and the prompts show their buttons (SDL names
# them the same way).
allow 'playstation|nintendo' 'packages/engine/platform/*' 'packages/engine/input/*' \
  'packages/engine/ui/*' \
  packages/engine/script/src/input_bindings.cpp packages/engine/debug/src/debug_ui.cpp \
  'apps/game/src/device_names.*' apps/game/src/ui_demo.cpp \
  'content/packs/*/localization/*/prompt.yaml' docs/adr/0014-mandos-y-entrada.md \
  docs/architecture/ui.md docs/guides/editar-menus.md docs/guides/prueba-vertical.md
# The Windows toolchain and third-party sources name their vendor: vcpkg's repository, the Windows
# SDK's licence, the manifest's XML namespaces and the code page table the charset comes from.
allow microsoft .devcontainer/Dockerfile '.github/workflows/*' 'tooling/cmake/windows/*' \
  scripts/dev.sh \
  docs/adr/0015-desarrollo-en-contenedor.md docs/getting-started/setup.md \
  packages/formats/src/text/charset.cpp docs/formats/textos.md

# What a commit message must not say: co-author trailers, generator lines and the names of AI
# assistants. Extended, matched ignoring case.
policy_assistant_text='co-authored-by|^[^[:alpha:]]*generated with|(^|[^[:alnum:]_])'
policy_assistant_text+='(claude|anthropic|chatgpt|copilot|ai-generated)([^[:alnum:]_]|$)'

# Reads lines of "<path><TAB><line><TAB><text>" and prints "<path>:<line>: <words>" for every line
# that uses a banned word outside its path's allowlist (in $1, policy_allowed's format). Fails when
# it prints any.
policy_scan_words() {
  # shellcheck disable=SC2016 # awk's program
  POLICY_ALLOWED="${1:-}" POLICY_WORDS="$policy_banned_words" LC_ALL=C awk '
    BEGIN {
      FS = "\n"
      rules = split(ENVIRON["POLICY_ALLOWED"], rule, "\n")
      for (i = 1; i <= rules; i++) {
        tab = index(rule[i], "\t")
        rule_path[i] = substr(rule[i], 1, tab - 1)
        rule_text[i] = substr(rule[i], tab + 1)
      }
      word = "(^|[^a-z0-9_])(" ENVIRON["POLICY_WORDS"] ")([^a-z0-9_]|$)"
      found = 0
    }
    {
      tab = index($0, "\t")
      path = substr($0, 1, tab - 1)
      rest = substr($0, tab + 1)
      tab = index(rest, "\t")
      line = substr(rest, 1, tab - 1)
      text = tolower(substr(rest, tab + 1))
      for (i = 1; i <= rules; i++) {
        if (rule_text[i] != "" && path ~ rule_path[i]) gsub(rule_text[i], " ", text)
      }
      words = ""
      while (match(text, word)) {
        hit = substr(text, RSTART, RLENGTH)
        gsub(/[^a-z0-9]/, "", hit)
        words = words (words == "" ? "" : ", ") hit
        text = substr(text, RSTART + RLENGTH - 1)
      }
      if (words != "") {
        print path ":" line ": " words
        found = 1
      }
    }
    END { exit found }
  '
}

# Prints "<path><TAB><line><TAB><text>" for every line the index adds or changes against HEAD.
policy_added_lines() {
  # shellcheck disable=SC2016 # awk's program
  git -c core.quotePath=false diff --cached --no-color --no-ext-diff --no-textconv -U0 \
    --src-prefix=a/ --dst-prefix=b/ --diff-filter=ACMR | LC_ALL=C awk '
    /^diff --git / { header = 1; path = ""; next }
    header && /^\+\+\+ / {
      path = substr($0, 5)
      sub(/\t$/, "", path)
      if (path ~ /^"/) path = substr(path, 2, length(path) - 2)
      path = (path == "/dev/null") ? "" : substr(path, 3)
      next
    }
    /^@@ / {
      header = 0
      split($0, range, " ")
      line = substr(range[3], 2)
      sub(/,.*/, "", line)
      line += 0
      next
    }
    !header && path != "" && /^\+/ { print path "\t" line "\t" substr($0, 2); line++ }
  '
}

# Prints "<path>: <pattern>" for every staged path that a forbidden pattern matches.
policy_forbidden_staged() {
  git diff --cached --name-only -z --diff-filter=ACMRT | policy_forbidden_paths_in
}

# Reads NUL-separated paths and prints "<path>: <pattern>" for every one that a forbidden pattern
# matches, and succeeds only when it prints any. The release checks run it on the packages' files.
policy_forbidden_paths_in() {
  local path lower pattern found=1
  while IFS= read -r -d '' path; do
    lower="${path,,}"
    for pattern in "${policy_forbidden_paths[@]}"; do
      # shellcheck disable=SC2053 # the pattern is a glob
      if [[ "$lower" == $pattern ]]; then
        echo "$path: $pattern"
        found=0
        break
      fi
    done
  done
  return "$found"
}

# The author's identity without its date, as git will record it.
policy_author() {
  local ident
  ident="$(git var GIT_AUTHOR_IDENT)" || return 1
  echo "${ident% * *}"
}
