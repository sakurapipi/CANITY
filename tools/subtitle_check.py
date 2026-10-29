#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""字幕/文字带重叠检测（v2.0，替代手工维护坐标表的 hud_check.py）

为什么重做
----------
旧 hud_check.py 是一张**手写坐标表**，代码一改就对不上：
2026-10-30 实测它还在检查已经删掉的 WEAPON / ENERGY 两行，
却对新增的"藏身提示 / 敌人三态 / 噪音环"完全不知道。

这个版本改成**从源码抽样**：
  1. 扫 src/ui.c 里所有 DrawTextWin / DrawTextCenterWin / DrawTextInRow 调用
  2. 把 y 参数里的 UIS(n) / winH-n / TextHeightWin() 换算成"600 基准设计坐标"
  3. 同一个纵向带里若有两个文本，且 x 区间相交 → 判重叠
  4. 顺便检查是否越出屏幕

用法
----
    python tools/subtitle_check.py            # 摘要
    python tools/subtitle_check.py -v         # 列出每条文本
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, "src", "ui.c")

# 设计基准：UIS(v) = v * renderH / 458，报表按 600 高（800x600 窗口）折算
BASE_H = 600
UIS_BASE = 458.0

# 文本高度（设计值）
TEXT_H = 24.0


def norm(expr):
    """把 y 表达式折算成 600 高下的设计坐标。无法解析返回 None。"""
    if not expr:
        return None
    e = expr.strip()

    m = re.fullmatch(r"UIS\(\s*(-?\d+)\s*\)", e)
    if m:
        return int(m.group(1)) * BASE_H / UIS_BASE

    m = re.fullmatch(r"winH\s*-\s*UIS\(\s*(-?\d+)\s*\)", e)
    if m:
        return BASE_H - int(m.group(1)) * BASE_H / UIS_BASE

    m = re.fullmatch(r"winH\s*/\s*(\d+)", e)
    if m:
        return BASE_H / int(m.group(1))

    m = re.fullmatch(r"winH\s*/\s*(\d+)\s*\+\s*UIS\(\s*(-?\d+)\s*\)", e)
    if m:
        return BASE_H / int(m.group(1)) + int(m.group(2)) * BASE_H / UIS_BASE

    m = re.fullmatch(r"winH\s*-\s*(\d+)", e)
    if m:
        return BASE_H - int(m.group(1))

    m = re.fullmatch(r"(\d+)", e)
    if m:
        return int(m.group(1))

    return None


CALL_RE = re.compile(
    r"(DrawTextWin|DrawTextCenterWin)\s*\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*(.+?)\s*[,)]",
    re.S,
)


FUNC_RE = re.compile(r"^(?:static\s+)?(?:void|int|float|unsigned|const\s+char\*)\s+"
                     r"(\w+)\s*\([^;{]*\)\s*\{", re.M)


def func_at(src, pos):
    """返回 pos 所在的函数名（只画同一个界面里的文本才可能重叠）"""
    name = "?"
    for m in FUNC_RE.finditer(src, 0, pos):
        name = m.group(1)
    return name


def extract():
    """返回 [(函数名, x表达式, y坐标, 字符串字面量)]"""
    src = io.open(UI, encoding="utf-8", errors="replace").read()
    out = []
    for m in CALL_RE.finditer(src):
        fn, xexpr, yexpr, rest = m.group(1), m.group(2), m.group(3), m.group(4)
        y = norm(yexpr)
        if y is None:
            continue

        # 取一个能读懂的标签：优先第一个字符串字面量
        lit = re.search(r'"([^"\\]{2,32})"', rest)
        label = lit.group(1) if lit else fn

        # 估算 x 区间
        if fn == "DrawTextCenterWin":
            w = len(label) * 12
            x0, x1 = BASE_H / 2 - w / 2, BASE_H / 2 + w / 2
        else:
            xm = re.search(r"UIS\(\s*(-?\d+)\s*\)", xexpr)
            x0 = int(xm.group(1)) if xm else 20
            if "winW -" in xexpr.replace(" ", ""):
                x0 = 800 - x0
            x1 = x0 + len(label) * 12

        out.append((fn, func_at(src, m.start()), x0, x1, y, y + TEXT_H, label))
    return out


def main():
    verbose = "-v" in sys.argv
    items = extract()
    print("=" * 70)
    print(" CANITY 字幕/文字带检测")
    print("=" * 70)
    print(" 抽样到 %d 处文本绘制（设计基准 %dx600）\n" % (len(items), 800))

    if verbose:
        for fn, fname, x0, x1, y0, y1, label in sorted(items, key=lambda t: (t[1], t[4], t[2])):
            print("  [%-22s] y=%5.1f..%5.1f  x=%4.0f..%4.0f  %s"
                  % (fname[:22], y0, y1, x0, x1, label[:24]))
        print()

    # ---- 重叠检测：**只在同一个绘制函数内部比较** ----
    # 不同界面（主菜单 vs HUD vs 安全屋）永远不会同时出现在屏幕上，
    # 跨函数比较只会产生误报。
    overlaps = []
    byfn = {}
    for it in items:
        byfn.setdefault(it[1], []).append(it)
    for fname, group in byfn.items():
        for i in range(len(group)):
            for j in range(i + 1, len(group)):
                a, b = group[i], group[j]
                vy = min(a[5], b[5]) - max(a[4], b[4])
                vx = min(a[3], b[3]) - max(a[2], b[2])
                if vy > 6 and vx > 6:
                    overlaps.append((a, b, vy, vx, fname))

    # ---- 越界检测 ----
    oob = [t for t in items if t[4] < 4 or t[5] > BASE_H - 4]

    print("=" * 70)
    if overlaps:
        print(" ❌ 发现 %d 处文字重叠（按纵向重叠量排序）" % len(overlaps))
        print("=" * 70)
        for a, b, vy, vx, fname in sorted(overlaps, key=lambda t: -t[2])[:20]:
            print("  [%s] 重叠 %.0fpx 高 / %.0fpx 宽" % (fname[:24], vy, vx))
            print("    A y=%.0f  x=%.0f..%.0f  %s" % (a[4], a[2], a[3], a[6][:40]))
            print("    B y=%.0f  x=%.0f..%.0f  %s" % (b[4], b[2], b[3], b[6][:40]))
    else:
        print(" ✅ 无文字重叠")

    if oob:
        print("\n ⚠️  %d 处越出屏幕：" % len(oob))
        for t in oob[:10]:
            print("    y=%.0f..%.0f  %s" % (t[4], t[5], t[6][:40]))
    else:
        print(" ✅ 无越界")

    return 1 if overlaps or oob else 0


if __name__ == "__main__":
    sys.exit(main())
