"""Report lexical C function sizes. Run manually after refactoring; no build is run."""
import argparse
import json
from pathlib import Path
from c_source import functions, source_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--threshold', type=int, default=200)
    parser.add_argument('--max-count', type=int, default=10)
    parser.add_argument('--hard-limit', type=int, default=300)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    if args.threshold < 1 or args.max_count < 0 or args.hard_limit < args.threshold:
        parser.error('require 1 <= threshold <= hard-limit and max-count >= 0')
    paths = source_files(args.root)
    if not paths:
        parser.error('root has no src/**/*.c files')
    rows = []
    try:
        for path in paths:
            for func in functions(path.read_text(encoding='utf-8')):
                if func.lines > args.threshold:
                    rows.append(dict(file=path.relative_to(args.root).as_posix(),
                                     name=func.name, line=func.line, lines=func.lines))
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(2, 'Cannot read function inventory: %s\n' % error)
    rows.sort(key=lambda row: (-row['lines'], row['file'], row['line']))
    oversized = [row for row in rows if row['lines'] > args.hard_limit]
    result = dict(method='lexical; comments and blank lines included; both platform branches visible',
                  threshold=args.threshold, count=len(rows), max_count=args.max_count,
                  hard_limit=args.hard_limit, hard_limit_count=len(oversized), functions=rows,
                  passed=len(rows) <= args.max_count and not oversized)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for row in rows:
            print('%s:%d %s %d lines' % (row['file'], row['line'], row['name'], row['lines']))
        print('%d functions > %d; allowed %d; %d above hard limit %d' %
              (len(rows), args.threshold, args.max_count, len(oversized), args.hard_limit))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
