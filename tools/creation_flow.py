"""CANITY creation workflow: prepare reusable specs and bounded source context.

This tool writes authoring artifacts only. It does not modify runtime sources,
launch the game, run tests, commit, push or invent successful acceptance results.
"""
import argparse
import json
import re
from pathlib import Path
from c_source import functions

ROOT = Path(__file__).resolve().parents[1]
CATALOG = Path(__file__).with_name('creation_recipes.json')


def load_catalog():
    return json.loads(CATALOG.read_text(encoding='utf-8'))


def safe_id(value):
    if not re.fullmatch(r'[a-z][a-z0-9_]{1,63}', value):
        raise argparse.ArgumentTypeError('Use a stable lower_case ASCII ID (2–64 characters).')
    return value


def create_task(args, catalog):
    recipe = catalog['recipes'][args.kind]
    output = args.output or ROOT/'work/creation'/args.id
    output.mkdir(parents=True, exist_ok=True)
    task = {'schema_version': 1, 'kind': args.kind, 'id': args.id,
            'name_zh': args.name, 'status': 'draft',
            'spec': {field: '' for field in recipe['required']},
            'symbols': {}, 'affected_files': recipe['entry_files'],
            'contracts': recipe['contracts'], 'invariants': catalog['invariants'],
            'acceptance': {'owner': 'user', 'status': 'not_run'},
            'delivery': {'compile': False, 'package': False, 'commit': False, 'push': False}}
    task['spec'].update(id=args.id, name_zh=args.name)
    target = output/'task.json'
    with target.open('x', encoding='utf-8') as stream:
        json.dump(task, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    (output/'handoff.md').write_text(
        '# '+args.name+'\n\nAuthoring task: `task.json`.\n\n'
        'Complete the spec before implementing. Read only the selected entry points.\n'
        'List changed producers, consumers, reset/save paths and UI bindings here.\n'
        'Acceptance: NOT RUN, user-owned. No automatic commit or push.\n', encoding='utf-8')
    print(target)


def write_context(args, catalog):
    task = json.loads(args.task.read_text(encoding='utf-8-sig'))
    kind = task['kind']
    recipe = catalog['recipes'][kind]
    # Authoring requests may select repo files, never escape via ../ or a symlink.
    requested = task.get('affected_files') or recipe['entry_files']
    files = []
    for relative in requested:
        path = (ROOT/relative).resolve()
        if not path.is_relative_to(ROOT) or path.suffix not in ('.c', '.h', '.inc', '.html'):
            raise SystemExit('Context must refer to supported files inside the repository.')
        files.append((relative, path))
    chunks = ['# Bounded creation context\n', json.dumps(task, ensure_ascii=False, indent=2)]
    remaining = args.max_chars - sum(len(c) for c in chunks)
    if remaining < 1024:
        raise SystemExit('Specification exceeds context budget; shorten it or raise --max-chars.')
    selected = task.get('symbols', {})
    for number, (relative, path) in enumerate(files):
        chunks.append('\n## '+relative+'\n')
        if not path.is_file():
            chunks.append('Missing source; resolve this entry before editing.\n')
            continue
        text = path.read_text(encoding='utf-8-sig')
        names = selected.get(relative, [])
        parsed = functions(text) if path.suffix in ('.c', '.h') else []
        if names:
            matches = [f for f in parsed if f.name in names]
            body = '\n\n'.join(text[f.start:f.end] for f in matches)
            missing = set(names)-{f.name for f in matches}
            if missing:
                body += '\nUnresolved symbols: '+', '.join(sorted(missing))
        elif path.suffix in ('.h', '.inc'):
            body = text
        elif parsed:
            body = 'Function index (select names in task.symbols to expand):\n'+'\n'.join(f.name for f in parsed)
        else:
            body = '\n'.join(text.splitlines()[:50])
        budget = max(0, remaining//max(1, len(files)-number))
        snippet = body[:budget]
        if len(body)>budget:
            snippet += '\n[Bounded excerpt; narrow the selected symbols for full bodies.]'
        chunks.append(snippet)
        remaining -= len(snippet)
    target = args.output or args.task.with_name('context.md')
    target.write_text('\n'.join(chunks), encoding='utf-8')
    print(target)


def main():
    catalog = load_catalog()
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    subs.add_parser('list')
    new = subs.add_parser('new')
    new.add_argument('--kind', choices=sorted(catalog['recipes']), required=True)
    new.add_argument('--id', type=safe_id, required=True)
    new.add_argument('--name', required=True)
    new.add_argument('--output', type=Path)
    context = subs.add_parser('context')
    context.add_argument('task', type=Path)
    context.add_argument('--max-chars', type=int, default=18000)
    context.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.command == 'list':
        for kind, recipe in catalog['recipes'].items():
            print(kind+': '+', '.join(recipe['required']))
    elif args.command == 'new':
        create_task(args, catalog)
    else:
        if not 4096<=args.max_chars<=120000:
            parser.error('--max-chars must be between 4096 and 120000')
        write_context(args, catalog)


if __name__ == '__main__':
    main()
