#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查器：技能系统的「四张表」必须互相对齐。

## 查什么

一个技能节点在代码里要登记 4 处：

    skillDefs[]   名字 / 描述 / 等级 / 价格 / 前置
    kShortNames[] 节点里的短名
    kNodeNX/NY[]  技能树坐标
    kSkillList[]  界面绘制列表

再加两个总量：

    SKILL_COUNT        总节点数
    SKILL_TOTAL_COST   全部买满的总价

**任何一处对不上，都会出「买了但看不见」「总价算错」这类问题。**
2026-11-08 就发生过：加 10 个节点但忘了 kSkillList，界面显示 1/30。
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _read(rel):
    return io.open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace').read()


def _arr(src, name):
    """取 `name[...] = { ... };` 的花括号内容（去注释）。"""
    m = re.search(name + r'\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;', src, re.S)
    if not m:
        return None
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = re.sub(r'//[^\n]*', '', body)
    return body


def _count_ints(body):
    return len([x for x in re.split(r'[,\s]+', body.strip()) if x])


def run():
    out = []
    common = _read('include/common.h')
    skills = _read('src/skills.c')
    ui = _read('src/ui.c')

    m = re.search(r'#define\s+SKILL_COUNT\s+(\d+)', common)
    if not m:
        return [(False, 'common.h 里找不到 SKILL_COUNT')]
    count = int(m.group(1))

    # ---- skillDefs 行数 ----
    body = _arr(skills, r'const SkillDef skillDefs')
    rows = len(re.findall(r'\{\s*"', body)) if body else 0
    out.append((rows == count,
                'skillDefs 行数 %d == SKILL_COUNT %d' % (rows, count)))

    # ---- 短名 ----
    sn = _arr(skills, r'kShortNames')
    sn_n = len(re.findall(r'"([^"]*)"', sn)) if sn else 0
    out.append((sn_n == count, 'kShortNames 项数 %d == %d' % (sn_n, count)))
    if sn:
        long_names = [x for x in re.findall(r'"([^"]*)"', sn) if len(x) > 6]
        out.append((not long_names,
                    '短名都 <= 6 字符%s' % ('' if not long_names
                                        else '（超长: %s）' % ', '.join(long_names[:3]))))

    # ---- 坐标 ----
    for nm in ('kNodeNX', 'kNodeNY'):
        b = _arr(skills, nm)
        n = _count_ints(b) if b else 0
        out.append((n == count, '%s 项数 %d == %d' % (nm, n, count)))

    # ---- 界面绘制列表 ----
    b = _arr(ui, r'kSkillList')
    n = _count_ints(b) if b else 0
    out.append((n == count, 'kSkillList 项数 %d == %d（少了就是"加了节点界面不画"）'
                % (n, count)))

    # ---- 总价 ----
    mt = re.search(r'#define\s+SKILL_TOTAL_COST\s+(\d+)', common)
    if mt and body:
        declared = int(mt.group(1))
        pat = re.compile(r'\{\s*"[^"]+"[^{]*?,\s*(-?\d+)\s*,\s*'
                         r'\{(-?\d+),(-?\d+),(-?\d+)\}', re.S)
        total = 0
        rows_parsed = 0
        for mm in pat.finditer(body):
            rank = int(mm.group(1))
            costs = [int(mm.group(2)), int(mm.group(3)), int(mm.group(4))]
            rows_parsed += 1
            total += sum(costs[:max(0, min(3, rank))])
        out.append((rows_parsed == count,
                    '能解析出价格的节点 %d/%d' % (rows_parsed, count)))
        out.append((total == declared,
                    'SKILL_TOTAL_COST %d == 实测总价 %d' % (declared, total)))

    return out
