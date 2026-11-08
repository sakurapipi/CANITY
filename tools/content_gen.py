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
import shutil
import subprocess
import sys
import tempfile

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
    if '--self-test' in argv or a.get('self-test'):
        return self_test()
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


# ======================================================================
# 2026-11-09（第 8 轮）：--self-test —— 在临时副本上真跑一遍插入
# ======================================================================

def _array_span(src, name):
    """找到 `name ... = {` 的花括号配对区间，返回 (开括号位置, 闭括号位置)。"""
    m = re.search(re.escape(name) + r'[^=]*=\s*\{', src)
    if not m:
        return None
    open_i = src.index('{', m.start())
    depth = 0
    i = open_i
    while i < len(src):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                return open_i, i
        i += 1
    return None


def _append_to_array(src, name, text):
    """在数组的闭合 } 前追加 text（自动补逗号）。"""
    span = _array_span(src, name)
    if not span:
        raise RuntimeError('找不到数组 %s' % name)
    _open, close = span
    head = src[:_open]
    body = src[_open:close]
    tail = src[close:]
    if not body.rstrip().endswith(','):
        body = body.rstrip() + ','
    body = body + '\n    ' + text + '\n'
    return head + body + tail


def _apply_on(root, en, zh, short, branch, rank, cost):
    """在 root 指向的目录上执行全部插入。返回 (ok, 说明)。"""
    def rd(rel):
        return io.open(os.path.join(root, rel), encoding='utf-8', errors='replace').read()

    def wr(rel, s):
        io.open(os.path.join(root, rel), 'w', encoding='utf-8', newline='\n').write(s)

    common = rd('include/common.h')
    skills = rd('src/skills.c')
    ui = rd('src/ui.c')

    m = re.search(r'#define\s+SKILL_COUNT\s+(\d+)', common)
    if not m:
        return False, '找不到 SKILL_COUNT'
    count = int(m.group(1))

    # ① SKILL_COUNT +1
    common = common.replace(m.group(0), '#define SKILL_COUNT %d' % (count + 1), 1)

    # ② 枚举加一项（在 SK 枚举的闭合 } 前）
    span = None
    mm = re.search(r'enum\s*\{[^}]*SK_QUIET[^}]*\}', common, re.S)
    if not mm:
        return False, '找不到 SK_ 枚举'
    enum_txt = mm.group(0)
    new_enum = enum_txt[:-1].rstrip()
    if not new_enum.endswith(','):
        new_enum += ','
    new_enum += '\n    SK_GEN_%s   /* 生成器测试项 */\n}' % re.sub(r'\W', '_', en)
    common = common.replace(enum_txt, new_enum, 1)

    # ③ 表行
    skills = _append_to_array(
        skills, 'skillDefs',
        '{"%s", "%s", %d, %d, {%d,0,0}, 0, -1, 0},' % (en, zh, branch, rank, cost))

    # ④ 坐标（下一格）
    DX, DY = 0.1221, 0.1353
    X0, Y0 = 0.5 - DX * 7 / 2, 0.5 - DY * 4 / 2
    col, rowi = count % 8, count // 8
    skills = _append_to_array(skills, 'kNodeNX', '%.4ff,' % (X0 + col * DX))
    skills = _append_to_array(skills, 'kNodeNY', '%.4ff,' % (Y0 + rowi * DY))

    # ⑤ 短名
    skills = _append_to_array(skills, 'kShortNames', '"%s",' % short)

    # ⑥ 绘制列表
    ui = _append_to_array(ui, 'kSkillList', '%d,' % count)

    wr('include/common.h', common)
    wr('src/skills.c', skills)
    wr('src/ui.c', ui)
    return True, '插入完成'


def _verify(root):
    """用正则复查四表是否对齐。"""
    def rd(rel):
        return io.open(os.path.join(root, rel), encoding='utf-8', errors='replace').read()

    common, skills, ui = rd('include/common.h'), rd('src/skills.c'), rd('src/ui.c')
    count = int(re.search(r'#define\s+SKILL_COUNT\s+(\d+)', common).group(1))

    def n_items(src, name):
        span = _array_span(src, name)
        body = src[span[0]:span[1]]
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        return len([x for x in re.split(r'[,\s]+', body[1:]) if x.strip()])

    return [
        ('SKILL_COUNT = %d' % count, count == 41),
        ('skillDefs 行数 == SKILL_COUNT',
         len(re.findall(r'\{\s*"', _array_body(skills, 'skillDefs'))) == count),
        ('kNodeNX 项数 == SKILL_COUNT', n_items(skills, 'kNodeNX') == count),
        ('kNodeNY 项数 == SKILL_COUNT', n_items(skills, 'kNodeNY') == count),
        ('kShortNames 项数 == SKILL_COUNT',
         len(re.findall(r'"[^"]*"', _array_body(skills, 'kShortNames'))) == count),
        ('kSkillList 项数 == SKILL_COUNT', n_items(ui, 'kSkillList') == count),
    ]


def _array_body(src, name):
    span = _array_span(src, name)
    return src[span[0]:span[1]] if span else ''


def self_test():
    tmp = tempfile.mkdtemp(prefix='canity_gen_')
    try:
        for rel in ('include', 'src'):
            shutil.copytree(os.path.join(ROOT, rel), os.path.join(tmp, rel))
        ok, why = _apply_on(tmp, 'GEN TEST', '生成器测试', 'GENTST', 4, 1, 2600)
        if not ok:
            print('  ✗ %s' % why)
            return 1
        print('  ✓ 副本上插入完成')
        bad = 0
        for label, good in _verify(tmp):
            print('   %s %s' % ('✓' if good else '✗', label))
            if not good:
                bad += 1
        print()
        print('  %s 四表对齐' % ('✓' if bad == 0 else '✗ 有 %d 处不对齐' % bad))
        return 0 if bad == 0 else 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))


