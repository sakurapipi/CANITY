#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查器：怪物图鉴的完整性。

## 查什么

1. `BESTIARY_COUNT` 和 `bestiary[]` 的实际行数一致
2. 每一只怪都有**中文名 / 中文描述 / 中文对策**（空字符串 = 图鉴里那格是空白）
3. 每只怪都有非空的反制手段 —— 这是设计红线：
   玩家遇到一只没有对策的怪，只能等死，那是设计事故不是难度

## 为什么

用户原话：「怪物图鉴我们做的特别烂」。烂的根因是**内容登记不全**，
不是渲染问题 —— 所以先把这个量出来。
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run():
    out = []
    common = io.open(os.path.join(ROOT, 'include', 'common.h'),
                     encoding='utf-8', errors='replace').read()
    # 2026-11-09（第 4 轮）：表已搬到 content/ 层，先找那里，回退旧位置
    cand = [os.path.join(ROOT, 'src', 'content', 'content_monsters.c'),
            os.path.join(ROOT, 'src', 'bestiary.c')]
    best = None
    for c in cand:
        if os.path.isfile(c):
            best = io.open(c, encoding='utf-8', errors='replace').read()
            break
    if best is None:
        return [(False, '找不到图鉴表（content_monsters.c / bestiary.c 都不在）')]

    # 2026-11-09：BESTIARY_COUNT 实际在 include/bestiary.h（不在 common.h）
    best_h = io.open(os.path.join(ROOT, 'include', 'bestiary.h'),
                     encoding='utf-8', errors='replace').read()
    m = re.search(r'#define\s+BESTIARY_COUNT\s+(\d+)', best_h + common)
    declared = int(m.group(1)) if m else None

    body_m = re.search(r'const BestiaryEntry bestiary\[[^\]]*\]\s*=\s*\{(.*?)\n\};',
                       best, re.S)
    if not body_m:
        return [(False, 'bestiary.c 里找不到 bestiary[] 表')]
    body = re.sub(r'/\*.*?\*/', '', body_m.group(1), flags=re.S)

    # 每行：{ "EN", "中文", "TAG_EN", "TAG_ZH", flag, ... }
    rows = re.findall(r'\{\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"\s*,\s*"([^"]*)"',
                      body)
    out.append((declared is not None,
                'BESTIARY_COUNT 已定义 = %s' % declared))
    out.append((declared is not None and len(rows) == declared,
                '表行数 %d == BESTIARY_COUNT %s' % (len(rows), declared)))

    # 逐只检查中文名与文本
    if not rows:
        return out + [(False, '一条图鉴行都解析不出来（格式变了？）')]

    no_zh = [en for en, zh, _, _ in rows if not zh.strip()]
    out.append((not no_zh,
                '每只怪都有中文名%s' % ('' if not no_zh else '（缺: %s）' % ', '.join(no_zh))))

    no_tag = [en for _, _, _, tz in rows if not tz.strip()]
    out.append((not no_tag,
                '每只怪都有中文标签%s' % ('' if not no_tag else '（缺: %s）' % ', '.join(no_tag))))

    # 对策字段：每行末尾两个字符串（对策 EN / 对策 ZH）
    blocks = re.findall(r'\{(.*?)\}', body, re.S)
    missing_counter = []
    for b in blocks:
        strs = re.findall(r'"([^"]*)"', b)
        if len(strs) < 7:
            continue
        counter_zh = strs[-1]
        name = strs[0]
        if not counter_zh.strip():
            missing_counter.append(name)
    out.append((not missing_counter,
                '每只怪都有反制手段（设计红线）%s'
                % ('' if not missing_counter
                   else '（缺: %s）' % ', '.join(missing_counter))))

    return out
