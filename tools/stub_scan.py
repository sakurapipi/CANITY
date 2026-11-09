"""Find unclassified empty/literal-return C implementations, not one-line getters.

This is a source triage tool, not proof that a system is wired or correct. State
expressions such as collector.active are NOT constants. Macro-return functions
are listed separately without guessing what the macro expands to.
"""
import argparse
import json
from pathlib import Path
import re
from c_source import functions, source_files, mask_comments, mask_directives

NUMBER = r'[+-]?(?:0[xX][0-9a-fA-F]+[uUlL]*|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fFuUlL]*)'
MARKER = re.compile(r'\b(STUB|FIXED_RESULT|PLATFORM_NOOP)\s*:\s*([^\n*]+)')


def state_return_expression(func):
    """Describe plain state reads without inferring that they are constants."""
    code = mask_directives(mask_comments(func.body)).strip()
    match = re.fullmatch(r'return\s+([A-Za-z_]\w*(?:\s*(?:\.|->)\s*[A-Za-z_]\w*)*)\s*;', code)
    if match and not re.fullmatch(r'[A-Z_][A-Z_0-9]*', match[1]):
        return match[1]
    return None


def classify(text, func):
    code = re.sub(r'\(\s*void\s*\)\s*[A-Za-z_]\w*\s*;', '',
                  mask_directives(mask_comments(func.body))).strip()
    if not code or re.fullmatch(r'return\s*;', code):
        shape = 'no_op'
    elif re.fullmatch(r'return\s+(?:'+NUMBER+r'|NULL|true|false)\s*;', code):
        shape = 'literal_return'
    elif re.fullmatch(r'return\s+[A-Z_][A-Z_0-9]*\s*;', code):
        return dict(shape='symbolic_return', status='review_info', reason='symbol is not expanded by this tool')
    else:
        return None
    # Only a body marker or the immediately preceding comment belongs to a function.
    # A file-wide STUB comment must not exempt every later implementation.
    prefix = text[:func.start].rstrip()
    comment = re.search(r'/\*[^*]*(?:\*(?!/)[^*]*)*\*/$', prefix)
    nearby = (comment[0] if comment else '') + '\n' + func.body
    marker = MARKER.search(nearby)
    reason = marker[2].strip() if marker else ''
    return dict(shape=shape, status=marker[1].lower() if reason else 'unmarked',
                reason=reason or 'empty or literal-only implementation needs classification')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--strict-stubs', action='store_true', help='also fail for explicitly unimplemented STUB entries')
    parser.add_argument('--include-queries', action='store_true', help='also show plain state reads and their actual expressions')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    paths = source_files(args.root)
    if not paths:
        parser.error('root has no src/**/*.c files')
    rows = []
    try:
        for path in paths:
            text = path.read_text(encoding='utf-8')
            for func in functions(text):
                category = classify(text, func)
                if category is None and args.include_queries:
                    expression = state_return_expression(func)
                    if expression:
                        category = dict(shape='state_read', status='query_info', reason=expression)
                if category:
                    rows.append(dict(file=path.relative_to(args.root).as_posix(), name=func.name,
                                     line=func.line, **category))
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(2, 'Cannot read stub candidates: %s\n' % error)
    unmarked = sum(row['status'] == 'unmarked' for row in rows)
    stubs = sum(row['status'] == 'stub' for row in rows)
    result = dict(method='lexical numeric/empty-body triage; no macro expansion or behavior verification',
                  unmarked=unmarked, declared_stubs=stubs, entries=rows,
                  passed=unmarked == 0 and (not args.strict_stubs or stubs == 0))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for row in rows:
            print('%s:%d %s [%s] %s' % (row['file'], row['line'], row['name'], row['status'], row['reason']))
        print('%d unmarked; %d explicitly unimplemented; %d total candidates/info' % (unmarked, stubs, len(rows)))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
