#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
读 canity_runs.csv，算出**真实**的经济数字，和设计目标对比。

用法：
  python -X utf8 tools/economy_analyze.py [csv路径]

没给路径就找 exe 旁边的 canity_runs.csv。
"""
import io
import os
import sys

TARGET_HOURS = 14.0     # 「至少两周」= 14 小时
TREE_COST = None        # 从 balance_report 那边抄，或读源码


def find_csv():
    for p in ('canity_runs.csv',
              os.path.join('..', '..', 'game', 'canity_runs.csv'),
              os.path.join(os.environ.get('USERPROFILE', ''), 'Desktop',
                           'game', 'canity_runs.csv')):
        if os.path.exists(p):
            return p
    return None


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else find_csv()
    if not path or not os.path.exists(path):
        print('找不到 canity_runs.csv。')
        print('先玩几局（每局结束会自动写一行），再跑这个脚本。')
        return 1

    rows = []
    with io.open(path, encoding='utf-8') as f:
        lines = [l.strip() for l in f if l.strip()]
    if len(lines) < 2:
        print('CSV 里还没有数据（只有表头）。先玩几局。')
        return 1

    for l in lines[1:]:
        parts = l.split(',')
        if len(parts) < 6:
            continue
        try:
            rows.append({
                'level': int(parts[0]),
                'banked': int(parts[1]),
                'carried': int(parts[2]),
                'survived': int(parts[3]),
                'seconds': float(parts[4]),
                'caught': int(parts[5]),
            })
        except ValueError:
            continue

    n = len(rows)
    print('=' * 62)
    print('CANITY 实测经济（%d 局）' % n)
    print('=' * 62)

    total_sec = sum(r['seconds'] for r in rows)
    total_gold = sum(r['banked'] for r in rows)
    surv = sum(1 for r in rows if r['survived'])
    caught = sum(r['caught'] for r in rows)

    avg_min = total_sec / n / 60.0
    avg_gold = total_gold / n
    rate = surv * 100.0 / n

    print()
    print('  平均单局时长   : %5.1f 分钟' % avg_min)
    print('  平均单局收入   : %5.0f 金' % avg_gold)
    print('  撤离率         : %5.1f %%' % rate)
    print('  平均被抓次数   : %5.2f' % (caught / float(n)))
    print('  平均结束层数   : %5.1f' % (sum(r['level'] for r in rows) / float(n)))

    if avg_gold <= 0:
        print()
        print('  单局收入 <= 0，没法推算总时长（是不是每局都死在第 1 层？）')
        return 0

    # 和设计值对比
    DESIGN_GOLD = 2625
    DESIGN_MIN = 22.0
    print()
    print('--- 和设计值对比 ---')
    print('  单局收入  设计 %5d  实测 %5.0f  （%+.0f%%）'
          % (DESIGN_GOLD, avg_gold, (avg_gold / DESIGN_GOLD - 1) * 100))
    print('  单局时长  设计 %5.1f  实测 %5.1f  （%+.0f%%）'
          % (DESIGN_MIN, avg_min, (avg_min / DESIGN_MIN - 1) * 100))

    # 用实测值重算总时长
    tree = TREE_COST
    if tree is None:
        # 从源码读
        try:
            root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            sk = io.open(os.path.join(root, 'src', 'skills.c'), encoding='utf-8').read()
            import re
            vals = re.findall(r'\{(\d+),\s*(\d+),\s*(\d+)\}', sk)
            tree = sum(int(x) for t in vals for x in t)
        except Exception:
            tree = 128500

    runs = tree / avg_gold
    hours = runs * avg_min / 60.0
    print()
    print('--- 用实测值重算 ---')
    print('  满树成本       : %6d 金' % tree)
    print('  需要局数       : %6.0f 局' % runs)
    print('  总时长         : %6.1f 小时' % hours)
    print('  两周 (%.0f h)   : %s' % (TARGET_HOURS,
                                      '达标' if hours >= TARGET_HOURS else '**不够**'))
    if hours > TARGET_HOURS * 1.6:
        print('  ⚠ 超过目标 %.1f 倍 —— 树价建议下调到 %d 左右'
              % (hours / TARGET_HOURS, int(tree * TARGET_HOURS * 1.2 / hours)))
    elif hours < TARGET_HOURS:
        print('  ⚠ 不到目标 —— 树价建议上调到 %d 左右'
              % int(tree * TARGET_HOURS * 1.1 / hours))
    else:
        print('  ✓ 落在目标区间')
    print()
    return 0


if __name__ == '__main__':
    sys.exit(main())
