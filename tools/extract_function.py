#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把一个函数从 game.c 搬到指定模块文件 —— **错误驱动**地自动补接缝。

## 配方（由第 21~22 轮实测得出）

搬一个函数出 game.c，需要先补三种接缝：

    ① 状态接缝   依赖的 static 变量 → 去掉 static + 契约头补 extern
    ② 类型接缝   依赖的 enum / #define → 整块搬到契约头
    ③ 函数接缝   调用的 game.c 私有函数 → 补原型（static 的先去 static）

本工具用**真实的编译错误**驱动这三步：构建 → 解析错误 → 补 → 再构建，
最多 8 轮；任何一轮无法解析就全部回滚。

## 用法

    python tools/extract_function.py 函数名 src/loop_xxx.c 契约头.h
    python tools/extract_function.py --dry 函数名 ...      # 只看它会怎么做
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'src', 'game.c')
INC = os.path.join(ROOT, 'include')


def read(p):
    return io.open(p, encoding='utf-8', errors='replace').read()


def write(p, t):
    io.open(p, 'w', encoding='utf-8', newline='\n').write(t)


def build():
    r = subprocess.run([sys.executable, '-X', 'utf8', 'tools/build_validate.py'],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=dict(os.environ))
    return r.returncode, (r.stdout or '') + (r.stderr or '')


def find_func(lines, name):
    for i, ln in enumerate(lines):
        if not re.match(r'^(?:static\s+)?(?:void|int|float|double|unsigned)\s+\**'
                        + re.escape(name) + r'\s*\([^;]*\)\s*\{', ln):
            continue
        if ln.rstrip().endswith('}'):
            return i, i
        depth, j = 0, i
        while j < len(lines):
            depth += lines[j].count('{') - lines[j].count('}')
            if depth == 0 and j > i:
                break
            j += 1
        return i, j
    return None


