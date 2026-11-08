#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""技能树布局自检 —— 2026-11-08 重写：**直接读坐标表**。

## 为什么重写

以前这个工具按"椭圆 + 角度 + 半径"公式把 skills.c 的常数再算一遍，
于是出现"工具说没事、实机压住"，以及"改了公式工具就崩"。

现在 skills.c 里就是一张 `kNodeNX[] / kNodeNY[]` 的 40 项常量表，
工具**只读这张表**，和界面用同一份数据。

## 判据
  ① 每个节点都在安全区内（留 40px 边距）
  ② 任意两个方框重叠面积 < 8%（擦边 1~2% 不算问题）
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'skills.c')

# 与 ui.c 的 SK_NODE_K 保持一致（方框尺寸基准）
NODE_K = 0.78
SK_WX, SK_WY = 210.0, 120.0
WORLD = 6.0


def read_table(src, name):
    m = re.search(name + r'\[\s*\w+\s*\]\s*=\s*\{(.*?)\};', src, re.S)
    if not m:
        return None
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return [float(x.strip().rstrip('f')) for x in body.split(',') if x.strip()]


def main():
    src = open(SRC, encoding='utf-8', errors='replace').read()
    nx = read_table(src, 'kNodeNX')
    ny = read_table(src, 'kNodeNY')
    if not nx or not ny or len(nx) != len(ny):
        print('✗ 读不到 kNodeNX / kNodeNY 坐标表')
        return 1

    n = len(nx)
    win_w, win_h = 1264, 711
    node_w = int(SK_WX * 0.55 * NODE_K)
    node_h = int(SK_WY * 0.62 * NODE_K)
    half_w, half_h = win_w / 2.0 - 40, win_h / 2.0 - 40

    pts = []
    for i in range(n):
        x = (nx[i] - 0.5) * WORLD * SK_WX * NODE_K
        y = (ny[i] - 0.5) * WORLD * SK_WY * NODE_K
        pts.append((i, x, y))

    oob = [(i, x, y) for i, x, y in pts
           if abs(x) + node_w / 2 > half_w or abs(y) + node_h / 2 > half_h]
    bad = []
    area = node_w * node_h
    for a in range(n):
        for b in range(a + 1, n):
            dx = abs(pts[a][1] - pts[b][1])
            dy = abs(pts[a][2] - pts[b][2])
            ox, oy = node_w - dx, node_h - dy
            if ox > 0 and oy > 0 and (ox * oy) / area >= 0.08:
                bad.append((a, b, ox, oy))

    print('  节点数 %d，方框 %.0fx%.0f px，安全区 ±%.0f x ±%.0f'
          % (n, node_w, node_h, half_w, half_h))
    if oob:
        print('✗ 出界 %d 个：' % len(oob))
        for i, x, y in oob[:8]:
            print('    id %2d  (%.0f, %.0f)' % (i, x, y))
    else:
        print('✓ 全部节点在屏幕内')
    if bad:
        print('✗ 明显重叠 %d 对：' % len(bad))
        for a, b, ox, oy in bad[:10]:
            print('    id %2d × id %2d  叠 %.0fx%.0f px' % (a, b, ox, oy))
    else:
        print('✓ 没有明显重叠')
    return 0 if not oob and not bad else 1


if __name__ == '__main__':
    sys.exit(main())
