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
    # 2026-11-08（架构审计 P1-1）：这 4 个以前不在表里 → 违规被漏判。
    # 实测 steam_zone.c 自己 include 了 <stdbool.h>，检查器却报 OK。
    'stdbool.h', 'errno.h', 'wchar.h', 'locale.h',
}

# 允许直接碰系统头的位置：**只有 common.h 一个**（2026-11-06 恢复严格）。
#
# 网页版移植时曾经给 src/platform/ 和 include/compat/ 开过例外，
# 那是错的：一旦允许第二个文件包含系统头，"唯一入口"就名存实亡。
# 正确做法是把平台差异**收进 common.h**：
#   #ifdef CANITY_WEB  -> emscripten.h + Win32 类型/键码垫片
#   #else              -> windows.h + mmsystem.h
# 平台层后端因此只 include "common.h"，一个系统头都不再自己拿。
ALLOW = {'common.h'}
ALLOW_DIRS = ()


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


# ==================================================================
# 规则 S3（2026-11-06）：**每个分区的公开符号只能由该分区自己定义**
# ------------------------------------------------------------------
# 这条是给网页移植补的。当时的做法是把 audio / music / gamepad / steam
# 的桩全塞进 src/platform/plat_web.c —— 平台层变成"什么都懂一点"的
# 杂物间，改一个音频接口要去动平台层。
#
# 现在：桩回到各自分区（audio_web.c / music_web.c / ...），
# 平台层只管平台。这条规则防止它再次退化。
SYMBOL_OWNERS = {
    'audio.h':       (re.compile(r'(^|/)audio(_[a-z0-9]+)?\.c$'),),
    'music.h':       (re.compile(r'(^|/)music(_[a-z0-9]+)?\.c$'),),
    'gamepad.h':     (re.compile(r'(^|/)gamepad(_[a-z0-9]+)?\.c$'),),
    'steam_zone.h':  (re.compile(r'(^|/)steam_zone(_[a-z0-9]+)?\.c$'),),
}


def _declared_funcs(header_path):
    """从分区头里抓出公开函数名。"""
    try:
        txt = io.open(header_path, encoding='utf-8', errors='replace').read()
    except OSError:
        return []
    names = []
    for ln in txt.split('\n'):
        s = ln.strip()
        if s.startswith('*') or s.startswith('/*') or s.startswith('//'):
            continue
        m = re.match(r'^(?:void|int|float|double|long|const\s+char\s*\*)\s+([A-Za-z_]\w*)\s*\(', s)
        if m:
            names.append(m.group(1))
    return names


def _who_defines(func):
    """找出哪些 .c 在文件作用域定义了这个函数。"""
    # 必须带函数体（{）才算"定义"；不带的是原型/调用，不能判成归属
    pat = re.compile(r'^(?:void|int|float|double|long|const\s+char\s*\*)\s+' +
                     re.escape(func) + r'\s*\([^;{]*\)\s*\{', re.M)
    hits = []
    for root, _dirs, files in os.walk(os.path.join(ROOT, 'src')):
        for f in sorted(files):
            if not f.endswith('.c'):
                continue
            path = os.path.join(root, f)
            try:
                txt = io.open(path, encoding='utf-8', errors='replace').read()
            except OSError:
                continue
            if pat.search(txt):
                hits.append(os.path.relpath(path, ROOT).replace('\\', '/'))
    return hits


def check_symbol_ownership():
    bad = []
    for header, owners in SYMBOL_OWNERS.items():
        hp = os.path.join(ROOT, 'include', header)
        if not os.path.isfile(hp):
            continue
        for fn in _declared_funcs(hp):
            hits = _who_defines(fn)
            if not hits:
                continue          # 没定义（可能只有实现分部有），不判死
            for h in hits:
                if not any(o.search(h) for o in owners):
                    bad.append((fn, header, h))
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

    # 规则 S3：分区符号归属
    owner_bad = check_symbol_ownership()

    if not bad and not save_bad and not owner_bad:
        print('partition: OK  (S1 系统头只在 common.h；S2 存档只在 save 后端；S3 分区符号各归各家)')
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

    if owner_bad:
        print('partition: FAIL  —— 有 %d 个符号被别的分区代写了' % len(owner_bad))
        for fn, header, where in owner_bad:
            print('  %-28s 属于 %-14s 却在 %s 里定义' % (fn, header, where))
        print()
        print('为什么不行：分区一旦互相代写，改一个接口要动好几处，')
        print('而且平台层会慢慢变成"什么都懂一点"的杂物间。')
        print('修法：把这个函数挪回它自己的分区文件（例如 audio_web.c）。')

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
