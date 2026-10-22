"""Tests of scripts/style/wrap_comments.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import wrap_comments  # noqa: E402

LIMIT = wrap_comments.MAX_COLUMNS


def words(count, word="word"):
    return " ".join([word] * count)


class WrapCommentsTests(unittest.TestCase):
    def test_a_comment_within_the_limit_stays(self):
        lines = ["# " + words(10)]
        self.assertEqual(wrap_comments.wrap(lines, "#"), lines)

    def test_a_long_comment_breaks_into_a_new_line(self):
        wrapped = wrap_comments.wrap(["  # " + words(30)], "#")
        self.assertEqual(len(wrapped), 2)
        self.assertTrue(all(len(line) <= LIMIT for line in wrapped))
        self.assertTrue(wrapped[1].startswith("  # word"))
        self.assertEqual(" ".join(line[4:] for line in wrapped), words(30))

    def test_the_words_that_move_join_the_next_line_of_the_paragraph(self):
        wrapped = wrap_comments.wrap(["-- " + words(22), "-- end."], "--")
        self.assertEqual(len(wrapped), 2)
        self.assertTrue(wrapped[1].endswith("word end."))

    def test_a_list_item_goes_on_under_its_text(self):
        wrapped = wrap_comments.wrap(["# - " + words(22), "# - next"], "#")
        self.assertEqual(wrapped[1], "#   " + words(3))
        self.assertEqual(wrapped[2], "# - next")

    def test_code_directives_and_unbreakable_lines_stay(self):
        lines = [
            "echo " + words(30),
            "# shellcheck disable=SC2034 " + words(20),
            "# " + "x" * (LIMIT + 5),
        ]
        self.assertEqual(wrap_comments.wrap(lines, "#"), lines)

    def test_here_documents_stay(self):
        lines = ["cat <<EOF", "# " + words(30), "EOF", "# " + words(30)]
        wrapped = wrap_comments.wrap(lines, "#")
        self.assertEqual(wrapped[:3], lines[:3])
        self.assertEqual(len(wrapped), 5)

    def test_lua_long_brackets_stay(self):
        lines = ["local text = [[", "-- " + words(30), "]]"]
        self.assertEqual(wrap_comments.wrap(lines, "--"), lines)

    def test_wrapping_twice_changes_nothing(self):
        once = wrap_comments.wrap(["# " + words(60), "# more"], "#")
        self.assertEqual(wrap_comments.wrap(once, "#"), once)


if __name__ == "__main__":
    unittest.main()
