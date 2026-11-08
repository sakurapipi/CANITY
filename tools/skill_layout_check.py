#!/usr/bin/env python3
"""技能树布局自检：节点有没有叠在一起、有没有跑出屏幕。

## 为什么要有

加节点最容易犯两个错：
  ① 两个节点半径/角度撞了 → 屏幕上叠成一坨，点不中
  ② 节点半径太大 → 默认缩放下跑到屏幕外（实测现在已有 9 个）

这个脚本从 `src/skills.c` 的 `SkillNodeNorm()` 里读参数，
算出每个节点的屏幕坐标，然后断言两件事。

## 用法

    python tools/skill_layout_check.py
    python tools/skill_layout_check.py --zoom 0.78 --width 1264 --height 711

退出码：0 = 布局合格；1 = 有重叠或出界；2 = 解析失败。
"""
import argparse
import io
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SK_WX, SK_WY = 210.0, 120.0     # ui.c:902-903
WORLD = 6.0                     # SkillWorldPos 里的 ×6


def read(path):
    with io.open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def parse_const(text, name, default):
    m = re.search(r'#define\s+%s\s+([0-9.]+)f?' % name, text)
    return float(m.group(1)) if m else default


def parse_norm(src):
    """从 SkillNodeNorm 里抽出：扇区数、半径表、内环半径、内环槽数。"""
    m = re.search(r'void SkillNodeNorm\([^)]*\)\s*\{(.*?)\n\}', src, re.S)
    if not m:
        return None
    body = m.group(1)

    sectors = None
    for mm in re.finditer(r'/\s*([0-9.]+)f?\s*\)\s*;', body):
        v = float(mm.group(1))
        if v in (3.0, 4.0, 5.0, 6.0, 8.0):
            sectors = int(v)
    if sectors is None:
        mm = re.search(r'6\.2831853f\s*\*\s*\(float\)\s*\w+\s*/\s*([0-9.]+)f', body)
        if mm:
            sectors = int(float(mm.group(1)))
    if sectors is None:
        sectors = 5

    mm = re.search(r'radius\s*=\s*([0-9.]+)f\s*\+\s*([0-9.]+)f\s*\*\s*\(float\)\s*\w+', body)
    if mm:
        base, step = float(mm.group(1)), float(mm.group(2))
    else:
        base, step = 0.18, 0.17
    radii = [base + step * i for i in range(6)]
    inner = re.search(r'\*\s*([0-9.]+)f\s*;\s*\*ny', body)
    inner_r = float(inner.group(1)) if inner else 0.37
    return {'sectors': sectors, 'radii': radii, 'inner_r': inner_r}


def norm_pos(id_, cfg, origin_id, count):
    """复刻 SkillNodeNorm 的映射（与 C 侧同构）。"""
    if id_ == origin_id:
        return 0.5, 0.5
    sectors = cfg['sectors']
    if id_ < 16:
        branch, row = id_ // 4, id_ % 4
    elif id_ >= 26:
        branch, row = 4, id_ - 26
    elif 20 <= id_ <= 24:
        ang = -math.pi / 2 + 2 * math.pi * ((id_ - 20) + 0.5) / 5.0
        r = cfg['inner_r']
        return 0.5 + math.cos(ang) * r, 0.5 + math.sin(ang) * r
    else:
        branch, row = id_ - 16, 4
    ang = -math.pi / 2 + 2 * math.pi * branch / sectors
    r = cfg['radii'][row] if row < len(cfg['radii']) else cfg['radii'][-1]
    return 0.5 + math.cos(ang) * r, 0.5 + math.sin(ang) * r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zoom', type=float, default=0.78)
    ap.add_argument('--width', type=int, default=1264)
    ap.add_argument('--height', type=int, default=711)
    ap.add_argument('--margin', type=int, default=40)
    a = ap.parse_args()

    skills = read(os.path.join(ROOT, 'src', 'skills.c'))
    common = read(os.path.join(ROOT, 'include', 'common.h'))
    ui = read(os.path.join(ROOT, 'src', 'ui.c'))

    count = int(parse_const(common, 'SKILL_COUNT', 30))
    wx = parse_const(ui, 'SK_WX', SK_WX)
    wy = parse_const(ui, 'SK_WY', SK_WY)
    # 屏幕像素尺寸（ui.c: nodeW = SK_WX*0.55*skVK, nodeH = SK_WY*0.62*skVK）
    node_w = 0.55 * wx * a.zoom
    node_h = 0.62 * wy * a.zoom

    m = re.search(r'#define\s+SKILL_CROSS_ID\s+(\d+)', ui)
    origin = int(m.group(1)) if m else 25

    cfg = parse_norm(skills)
    if not cfg:
        print('解析 SkillNodeNorm 失败')
        return 2

    print('节点数 %d，扇区 %d，缩放 %.2f，窗口 %dx%d'
          % (count, cfg['sectors'], a.zoom, a.width, a.height))
    print('安全半宽 %.0f px，半高 %.0f px（留 %d px 边距）'
          % (a.width / 2 - a.margin, a.height / 2 - a.margin, a.margin))
    print()

    pts = []
    for i in range(count):
        nx, ny = norm_pos(i, cfg, origin, count)
        px = (nx - 0.5) * WORLD * wx * a.zoom
        py = (ny - 0.5) * WORLD * wy * a.zoom
        pts.append((i, px, py))

    half_w = a.width / 2 - a.margin
    half_h = a.height / 2 - a.margin

    off = [(i, px, py) for i, px, py in pts
           if abs(px) > half_w or abs(py) > half_h]
    if off:
        print('✗ 出界 %d 个节点：' % len(off))
        for i, px, py in off[:12]:
            print('    id %2d  水平 %5.0f  垂直 %5.0f' % (i, px, py))
    else:
        print('✓ 全部节点在屏幕内')

    # 重叠：矩形相交（同辐条上下相邻垂直间距 95 > 高 58，不算重叠）
    close = []
    for x in range(len(pts)):
        for y in range(x + 1, len(pts)):
            dx = abs(pts[x][1] - pts[y][1])
            dy = abs(pts[x][2] - pts[y][2])
            if dx < node_w and dy < node_h:
                close.append((pts[x][0], pts[y][0], dx, dy))
    if close:
        print('✗ 重叠 %d 对（节点 %.0fx%.0f px）：' % (len(close), node_w, node_h))
        for i, j, dx, dy in close[:12]:
            print('    id %2d × id %2d  dx=%.0f dy=%.0f' % (i, j, dx, dy))
    else:
        print('✓ 没有重叠（矩形不相交）')

    return 1 if (off or close) else 0


if __name__ == '__main__':
    sys.exit(main())