def main(argv):
    dry = '--dry' in argv
    argv = [a for a in argv if a != '--dry']
    if len(argv) < 3:
        print(__doc__)
        return 2
    name, target_rel, hdr_name = argv[0], argv[1], argv[2]
    target = os.path.join(ROOT, target_rel)
    hdrp = os.path.join(INC, hdr_name)

    touched = [GAME, target, hdrp]
    snap = {p: (read(p) if os.path.isfile(p) else None) for p in touched}
    game = snap[GAME]
    lines = game.split('\n')
    span = find_func(lines, name)
    if not span:
        print('  ✗ game.c 里找不到 %s' % name)
        return 1
    a, b = span
    body = '\n'.join(lines[a:b + 1])
    print('  %s：%d 行（第 %d 行）' % (name, b - a + 1, a + 1))

    # 先做函数搬迁（接缝留给错误驱动补）
    if os.path.isfile(target):
        t = read(target)
        # 2026-11-09：目标文件已存在时也要补 include（posX/posY 在 state.h）
        for inc in ('state.h', 'entity.h'):
            if ('#include "%s"' % inc) not in t:
                t = t.replace('#include "common.h"',
                              '#include "common.h"\n#include "%s"' % inc, 1)
        t = t.rstrip() + '\n\n' + body + '\n'
    else:
        t = ('/* %s —— 从 game.c 拆出（extract_function.py） */\n'
             '#include "common.h"\n#include "%s"\n#include "present_api.h"\n'
             '#include "player_api.h"\n#include "world_api.h"\n'
             '#include "state.h"\n#include "entity.h"\n\n'
             % (os.path.basename(target_rel), hdr_name) + body + '\n')
    write(target, t)
    write(GAME, '\n'.join(lines[:a] + lines[b + 1:]))
    h = read(hdrp)
    proto = body.split('{')[0].strip().rstrip() + ';'
    if name not in h:
        h = h.replace('\n#endif', '\n' + proto + '\n\n#endif', 1)
        write(hdrp, h)
    print('  已搬入 %s，原型进 %s' % (target_rel, hdr_name))

    if dry:
        print('  （--dry：不构建）')
        for p, t0 in snap.items():
            if t0 is None:
                if os.path.isfile(p):
                    os.remove(p)
            else:
                write(p, t0)
        return 0

    for attempt in range(8):
        code, out = build()
        if code == 0:
            print('  ✓ 构建通过（第 %d 次，接缝自动补全）' % (attempt + 1))
            return 0
        errs = [l.strip() for l in out.splitlines() if 'error:' in l]
        g = read(GAME)
        h = read(hdrp)
        tgt = read(target)
        fixed = False

        # ① 未声明的标识符
        for sym in set(re.findall(r"'(\w+)' undeclared", out)):
            # 情形 A：game.c 里是 static 全局 → 去 static + 补 extern
            m = re.search(r'^static\s+(float|int|unsigned|double|char)\s+'
                          + sym + r'\b', g, re.M)
            if m:
                g = re.sub(r'^static\s+(' + m.group(1) + r')\s+' + sym + r'\b',
                           r'\1 ' + sym, g, count=1, flags=re.M)
                write(GAME, g)
                h = h.replace('\n#endif', '\nextern %s %s;\n\n#endif'
                              % (m.group(1), sym), 1)
                write(hdrp, h)
                print('   + ① %s: static → extern(%s)' % (sym, m.group(1)))
                fixed = True
                continue
            # 情形 A2：game.c 里的非 static 全局 → 头里补 extern
            m2 = re.search(r'^(float|int|unsigned|double|char)\s+' + sym + r'\b\s*[=;]',
                           g, re.M)
            if m2:
                h = read(hdrp)
                if not re.search(r'extern\s+[^;]*\b' + sym + r'\b', h):
                    h = h.replace('\n#endif', '\nextern %s %s;\n\n#endif'
                                  % (m2.group(1), sym), 1)
                    write(hdrp, h)
                print('   + ① %s: 全局 → extern(%s)' % (sym, m2.group(1)))
                fixed = True
                continue

            # 情形 B：枚举成员 / 宏 → 找所在块搬到头
            mm = re.search(r'(enum\s*\{[^}]*\b' + sym + r'\b[^}]*\}\s*\w*\s*;)', g, re.S)
            if mm:
                blk = mm.group(1)
                g = g.replace(blk, '/* 搬到 %s */' % hdr_name, 1)
                write(GAME, g)
                h = h.replace('\n#endif', '\n' + blk + '\n\n#endif', 1)
                write(hdrp, h)
                print('   + ② %s: 枚举块搬到 %s' % (sym, hdr_name))
                fixed = True
                continue
            md = re.search(r'^#define\s+' + sym + r'\b[^\n]*$', g, re.M)
            if md:
                g = g.replace(md.group(0), '/* 搬到 %s */' % hdr_name, 1)
                write(GAME, g)
                h = h.replace('\n#endif', '\n' + md.group(0) + '\n\n#endif', 1)
                write(hdrp, h)
                print('   + ② %s: 宏搬到 %s' % (sym, hdr_name))
                fixed = True
                continue

        # ③ 隐式声明的函数
        for sym in set(re.findall(r"implicit declaration of function '(\w+)'", out)):
            m = re.search(r'^(static\s+)?((?:void|int|float|double|unsigned)\s+\**'
                          + sym + r'\s*\([^;{]*\))', g, re.M)
            if not m:
                continue
            if m.group(1):
                g = g.replace(m.group(0), m.group(2), 1)
                write(GAME, g)
            proto = m.group(2).strip() + ';'
            h = read(hdrp)
            if not re.search(r'\b' + sym + r'\s*\(', h):
                h = h.replace('\n#endif', '\n' + proto + '\n\n#endif', 1)
                write(hdrp, h)
            print('   + ③ 函数 %s 补原型' % sym)
            fixed = True

        # ④ 缺头文件
        for inc in set(re.findall(r'fatal error: ([^:]+): No such file', out)):
            inc = inc.strip()
            if ('#include "%s"' % inc) in tgt:
                continue
            tgt = tgt.replace('#include "common.h"',
                              '#include "common.h"\n#include "%s"' % inc, 1)
            write(target, tgt)
            print('   + ④ 加 #include "%s"' % inc)
            fixed = True

        if not fixed:
            print('  ✗ 无法自动补全，错误：')
            for e in errs[:4]:
                print('     %s' % e[-104:])
            break

    for p, t0 in snap.items():
        if t0 is None:
            if os.path.isfile(p):
                os.remove(p)
        else:
            write(p, t0)
    print('  已全部回滚')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
