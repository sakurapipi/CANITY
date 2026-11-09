"""Build-cache identities; these cases invoke no compiler and no game."""
from pathlib import Path
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_cache import file_digest, include_digest, read_profile, write_profile


class BuildCacheCases(unittest.TestCase):
    def test_content_change_is_detected_even_if_mtime_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'source.c'
            path.write_text('old', encoding='utf-8')
            stamp = path.stat()
            before = file_digest(path)
            path.write_text('new', encoding='utf-8')
            os.utime(path, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
            self.assertNotEqual(file_digest(path), before)

    def test_nested_headers_and_source_inc_files_change_the_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'include' / 'compat').mkdir(parents=True)
            (root / 'src').mkdir()
            header = root / 'include' / 'compat' / 'types.h'
            header.write_text('one', encoding='utf-8')
            before = include_digest(root)
            header.write_text('two', encoding='utf-8')
            changed = include_digest(root)
            self.assertNotEqual(before, changed)
            (root / 'src' / 'table.inc').write_text('table', encoding='utf-8')
            self.assertNotEqual(changed, include_digest(root))

    def test_missing_and_invalid_profiles_require_a_fresh_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.json'
            self.assertEqual(read_profile(path), {})
            path.write_text('broken JSON', encoding='utf-8')
            self.assertEqual(read_profile(path), {})
            path.write_text('[]', encoding='utf-8')
            self.assertEqual(read_profile(path), {})

    def test_profile_roundtrip_leaves_no_temporary_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'profile.json'
            profile = dict(configuration=dict(flags=['-Werror']), sources={'src/game.c': 'hash'})
            write_profile(path, profile)
            self.assertEqual(read_profile(path), profile)
            self.assertEqual(list(root.glob('compile-*.tmp')), [])


if __name__ == '__main__':
    unittest.main()
