#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CANITY 边界报告（2026-11-09，第 3 轮）。

## 它回答一个问题

**「把游戏主体拆成五块，到底有多少处会断？」**

拆之前必须先量：现在每个 .c 依赖了谁、依赖有多重。
这份报告是后面 13 轮的依据 —— 数字决定策略。

## 两个指标

1. **include 边**：A.c 引用了 B.h → 一条依赖边
2. **函数体内 extern**：绕过头文件直接拿别人的全局（真正的强耦合）

用法：
    python tools/boundary_report.py                # 打到屏幕
    python tools/boundary_report.py --md docs/x.md # 同时写文件
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')


def src_files():
    out = []
    for root, _dirs, fs in os.walk(SRC):
        for f in sorted(fs):
            if f.endswith('.c'):
                out.append(os.path.join(root, f))
    return out


def main(argv):
    files = src_files()
    inc_edges = {}      # 文件 -> set(它 include 的自家头)
    extern_counts = {}  # 文件 -> 函数体内 extern 次数

    for p in files:
        rel = os.path.relpath(p, ROOT).replace('\\', '/')
        t = io.open(p, encoding='utf-8', errors='replace').read()
        # 自家头（"xxx.h"）
        heads = set(re.findall(r'#\s*include\s*"([^"]+\.h)"', t))
        heads.discard('common.h')
        inc_edges[rel] = heads
        # 函数体内 extern：出现在行首缩进里的 extern 声明
        extern_counts[rel] = len(re.findall(r'^\s{4,}extern\s+', t, re.M))

    total_edges = sum(len(v) for v in inc_edges.values())
    total_extern = sum(extern_counts.values())

    print('=' * 72)
    print(' CANITY 边界报告')
    print('=' * 72)
    print()
    print(' 文件数 %d' % len(files))
    print(' include 边 %d 条（平均每个 .c 依赖 %.1f 个自家头）'
          % (total_edges, total_edges / max(1, len(files))))
    print(' 函数体内 extern %d 处' % total_extern)
    print()

    print(' [依赖最重的 10 个文件]')
    print('   %-32s %-6s %s' % ('文件', 'include', 'extern'))
    ranked = sorted(inc_edges.keys(),
                    key=lambda k: (-len(inc_edges[k]), -extern_counts.get(k, 0)))
    for rel in ranked[:10]:
        print('   %-32s %-6d %d' % (rel, len(inc_edges[rel]), extern_counts.get(rel, 0)))
    print()

    print(' [被依赖最多的头 10 个]')
    hdr_count = {}
    for rel, hs in inc_edges.items():
        for h in hs:
            hdr_count[h] = hdr_count.get(h, 0) + 1
    for h, n in sorted(hdr_count.items(), key=lambda kv: -kv[1])[:10]:
        print('   %-28s 被 %d 个 .c 引用' % (h, n))
    print()

    # 五块归属（按文件名粗分，给拆解做预算）
    blocks = {
        'P1 循环/时间': ('game.c', 'core/tick.c'),
        'P2 世界': ('map_rooms.c', 'hatch_place.c', 'collapse.c', 'room_template.c'),
        'P3 实体/AI': ('entity.c', 'warden.c', 'collector.c', 'scavenger.c',
                       'spider.c', 'mimic.c', 'flank.c', 'ai_common.c'),
        'P4 玩家': ('throwing.c', 'money.c', 'lockbox.c'),
        'P5 呈现': ('render.c', 'ui.c'),
    }
    print(' [五块归属粗估（按文件名）]')
    for name, pats in blocks.items():
        hit = [rel for rel in inc_edges
               if any(rel.endswith(p) or ('/' + p) in rel for p in pats)]
        ext = sum(extern_counts.get(r, 0) for r in hit)
        print('   %-14s %2d 个文件，extern %3d 处' % (name, len(hit), ext))
    print()
    print(' 说明：game.c 会分摊到五块里，它单文件 %d 处 extern 是最大的成本。'
          % extern_counts.get('src/game.c', 0))

    if '--md' in argv:
        i = argv.index('--md')
        if i + 1 < len(argv):
            out = os.path.join(ROOT, argv[i + 1])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with io.open(out, 'w', encoding='utf-8', newline='\n') as f:
                f.write('# 边界报告（第 3 轮）\n\n')
                f.write('- 文件 %d 个\n- include 边 %d 条\n- 函数体内 extern %d 处\n\n'
                        % (len(files), total_edges, total_extern))
                f.write('| 文件 | include | extern |\n|---|---|---|\n')
                for rel in ranked[:20]:
                    f.write('| `%s` | %d | %d |\n'
                            % (rel, len(inc_edges[rel]), extern_counts.get(rel, 0)))
            print()
            print(' 已写入 %s' % argv[i + 1])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
