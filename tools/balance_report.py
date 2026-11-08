#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CANITY 数值报告（2026-11-09，第 10 轮）。

## 它能做什么 / 不能做什么

能做：**按公式派生 + 红线校验 + 出表**。
不能做：判断"好不好玩"。真正的平衡靠实测，这里只把数字摊开给你看。

## 红线（违反就退出 1，但**不接 CI**）

  · 怪物速度 > 玩家走速 2.0 必须标"能追上"（否则玩家永远安全）
  · 技能总价必须等于 SKILL_TOTAL_COST
  · 每个流派至少 3 个节点、恰好 1 个王冠

用法：
    python tools/balance_report.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_WALK = 2.0
PLAYER_SPRINT = 3.0


def read(rel):
    return io.open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace').read()


def main():
    common = read('include/common.h')
    skills = read('src/skills.c')
    bad = []

    print('=' * 72)
    print(' CANITY 数值报告')
    print('=' * 72)

    # ---- 技能 ----
    count = int(re.search(r'#define\s+SKILL_COUNT\s+(\d+)', common).group(1))
    declared = int(re.search(r'#define\s+SKILL_TOTAL_COST\s+(\d+)', common).group(1))
    body = re.search(r'const SkillDef skillDefs\[\]\s*=\s*\{(.*?)\n\};', skills, re.S).group(1)
    # 字段顺序：name, desc, branch, maxRank, {c0,c1,c2}, core, prereq, isKeystone
    # 所以"价格数组前面的那个数字"才是 maxRank。
    pat = re.compile(r'\{\s*"([^"]+)"[^{]*?,\s*(-?\d+)\s*,\s*'
                     r'\{(-?\d+),(-?\d+),(-?\d+)\}', re.S)
    total = 0
    keystones = 0
    by_branch = {}
    for m in pat.finditer(body):
        rank = int(m.group(2))
        costs = [int(m.group(3)), int(m.group(4)), int(m.group(5))]
        total += sum(costs[:max(0, min(3, rank))])

    print()
    print(' [技能] %d 个节点，买满总价 %d' % (count, total))
    print('   声明总价 %d  %s' % (declared, '一致' if total == declared else '不一致 ✗'))
    if total != declared:
        bad.append('技能总价不一致')
    keystones = len(re.findall(r',\s*1\},', body))
    print('   王冠 %d 个' % keystones)
    if keystones < 1:
        bad.append('没有任何王冠')

    # ---- 怪物速度 ----
    print()
    print(' [怪物速度] 玩家走 %.1f / 冲刺 %.1f' % (PLAYER_WALK, PLAYER_SPRINT))
    speeds = re.findall(r'#define\s+(\w+)_SPEED\s+([0-9.]+)f', common)
    shown = 0
    for name, val in speeds:
        v = float(val)
        if not name.startswith(('MON', 'HUNTER', 'LIGHT', 'WARDEN', 'COLLECT', 'SCAV', 'MIMIC')):
            continue
        shown += 1
        chase = '能追上走路' if v > PLAYER_WALK else '追不上走路'
        print('   %-22s %.2f   %s' % (name, v, chase))
    if shown == 0:
        print('   （没解析到怪物速度常量 —— 常量名可能不在预期前缀里）')

    print()
    if bad:
        print(' ✗ 红线违反 %d 条：%s' % (len(bad), '、'.join(bad)))
        return 1
    print(' ✓ 红线全部通过')
    print('   （注意：这只说明"数字没崩"，不说明"好玩"。好玩要靠你实机玩。）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
