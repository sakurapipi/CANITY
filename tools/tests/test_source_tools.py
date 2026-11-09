"""Regression cases for source triage. Authored here; execution is user-owned."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from c_source import functions
from stub_scan import classify, state_return_expression


class SourceTriageCases(unittest.TestCase):
    def test_comments_and_string_braces_do_not_change_function_extent(self):
        text = '/* int fake(void) { } */\nint real(void) { puts("}"); return 3; }'
        found = list(functions(text))
        self.assertEqual([item.name for item in found], ['real'])
        self.assertEqual(text[found[0].end - 1], '}')
        self.assertIn('return 3;', found[0].body)

    def test_same_line_catalog_functions_are_not_lost(self):
        text = 'static int first(void) {return 0;}static int second(void) {return 1;}'
        self.assertEqual([item.name for item in functions(text)], ['first', 'second'])

    def test_state_query_is_not_a_constant_stub(self):
        text = 'int active(void) {return actor.active;}'
        self.assertIsNone(classify(text, next(functions(text))))

    def test_query_report_exposes_the_actual_state_expression(self):
        text = 'int active(void) {return actor.active;}'
        self.assertEqual(state_return_expression(next(functions(text))), 'actor.active')

    def test_function_calls_and_increment_are_not_claimed_as_plain_state_reads(self):
        for text in ('int value(void) {return next();}', 'int value(void) {return count++;}'):
            self.assertIsNone(state_return_expression(next(functions(text))))

    def test_returning_a_string_is_not_an_empty_return(self):
        text = 'const char* status(void) {return "WEB";}'
        self.assertIsNone(classify(text, next(functions(text))))

    def test_unmarked_numeric_result_is_a_candidate(self):
        text = 'int active(void) {return 0;}'
        self.assertEqual(classify(text, next(functions(text)))['status'], 'unmarked')

    def test_explicit_unimplemented_reason_remains_visible(self):
        text = 'int active(void) { /* STUB: device backend not connected. */ return 0; }'
        self.assertEqual(classify(text, next(functions(text)))['status'], 'stub')

    def test_prior_function_marker_cannot_exempt_the_next_function(self):
        text = 'void old(void) { /* STUB: not connected. */ }\nint fresh(void) {return 0;}'
        self.assertEqual(classify(text, list(functions(text))[1])['status'], 'unmarked')

    def test_fixed_rule_requires_a_nonempty_reason(self):
        text = 'int fixed(void) { /* FIXED_RESULT: */ return 0; }'
        self.assertEqual(classify(text, next(functions(text)))['status'], 'unmarked')

    def test_stated_fixed_rule_is_classified(self):
        text = 'int fixed(void) { /* FIXED_RESULT: gold never changes speed. */ return 1; }'
        self.assertEqual(classify(text, next(functions(text)))['status'], 'fixed_result')


if __name__ == '__main__':
    unittest.main()
