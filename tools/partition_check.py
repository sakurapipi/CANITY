#!/usr/bin/env python3
"""分区规划检查 —— 防止系统头重新散回各个 .c

规则来自 include/common.h 顶部：

    common.h   系统头 / 全部常量 / 跨模块共享的类型 / 纯数学工具
    约定：除本文件外，任何 .c / .h 都不再直接包含系统头。

用法：
    python -X utf8 tools/partition_check.py            # 只检查，违规返回 1
    python -X utf8 tools/partition_check.py --fix-hint # 附带修复建议

2026-11-03 清过一次：25 个文件删掉 40 行系统头，
lang.c / shop.c / steam_ach.c 各补一行 common.h。
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SYS = {
    'windows.h', 'mmsystem.h', 'math.h', 'stdio.h', 'stdlib.h',
    'string.h', 'time.h', 'stdint.h', 'stddef.h', 'stdint.h',
    'stdarg.h', 'limits.h', 'float.h', 'assert.h', 'ctype.h',
}

# 允许直接碰系统头的位置：
#   common.h        —— 唯一入口
#   src/platform/*  —— 平台层（2026-11-06 网页版移植时新增）
#
# 为什么平台层要例外：这一层的**唯一职责**就是把系统 API 关在笼子里。
# Win32 后端要用 windows.h / gdi32，Emscripten 后端要用 emscripten.h，
# 两边的系统头根本不兼容 —— 强迫它们都塞进 common.h 会把
# "#ifdef __EMSCRIPTEN__" 撒满整个工程，正是这层要消除的东西。
# 界限仍然清晰：plat_*.c 之外一个系统头都不许有。
ALLOW = {'common.h'}
ALLOW_DIRS = (os.path.join('src', 'platform'),)


def iter_files():
    for sub in ('src', 'include'):
        d = os.path.join(ROOT, sub)
        if not os.path.isdir(d):
            continue
        for root, _dirs, files in os.walk(d):
            for f in sorted(files):
                if f.endswith('.c') or f.endswith('.h'):
                    yield os.path.join(root, f)
                elif f.endswith('.inc'):
                    yield os.path.join(root, f)


# ==================================================================
# 存档分区规则（2026-11-05）
# ------------------------------------------------------------------
# src/save/ 是存档分区，src/steam/ 是平台分区。分工：
#   存档分区  →  生产 / 消费存档字节（含 SteamCloudHead 信封）
#   Steam 专区 →  只负责搬运 + 冲突判定
#
# 规则 S1（已有，全局生效）：任何 .c/.h 出现系统头 → FAIL
#   src/save/* 自然也被这条覆盖（只允许 include "common.h"）。
#
# 规则 S2（新增）：**除存档后端外，谁都不许自己碰存档文件**。
#   否则会绕开 tmp + fsync + 原子改名 和 .bak 链 ——
#   steam_zone.c 原来就是这么绕的（缺陷 ④）。
SAVE_OWNER = os.path.join('src', 'save', 'save_backend.c')

SAVE_FILE_PAT = re.compile(
    r'fopen\s*\([^)]*\.sav|CreateFileA\s*\([^)]*\.sav', re.IGNORECASE)


def check_save_partition(bad):
    for p in iter_files():
        rel = os.path.relpath(p, ROOT)
        if os.path.normpath(rel) == os.path.normpath(SAVE_OWNER):
            continue
        try:
            txt = io.open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for i, ln in enumerate(txt.split('\n')):
            if ln.lstrip().startswith('*') or ln.lstrip().startswith('/*'):
                continue
            if SAVE_FILE_PAT.search(ln):
                bad.append((rel, i + 1, 'sav-file'))
    return bad


def main():
    bad = []
    for p in iter_files():
        name = os.path.basename(p)
        if name in ALLOW:
            continue
        rel = os.path.relpath(p, ROOT)
        # 平台层文件豁免（见 ALLOW_DIRS 的说明）
        if any(os.path.normpath(rel).startswith(os.path.normpath(d) + os.sep) for d in ALLOW_DIRS):
            continue
        try:
            txt = io.open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for i, ln in enumerate(txt.split('\n')):
            m = re.search(r'#\s*include\s*<([^>]+)>', ln)
            if m and m.group(1).strip() in SYS:
                bad.append((rel, i + 1, m.group(1).strip()))

    # 规则 S2：存档文件只许存档后端碰
    save_bad = check_save_partition([])

    if not bad and not save_bad:
        print('partition: OK  (系统头只在 common.h 与 src/platform/ 里出现；存档文件只在 save 后端读写)')
        return 0

    if bad:
        print('partition: FAIL  —— 有 %d 处系统头散在外面' % len(bad))
        for rel, line, hdr in bad:
            print('  %-32s %4d  <%s>' % (rel, line, hdr))
        if '--fix-hint' in sys.argv:
            print()
            print('修法：')
            print('  ① 文件已经包含 common.h → 直接删掉这行系统头')
            print('  ② 没包含 common.h      → 换成 #include "common.h"')

    if save_bad:
        print('save partition: FAIL  —— 有 %d 处绕开存档后端直接读写 .sav' % len(save_bad))
        for rel, line, _ in save_bad:
            print('  %-32s %4d  fopen/CreateFileA(... *.sav)' % (rel, line))
        print()
        print('为什么不行：绕开 src/save/save_backend.c 会丢掉')
        print('  · tmp + FlushFileBuffers + MoveFileEx 的原子写（写一半掉电 = 半截档）')
        print('  · .bak 备份链')
        print('  · 信封（SteamCloudHead）封头 —— 云存档就认不出来')
        print('修法：改调 SaveBackendWrite / SaveBackendRead / SaveBackendUpload。')
    return 1


if __name__ == '__main__':
    sys.exit(main())
