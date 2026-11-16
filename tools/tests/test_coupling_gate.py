"""2.4.6-RC numeric debt-ceiling regressions."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from coupling_gate import Violation, load_baseline, split_debt, function_inventory


class CouplingDebtCases(unittest.TestCase):
    def issue(self, value, target="src/sprites.c"):
        return Violation("file_lines", target, value, 2600, "arbitrary diagnostic text")

    def test_debt_reduction_remains_known(self):
        known, fresh = split_debt([self.issue(3000)], {("file_lines", "src/sprites.c"): 3863})
        self.assertEqual((len(known), len(fresh)), (1, 0))

    def test_worse_debt_is_blocked(self):
        self.assertEqual(len(split_debt([self.issue(3864)],
                         {("file_lines", "src/sprites.c"): 3863})[1]), 1)

    def test_new_file_cannot_inherit_a_different_files_allowance(self):
        self.assertEqual(len(split_debt([self.issue(3000, "src/new.c")],
                         {("file_lines", "src/sprites.c"): 3863})[1]), 1)

    def test_function_line_number_does_not_define_its_identity(self):
        issue = Violation("function_lines", "src/x.c::draw", 350, 300, "moved to line 900")
        self.assertEqual(len(split_debt([issue], {issue.key: 360})[0]), 1)

    def test_legacy_text_baseline_is_translated_to_numeric_ceiling(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/"old.txt"
            path.write_text("文件 src/sprites.c 有 3863 行 > 2600\n", "utf-8")
            self.assertEqual(load_baseline(path), {("file_lines", "src/sprites.c"): 3863})

    def test_duplicate_baseline_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/"bad.txt"
            path.write_text("文件 src/sprites.c 有 3863 行 > 2600\n" * 2, "utf-8")
            with self.assertRaises(ValueError):load_baseline(path)

    def test_missing_baseline_is_not_silently_accepted(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(FileNotFoundError):load_baseline(Path(temp)/"absent.json")

    def test_prototypes_comments_and_literal_braces_are_not_functions(self):
        with tempfile.TemporaryDirectory() as temp:
            src=Path(temp)/"src";src.mkdir()
            (src/"example.c").write_text('int prototype(void);\n/* void fake(void) { } */\n'
                'int actual(void) { puts("}"); return 1; }\n', "utf-8")
            with patch("coupling_gate.ROOT", temp):rows=function_inventory()
            self.assertEqual([row[1] for row in rows], ["actual"])


if __name__ == "__main__":
    unittest.main()
