"""Single authoritative source list for CANITY builds.

本地 build_validate.py、GitHub CI 和 Release 工作流都从这里取清单，
避免三处手写目录列表逐渐漂移（v2.3.8 就漏过 src/save/）。

用法：
    python tools/build_sources.py            # 打印游戏源文件，一行一个
    python tools/build_sources.py --check     # 与磁盘实际文件比对，不一致退出 1
"""
from pathlib import Path
import sys


def game_sources(root):
    """递归收集 src/**/*.c —— 子目录（src/save、src/steam）自动包含。"""
    root = Path(root)
    return sorted((root / 'src').rglob('*.c'))


def test_canity_sources(root):
    """离线自检：纯 C99，只需要 steam_ach + save_container 两个真实实现。"""
    root = Path(root)
    return [
        root / 'tests' / 'test_canity.c',
        root / 'src' / 'steam' / 'steam_ach.c',
        root / 'src' / 'save' / 'save_container.c',
    ]


def _main(argv):
    root = Path(__file__).resolve().parents[1]
    if '--check' in argv:
        found = game_sources(root)
        if not found:
            print('ERROR: no .c files under src/', file=sys.stderr)
            return 1
        missing = [p for p in found if not p.is_file()]
        if missing:
            print('ERROR: listed but missing: %s' % missing, file=sys.stderr)
            return 1
        dirs = sorted({p.parent.relative_to(root).as_posix() for p in found})
        print('sources: %d .c files in %s' % (len(found), ', '.join(dirs)))
        return 0
    for p in game_sources(root):
        print(p.relative_to(root).as_posix())
    return 0


if __name__ == '__main__':
    sys.exit(_main(sys.argv[1:]))
