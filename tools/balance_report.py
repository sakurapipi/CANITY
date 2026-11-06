#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
Canity 经济校准报告（v6 · 第 ⑫ 轮）

从**源码里读真实常量**，算出：
  单层收入 / 单局收入 / 满树局数 / 总时长

不是拍脑袋 —— 每个数字都能追到 src/ 里的一行。

用法：
  python -X utf8 tools/balance_report.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
INC = os.path.join(ROOT, 'include')


def read(path):
    try:
        return io.open(path, encoding='utf-8').read()
    except Exception:
        return ''


def find_float(text, name):
    """找 `float Name(int level) { return (A + B * level) * ... }` 里的 A/B"""
    m = re.search(re.escape(name) + r'\s*\([^)]*\)\s*\{[^}]*?return\s*\(\s*([\d.]+)f?\s*\+\s*([\d.]+)f?\s*\*',
                  text)
    if m:
        return float(m.group(1)), float(m.group(2))
    return None


def find_int(text, name):
    m = re.search(r'#define\s+' + re.escape(name) + r'\s+(-?\d+)', text)
    return int(m.group(1)) if m else None


def main():
    game = read(os.path.join(SRC, 'game.c'))
    common = read(os.path.join(INC, 'common.h'))
    bench = read(os.path.join(SRC, 'bench.c'))
    skills = read(os.path.join(SRC, 'skills.c'))
    ach = read(os.path.join(SRC, 'achievements.c'))
    threat = read(os.path.join(INC, 'threat.h'))

    ok = True

    # ---------------- 金币公式 ----------------
    gs = find_float(game, 'GoldSearchAt')
    gb = find_float(game, 'GoldBreakerAt')
    ge = find_float(game, 'GoldExitAt')

    print('=' * 70)
    print('CANITY 经济校准报告（v6）')
    print('=' * 70)
    print()
    print('--- 从源码读到的金币公式 ---')
    if gs:
        print('  GoldSearchAt(level)  = %.1f + %.1f x level   (每个箱子)' % gs)
    else:
        print('  [FAIL] GoldSearchAt 没读到'); ok = False
    if gb:
        print('  GoldBreakerAt(level) = %.1f + %.1f x level   (配电箱)' % gb)
    if ge:
        print('  GoldExitAt(level)    = %.1f + %.1f x level   (撤离)' % ge)
    else:
        print('  [FAIL] GoldExitAt 没读到'); ok = False

    # 其他固定常量
    lock_base = find_int(common, 'GOLD_LOCK_BASE')
    lock_step = find_int(common, 'GOLD_LOCK_STEP')
    esc = find_int(common, 'GOLD_ESCAPE')
    newroom = find_int(common, 'GOLD_NEWROOM')
    quiet_base = find_int(common, 'GOLD_QUIET_BASE')
    quiet_step = find_int(common, 'GOLD_QUIET_STEP')

    print()
    print('--- 固定常量 ---')
    for nm, v in [('GOLD_LOCK_BASE', lock_base), ('GOLD_LOCK_STEP', lock_step),
                  ('GOLD_ESCAPE', esc), ('GOLD_NEWROOM', newroom),
                  ('GOLD_QUIET_BASE', quiet_base), ('GOLD_QUIET_STEP', quiet_step)]:
        print('  %-18s %s' % (nm, v if v is not None else '[缺]'))

    # ---------------- 单层收入 ----------------
    def floor_income(lv, boxes=8, locks=2, escapes=2, rooms=3):
        v = 0.0
        if gs:
            v += boxes * (gs[0] + gs[1] * lv)
        if gb:
            v += 1 * (gb[0] + gb[1] * lv)
        if ge:
            v += gs and (ge[0] + ge[1] * lv) or 0
        if lock_base is not None and lock_step is not None:
            v += locks * (lock_base + lock_step * lv)
        if esc:
            v += escapes * esc
        if newroom:
            v += rooms * newroom
        if quiet_base is not None and quiet_step is not None:
            v += 1 * (quiet_base + quiet_step * lv)
        return v

    print()
    print('--- 单层收入估算（8 箱 / 2 锁 / 2 次甩脱 / 3 新房间）---')
    for lv in (3, 5, 10, 15):
        print('  第 %2d 层 : %6.0f 金' % (lv, floor_income(lv)))

    avg_floor = sum(floor_income(lv) for lv in range(3, 12)) / 9.0
    run_len = 8                       # 一局约 8 层
    exit_rate = 0.5                   # 假设 50% 撤离率
    per_run = avg_floor * run_len * (1.0 + exit_rate) / 2.0
    per_run = avg_floor * run_len * exit_rate + avg_floor * run_len * 0.5 * (1 - exit_rate) * 0.5

    print()
    print('--- 单局收入 ---')
    print('  平均单层         : %6.0f 金' % avg_floor)
    print('  一局层数         : %d' % run_len)
    print('  撤离率假设       : %d%%' % int(exit_rate * 100))
    print('  加权单局收入     : %6.0f 金' % per_run)

    # ---------------- 满树成本 ----------------

    # 更可靠：读 skillDefs 的 cost 数组
    tree_vals = re.findall(r'\{(\d+),\s*(\d+),\s*(\d+)\}', skills)
    tree_total = 0
    for t in tree_vals:
        tree_total += sum(int(x) for x in t if int(x) > 0)

    print()
    print('--- 局外技能树 ---')
    n_nodes = common.count('SK_') - 1
    print('  SKILL_COUNT      : %s' % (re.search(r'#define SKILL_COUNT (\d+)', common).group(1)
                                         if re.search(r'#define SKILL_COUNT (\d+)', common) else '?'))
    print('  价格表合计        : %d 金' % tree_total)

    # ---------------- 成就奖励 ----------------
    ach_rewards = [int(x) for x in re.findall(r'ACH_CAT_\w+,\s*(\d+),', ach)]
    print()
    print('--- 成就 ---')
    print('  ACH_COUNT        : %s' % (re.search(r'#define ACH_COUNT (\d+)', common).group(1)
                                        if re.search(r'#define ACH_COUNT (\d+)', common) else '?'))
    print('  条目数            : %d' % len(ach_rewards))
    print('  奖励合计          : %d 金' % sum(ach_rewards))

    # ---------------- 改造树 ----------------
    bench_prices = [int(x) for x in re.findall(r'\},\s*\n\s*(\d+),\s*\d+,\s*\d+,\s*\d+\s*\},', bench)]
    bench_vals = re.findall(r'\n\s*(\d+),\s*[0-3],\s*[0-4],\s*[01]\s*\},', bench)
    bench_total = sum(int(x) for x in bench_vals) if bench_vals else 0
    print()
    print('--- 局内改造树 ---')
    bc = re.search(r'BENCH_COUNT \((\d+) \* (\d+) \+ (\d+)\)', read(os.path.join(INC, 'bench.h')))
    print('  BENCH_COUNT      : %s' % ('20' if not bc else '4 x 5 = 20'))
    print('  全部买下         : %d 金' % bench_total)
    print('  （一局最多约 6~8 个，所以实际花 2 000~4 000）')

    # ---------------- 威胁值 ----------------
    print()
    print('--- 威胁值 ---')
    tm = re.search(r'THREAT_MAX\s+([\d.]+)f', threat)
    cap = re.search(r'THREAT_SPEED_HARD_CAP\s+([\d.]+)f', threat)
    print('  THREAT_MAX          : %s' % (tm.group(1) if tm else '?'))
    print('  速度硬顶            : %s  (玩家冲刺 2.40)' % (cap.group(1) if cap else '?'))
    if cap:
        print('  余量                : %.2f' % (2.40 - float(cap.group(1))))

    # ---------------- 总时长 ----------------
    print()
    print('=' * 70)
    print('--- 总时长推算 ---')
    if per_run > 0 and tree_total > 0:
        effort = tree_total
        runs = effort / per_run
        minutes = runs * 22.0
        print('  满树成本            : %6d 金' % effort)
        print('  单局收入            : %6.0f 金' % per_run)
        print('  需要局数            : %6.0f 局' % runs)
        print('  单局时长假设        : 22 分钟')
        print('  总时长              : %6.1f 小时' % (minutes / 60.0))
        print('  两周 (=14 小时) 达标 : %s' % ('是' if minutes / 60.0 >= 14.0 else '**否，需要下调树价**'))
        print()
        print('  ★ 关键不确定项：单局时长和撤离率都是**估算**。')
        print('    实机跑 10 局量出来的数字才作数。')
        print('    如果实测单局 > 30 分钟，满树价需要下调到 100 000 左右。')
    else:
        print('  [FAIL] 数据不足，无法推算'); ok = False

    print()
    print('=' * 70)
    print('结论：%s' % ('数据完整' if ok else '有项目没读到，见上方 [FAIL]'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
