#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把一个函数从 game.c 搬到指定块文件 —— 一条命令，自动补声明。

## 用法

    python tools/move_function.py RunRngSetState src/loop_world.c world_api.h

## 它做什么

1. 在 game.c 找到函数（单行或多行）
2. 扫出它用到的、当前头里看不到的符号
3. 这些符号若在 game.c 里是全局定义 → 往目标头补 `extern T X;`
4. 函数搬到块文件（带 common.h + 目标头 + present_api.h）
5. 从 game.c 删除，原型补进目标头
6. 构建；失败 → 全部还原并打印错误

## 为什么要有它

前 5 轮经验：手工搬 1 个函数要 4~8 分钟（查依赖、补声明、构建、排错）。
这个脚本把"查依赖 + 补声明"自动化，只剩"构建验证"这一步。
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'src', 'game.c')
INC = os.path.join(ROOT, 'include')
KEYWORDS = set('''if else for while do return break continue sizeof switch case default
int float double void char unsigned const static inline struct typedef union enum
NULL true false'''.split())


def build():
    env = dict(os.environ)
    env['PATH'] = os.environ.get('CANITY_MINGW', '') + ';' + env.get('PATH', '')
    env['CANITY_GCC'] = "gcc"
    env['CANITY_WINDRES'] = "windres"
    env.pop('CANITY_SHOT', None)
    r = subprocess.run([sys.executable, '-X', 'utf8', 'tools/build_validate.py'],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=env)
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    func, target_rel, hdr = argv[0], argv[1], argv[2]
    target = os.path.join(ROOT, target_rel)
    hdrp = os.path.join(INC, hdr)

    files = [GAME, target, hdrp, os.path.join(INC, 'present_api.h')]
    snap = {p: (io.open(p, encoding='utf-8', errors='replace').read()
                if os.path.isfile(p) else None) for p in files}

    game = snap[GAME]
    lines = game.split('\n')
    start = end = None
    for i, ln in enumerate(lines):
        if not re.match(r'^(?:void|int|float|double|const\s+\w+\s*\*?)\s+\**'
                        + func + r'\s*\(', ln):
            continue
        start = i
        if ln.rstrip().endswith('}'):
            end = i
        else:
            depth, j = 0, i
            while j < len(lines):
                depth += lines[j].count('{') - lines[j].count('}')
                if depth == 0 and j > i:
                    break
                j += 1
            end = j
        break
    if start is None:
        print('  ✗ game.c 里找不到 %s' % func)
        return 1
    body = '\n'.join(lines[start:end + 1])
    print('  找到 %s（%d 行）' % (func, end - start + 1))

    # 依赖：函数体里出现的标识符
    ids = set(re.findall(r'\b([A-Za-z_]\w*)\b', body)) - KEYWORDS - {func}
    header_blob = ''
    for f in os.listdir(INC):
        if f.endswith('.h'):
            header_blob += io.open(os.path.join(INC, f), encoding='utf-8',
                                   errors='replace').read()
    unknown = sorted(x for x in ids if not re.search(r'\b' + re.escape(x) + r'\b', header_blob))
    print('  头里看不到的符号 %d 个：%s' % (len(unknown), ', '.join(unknown[:8])))

    # 补声明（只用 game.c 里的全局定义）
    added = []
    for sym in unknown:
        m = re.search(r'^(?:static\s+)?(float|int|double|unsigned|const\s+char\s*\*)\s+'
                      + re.escape(sym) + r'\b[^;]*;', game, re.M)
        if not m or m.group(0).strip().startswith('static'):
            continue
        ty = m.group(1)
        added.append('extern %s %s;' % (ty, sym))
    if added:
        t = io.open(hdrp, encoding='utf-8', errors='replace').read()
        t = t.replace('\n#endif', '\n/* move_function 自动补的声明 */\n'
                      + '\n'.join(added) + '\n\n#endif', 1)
        io.open(hdrp, 'w', encoding='utf-8', newline='\n').write(t)
        print('  + %s 补 %d 个声明' % (hdr, len(added)))

    # 搬函数
    if os.path.isfile(target):
        t = io.open(target, encoding='utf-8', errors='replace').read().rstrip() + '\n\n' + body + '\n'
    else:
        t = ('/* %s —— 从 game.c 拆出 */\n#include "common.h"\n#include "%s"\n'
             '#include "present_api.h"\n\n' % (target_rel.split("/")[-1], hdr) + body + '\n')
    io.open(target, 'w', encoding='utf-8', newline='\n').write(t)

    io.open(GAME, 'w', encoding='utf-8', newline='\n').write(
        '\n'.join(lines[:start] + lines[end + 1:]))

    # 原型
    proto = body.split('{')[0].strip().rstrip() + ';'
    t = io.open(hdrp, encoding='utf-8', errors='replace').read()
    if func not in t:
        t = t.replace('\n#endif', '\n' + proto + '\n\n#endif', 1)
        io.open(hdrp, 'w', encoding='utf-8', newline='\n').write(t)
    print('  ✓ 已搬 + 补原型')

    code, out = build()
    if code == 0:
        print('  ✓ 构建通过')
        return 0
    for e in [l.strip() for l in out.splitlines() if 'error:' in l][:3]:
        print('  ✗ %s' % e[-110:])
    for p, t0 in snap.items():
        if t0 is None:
            if os.path.isfile(p):
                os.remove(p)
        else:
            io.open(p, 'w', encoding='utf-8', newline='\n').write(t0)
    print('  已还原')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
