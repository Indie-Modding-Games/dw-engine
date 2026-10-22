#!/usr/bin/env python3
"""Wraps the whole-line comments of Lua and shell files that pass the column limit.

StyLua and shfmt leave comments as they are, so this does for them what clang-format does for C++:
a comment line that passes the limit breaks at the last space that fits, and the words that move go
to the start of the next line when that line goes on with the same paragraph, or to a new line
otherwise. A list item ("- ", "* ", "1. ") goes on indented under its text. Lines that cannot
break (a single long word), directives (shellcheck, luacheck, NOLINT), shebangs, here-documents and
Lua's long comments and strings stay as they are.

Usage: wrap_comments.py [--check] [--] FILE...
With --check it changes nothing, names the files it would change and fails if there is any.
"""

import re
import sys
from pathlib import Path

MAX_COLUMNS = 100
LUA_MARKER = "--"
SHELL_MARKER = "#"
LIST_ITEM = re.compile(r"([-*]|\d+[.)]) ")
DIRECTIVES = ("shellcheck ", "luacheck:", "stylua:", "NOLINT")
HEREDOC_START = re.compile(r"(?<!<)<<-?(?!<)\s*(['\"]?)([A-Za-z_][A-Za-z0-9_]*)\1")
LUA_LONG_OPEN = re.compile(r"\[(=*)\[")


def marker_of(path):
    return LUA_MARKER if path.suffix == ".lua" else SHELL_MARKER


def comment_parts(line, marker):
    """The prefix (indent, marker, spaces) and text of a whole-line comment, or None."""
    match = re.fullmatch(r"( *)" + re.escape(marker) + r"( +)(\S.*)", line)
    if match is None:
        return None
    text = match.group(3)
    if text.startswith(DIRECTIVES):
        return None
    return match.group(1) + marker + match.group(2), text


def verbatim_lines(lines, marker):
    """The indices of the lines to keep as they are: shell's here-documents, Lua's long brackets."""
    verbatim = set()
    closing = None
    for index, line in enumerate(lines):
        if closing is not None:
            verbatim.add(index)
            if (marker == SHELL_MARKER and line.strip() == closing) or (
                marker == LUA_MARKER and closing in line
            ):
                closing = None
            continue
        if marker == SHELL_MARKER:
            heredoc = HEREDOC_START.search(line)
            if heredoc is not None and not line.lstrip().startswith(SHELL_MARKER):
                closing = heredoc.group(2)
        else:
            long_open = LUA_LONG_OPEN.search(line)
            if long_open is not None:
                close = "]" + long_open.group(1) + "]"
                if close not in line[long_open.end() :]:
                    verbatim.add(index)
                    closing = close
    return verbatim


def split_at_limit(prefix, text):
    """The words of text that fit after prefix, and the rest; None when not even one fits."""
    room = MAX_COLUMNS - len(prefix)
    cut = text.rfind(" ", 0, room + 1)
    if cut <= 0:
        return None
    return text[:cut].rstrip(), text[cut + 1 :].lstrip()


def continues_paragraph(parts, prefix):
    if parts is None:
        return False
    next_prefix, next_text = parts
    return next_prefix == prefix and LIST_ITEM.match(next_text) is None


def wrap(lines, marker):
    verbatim = verbatim_lines(lines, marker)
    result = list(lines)
    index = 0
    while index < len(result):
        line = result[index]
        parts = None if index in verbatim else comment_parts(line, marker)
        if parts is None or len(line) <= MAX_COLUMNS:
            index += 1
            continue
        prefix, text = parts
        split = split_at_limit(prefix, text)
        if split is None:
            index += 1
            continue
        head, rest = split
        follow_prefix = prefix + "  " if LIST_ITEM.match(text) else prefix
        result[index] = prefix + head
        next_index = index + 1
        next_parts = None
        if next_index < len(result) and next_index not in verbatim:
            next_parts = comment_parts(result[next_index], marker)
        if continues_paragraph(next_parts, follow_prefix):
            result[next_index] = follow_prefix + rest + " " + next_parts[1]
        else:
            result.insert(next_index, follow_prefix + rest)
            verbatim = {line + 1 if line >= next_index else line for line in verbatim}
        index += 1
    return result


def wrap_file(path, check):
    """Whether the file needed wrapping; writes it unless check."""
    original = path.read_text(encoding="utf-8")
    lines = original.split("\n")
    wrapped = "\n".join(wrap(lines, marker_of(path)))
    if wrapped == original:
        return False
    if not check:
        path.write_text(wrapped, encoding="utf-8")
    return True


def main(arguments):
    check = False
    if arguments[:1] == ["--check"]:
        check = True
        arguments = arguments[1:]
    if arguments[:1] == ["--"]:
        arguments = arguments[1:]
    changed = [name for name in arguments if wrap_file(Path(name), check)]
    if check and changed:
        for name in changed:
            print(f"{name}: comments past {MAX_COLUMNS} columns (scripts/dev.sh format wraps them)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
