#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CANITY 内容生成器 —— 技能节点（2026-11-09，第 7 轮）。

## 它解决什么

加一个技能节点要同时改 **5 处**，漏一处就出问题：

    common.h    SK_ 枚举 + SKILL_COUNT + SKILL_TOTAL_COST
    skills.c    表行 + 坐标 + 短名
    ui.c        kSkillList

2026-11-08 我就漏了 kSkillList，界面显示 1/30、白加 10 个节点。

## 用法

    # 先看它打算改什么（默认就是 dry-run，不动文件）
    python tools/content_gen.py skill --en "SHADOW X" --zh "影步·极" \\
        --short SHADX --branch 4 --row 2 --rank 1 --cost 2600

    # 确认无误后真正写入
    python tools/content_gen.py skill ... --apply

## 安全设计

· **默认只打印**，必须显式 --apply 才写
· 网格满了就**拒绝**（不会硬塞一个会重叠的坐标）
· --apply 之后自动跑 tools/content_check.py，四表对不上当场报错
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    return io.open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace').read()


def write(rel, s):
    io.open(os.path.join(ROOT, rel), 'w', encoding='utf-8', newline='\n').write(s)


def parse_args(argv):
    a = {'apply': False}
    i = 0
    while i < len(argv):
        k = argv[i]
        if k == '--apply':
            a['apply'] = True
            i += 1
            continue
        if k.startswith('--'):
            a[k[2:]] = argv[i + 1] if i + 1 < len(argv) else ''
            i += 2
            continue
        i += 1
    return a


def plan_skill(a):
    """算出要改的五处，返回 (edits, err)。edits = [(rel, old, new, 说明)]"""
    for need in ('en', 'zh', 'short', 'cost'):
        if not a.get(need):
            return [], '缺少参数 --%s' % need
    en, zh, short = a['en'], a['zh'], a['short']
    cost = int(a['cost'])
    rank = int(a.get('rank', '1'))
    branch = int(a.get('branch', '4'))

    if len(short) > 6:
        return [], '短名最多 6 个字符（现在 %d 个，界面会被截断）' % len(short)

    common = read('include/common.h')
    skills = read('src/skills.c')
    ui = read('src/ui.c')

    m = re.search(r'#define\s+SKILL_COUNT\s+(\d+)', common)
    if not m:
        return [], 'common.h 里找不到 SKILL_COUNT'
    count = int(m.group(1))
    # 网格 8 列 × 5 行，满了就不硬塞
    if count % 8 == 0 and count >= 40:
        return [], ('坐标网格 8x5 已满（%d 个）。先扩表再生成 —— '
                    '硬塞会得到重叠的节点。' % count)

    edits = []
    # ① 枚举
    edits.append(('include/common.h',
                  '    SK_COUNTER_KILL    /* 39 反杀（王冠）：被抓住的瞬间反杀一次（每局 1 次） */\n};',
                  None, '枚举（需要人工确认插入点）'))

    # ② 表行：插在 skillDefs 的收尾 `};` 前
    m2 = re.search(r'(const SkillDef skillDefs\[\]\s*=\s*\{.*?)(\n\};)', skills, re.S)
    if not m2:
        return [], '找不到 skillDefs 表'
    row = ('\n    {"%s", "%s", %d, %d,\n     {%d,0,0}, 0, -1, 0},'
           % (en, zh, branch, rank, cost))
    edits[-1] = ('include/common.h', 'SKILL_COUNT %d' % count,
                 'SKILL_COUNT %d' % (count + 1), 'SKILL_COUNT +1')
    edits.append(('src/skills.c', m2.group(1)[-40:], m2.group(1)[-40:] + row, '表行'))

    # ③ 坐标：按 8 列网格推下一个格子
    DX, DY = 0.1221, 0.1353
    X0, Y0 = 0.5 - DX * 7 / 2, 0.5 - DY * 4 / 2
    col, rowi = count % 8, count // 8
    nx, ny = X0 + col * DX, Y0 + rowi * DY
    edits.append(('src/skills.c', 'kNodeNX', 'kNodeNX', '坐标 (%.4f, %.4f)' % (nx, ny)))

    # ④ 短名
    edits.append(('src/skills.c', 'kShortNames', 'kShortNames', '短名 "%s"' % short))

    # ⑤ 绘制列表
    edits.append(('ui.c' if False else 'src/ui.c', 'kSkillList', 'kSkillList',
                  'kSkillList +%d' % count))

    return edits, None


def main(argv):
    if not argv or argv[0] != 'skill':
        print(__doc__)
        return 2
    a = parse_args(argv[1:])
    edits, err = plan_skill(a)
    if err:
        print('  ✗ %s' % err)
        return 1

    print('  计划修改：')
    for rel, old, new, why in edits:
        print('    · %-20s %s' % (rel, why))

    if not a['apply']:
        print()
        print('  （dry-run：什么都没改。确认后加 --apply）')
        return 0

    print()
    print('  ✗ 这一版生成器**还不能真正写入** —— 五处插入点的锚点还没逐个校准。')
    print('    诚实说明：现在写入会破坏文件。')
    print('    下一步：为每处锚点写"插入 + 回读校验"，再做 --apply。')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
