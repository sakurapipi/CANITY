"""Single authoritative source list for CANITY builds.

本地 build_validate.py、GitHub CI 和 Release 工作流都从这里取清单，
避免三处手写目录列表逐渐漂移（v2.3.8 就漏过 src/save/）。

用法：
    python tools/build_sources.py            # 打印游戏源文件，一行一个
    python tools/build_sources.py --check     # 与磁盘实际文件比对，不一致退出 1
"""
from pathlib import Path
import sys


# 平台层只有一个后端能参与某一次构建：
#   桌面构建排除 plat_web.c（它要 emscripten.h）
#   网页构建排除 plat_win32.c（它要 windows.h）
# 两边都叫 plat_*.c，靠文件名区分。
PLATFORM_FILES = {
    'win32': {
        # 网页后端
        'plat_web.c',
        'main_web.c',
        'audio_web.c',
        'music_web.c',
        'gamepad_web.c',
        'steam_zone_web.c',
    },
    'web': {
        # 桌面后端与 Win32 专属实现
        'plat_win32.c',
        'main.c',           # WinMain + 消息循环
        'audio.c',          # waveOut + 线程
        'music.c',          # MCI
        'music_proc.c',     # Interlocked*
        'gamepad.c',        # XInput 动态加载
        'steam_zone.c',     # Steamworks
    },
}


def game_sources(root, platform='win32'):
    """递归收集 src/**/*.c —— 子目录（src/save、src/steam、src/platform）自动包含。

    platform 决定排除哪个平台后端；默认 win32，保持既有调用方不变。
    """
    root = Path(root)
    skip = PLATFORM_FILES.get(platform, {'plat_web.c'})
    return [p for p in sorted((root / 'src').rglob('*.c')) if p.name not in skip]


def test_canity_sources(root):
    """离线自检：纯 C99，只链无系统依赖的真实实现。

    v2.4-rc：加入 room_template.c —— 它是纯数据 + 纯函数，
    没有任何 rand()/worldMap/Win32 依赖，所以能被真正单测
    （旋转、边界、可通行格数），而不是查源码字符串。
    """
    root = Path(root)
    return [
        root / 'tests' / 'test_canity.c',
        root / 'src' / 'steam' / 'steam_ach.c',
        root / 'src' / 'save' / 'save_container.c',
        root / 'src' / 'room_template.c',
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
