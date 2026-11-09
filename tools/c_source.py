"""Small lexical C source reader for maintenance reports, not a C preprocessor.

Offsets are preserved when comments, strings and directives are masked. Both
platform branches remain visible. This is intentionally not a compiler or an
implementation/behavior verifier; function-pointer declarators are out of scope.
"""
from dataclasses import dataclass
from pathlib import Path
import re


def mask_non_code(text):
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  text, flags=re.S)


def mask_comments(text):
    """Preserve string/character expressions when classifying a return value."""
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern,
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0])
                  if m[0].startswith(('/*', '//')) else m[0], text, flags=re.S)


def mask_directives(text):
    return re.sub(r'^[ \t]*\#(?:[^\n]*\\\n)*[^\n]*',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]),
                  text, flags=re.M)


@dataclass(frozen=True)
class Function:
    name: str
    start: int
    brace: int
    end: int
    line: int
    lines: int
    params: str
    body: str
    code: str


def functions(text):
    clean = mask_directives(mask_non_code(text))
    pattern = re.compile(
        r'(?:^[ \t]*|(?<=[;}])[ \t]*)(?P<prefix>[A-Za-z_][A-Za-z_0-9 \t*]*?[ \t*])'
        r'(?P<name>[A-Za-z_]\w*)[ \t]*\((?P<params>[^;{}]*)\)[ \t\r\n]*\{', re.M)
    consumed = 0
    for match in pattern.finditer(clean):
        if (match.start() < consumed or match['name'] in {'if', 'while', 'for', 'switch'}
                or match['prefix'].strip().split()[0] in {'function', 'async'}):
            continue
        brace = match.end() - 1
        depth, end = 1, brace + 1
        while end < len(clean) and depth:
            if clean[end] == '{':
                depth += 1
            elif clean[end] == '}':
                depth -= 1
            end += 1
        if depth:
            raise ValueError('Unbalanced function near line %d' % (text.count('\n', 0, match.start()) + 1))
        consumed = end
        yield Function(match['name'], match.start(), brace, end,
                       text.count('\n', 0, match.start()) + 1,
                       text.count('\n', match.start(), end) + 1,
                       text[match.start('params'):match.end('params')],
                       text[brace + 1:end - 1], clean[brace + 1:end - 1])


def source_files(root):
    return sorted((Path(root) / 'src').rglob('*.c'))
