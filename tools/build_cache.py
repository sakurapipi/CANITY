"""Content identities for incremental compilation; no compiler is invoked here."""
import hashlib
import json
from pathlib import Path
import tempfile


def file_digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inputs_digest(root, paths):
    root = Path(root)
    digest = hashlib.sha256()
    for path in sorted(set(Path(p) for p in paths)):
        relative = path.relative_to(root).as_posix().encode('utf-8')
        digest.update(relative + b'\0')
        digest.update(file_digest(path).encode('ascii') + b'\0')
    return digest.hexdigest()


def include_digest(root):
    root = Path(root)
    paths = list((root / 'include').rglob('*.h'))
    paths += list((root / 'include').rglob('*.inc'))
    paths += list((root / 'src').rglob('*.inc'))
    paths += list((root / 'third_party').rglob('*.h'))
    return inputs_digest(root, paths)


def tool_identity(path):
    path = Path(path).resolve()
    stat = path.stat()
    return dict(path=str(path), size=stat.st_size, modified_ns=stat.st_mtime_ns)


def read_profile(path):
    try:
        value = json.loads(Path(path).read_text(encoding='utf-8'))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def write_profile(path, profile):
    """Publish compilation metadata only after successful linking, atomically."""
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='compile-', suffix='.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(profile, stream, sort_keys=True, indent=2)
            stream.write('\n')
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def validation_digest(root):
    """Bind evidence to code, tests, validation tools and workflows, not build outputs."""
    root=Path(root);paths=[]
    for directory in ('src','include','tests','tools','l10n','third_party','.github'):
        for path in (root/directory).rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix.lower() in {'.c','.h','.inc','.py','.json','.tsv','.yml','.yaml','.sav'}:
                paths.append(path)
    return inputs_digest(root,paths)
