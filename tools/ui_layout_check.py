#!/usr/bin/env python3
"""读 canity_ui_layout.txt，判定文字矩形有没有重叠。

## 为什么有这个脚本

`src/ui.c` 的注释里一直写着「tools/ui_layout_check.py 读它做矩形重叠判定」，
**但这个脚本从来不存在** —— 所以 F9 导出的布局文件一直没人分析，
文字压字只能靠人眼在截图上看出来。

## 用法

    # 游戏里改好界面，按 F9 导出（exe 同目录，或 %TEMP%\\canity_ui_layout.txt）
    python tools/ui_layout_check.py                     # 自动找
    python tools/ui_layout_check.py path\\to\\dump.txt   # 指定

## 判定

两个矩形重叠面积 / 较小矩形面积 >= 阈值（默认 25%）就算"压字"。
背景块、整行容器这类"预期重叠"靠 label 白名单跳过：
  · 以 `#` 开头的行是注释
  · label 里带 `[bg]` / `[panel]` / `[row]` 的当作容器，不参与比较

退出码：0 = 没有超阈值重叠；1 = 有。
"""
import os
import re
import sys

THRESHOLD = 0.25

# 这些 label 是容器/背景，和别的东西重叠是设计意图
SKIP_TAGS = ('[bg]', '[panel]', '[row]', '[box]', '[fill]')


def find_dump(argv):
    if len(argv) > 1 and os.path.isfile(argv[1]):
        return argv[1]
    # 游戏写的是 `ui_layout.txt`（ui.c DumpUILayout("ui_layout.txt")）。
    # 早期版本只找 `canity_ui_layout.txt`，于是不带参数跑永远 exit 2。
    NAMES = ('ui_layout.txt', 'canity_ui_layout.txt')
    cands = []
    here = os.getcwd()
    for n in NAMES:
        cands.append(os.path.join(here, n))
        cands.append(os.path.join(here, 'work', 'codex-build', n))
    tmp = os.environ.get('TEMP') or os.environ.get('TMP')
    if tmp:
        for n in NAMES:
            cands.append(os.path.join(tmp, n))
    for p in cands:
        if os.path.isfile(p):
            return p
    return None


def parse(path):
    rects, meta = [], ''
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('#'):
                meta = line
                continue
            parts = line.split(None, 4)
            if len(parts) < 4:
                continue
            try:
                x0, y0, x1, y1 = (int(v) for v in parts[:4])
            except ValueError:
                continue
            label = parts[4] if len(parts) > 4 else ''
            rects.append((x0, y0, x1, y1, label))
    return meta, rects


def overlap_ratio(a, b):
    ax0, ay0, ax1, ay1 = a[:4]
    bx0, by0, bx1, by1 = b[:4]
    ix0, iy0 = max(ax0, bx0), max(ay0, by0)
    ix1, iy1 = min(ax1, bx1), min(ay1, by1)
    if ix1 <= ix0 or iy1 <= iy0:
        return 0.0
    inter = (ix1 - ix0) * (iy1 - iy0)
    aa = max(1, (ax1 - ax0) * (ay1 - ay0))
    bb = max(1, (bx1 - bx0) * (by1 - by0))
    return inter / min(aa, bb)


def main():
    path = find_dump(sys.argv)
    if not path:
        print('找不到 canity_ui_layout.txt。先在游戏里按 F9 导出一帧，再跑这个脚本。')
        print('（导出位置：exe 同目录，或 %TEMP%\\canity_ui_layout.txt）')
        return 2

    meta, rects = parse(path)
    print('文件:', path)
    if meta:
        print(meta.lstrip('# '))
    print('矩形数:', len(rects))
    print()

    if len(rects) < 2:
        print('矩形太少，这一帧大概是空的（没画文字）—— 换一个界面再按 F9。')
        return 2

    # ---- 越界检测（2026-11-08 加）----
    # 实机 dump 里技能树提示行是 x=-72..872，画布只有 800 —— 旧版完全不报。
    canvas_w = canvas_h = 0
    mm = re.search(r'canvas=(\d+)x(\d+)', meta or '')
    if mm:
        canvas_w, canvas_h = int(mm.group(1)), int(mm.group(2))
    bad = []
    if canvas_w and canvas_h:
        for r0, r1, r2, r3, label in rects:
            if r0 < 0 or r1 < 0 or r2 > canvas_w or r3 > canvas_h:
                bad.append((r0, r1, r2, r3, label))
        if bad:
            print('✗ 越界 %d 条（画布 %dx%d）：' % (len(bad), canvas_w, canvas_h))
            for r in bad[:10]:
                print('    "%s"  x=%d..%d  y=%d..%d'
                      % (r[4] or '?', r[0], r[2], r[1], r[3]))
        else:
            print('✓ 没有越界')

    hits = []
    for i in range(len(rects)):
        if any(t in rects[i][4] for t in SKIP_TAGS):
            continue
        for j in range(i + 1, len(rects)):
            if any(t in rects[j][4] for t in SKIP_TAGS):
                continue
            r = overlap_ratio(rects[i], rects[j])
            if r >= THRESHOLD:
                hits.append((r, rects[i], rects[j]))

    if not hits:
        print('✓ 没有超阈值的文字重叠（阈值 %.0f%%）' % (THRESHOLD * 100))
        return 1 if bad else 0

    hits.sort(key=lambda t: -t[0])
    print('✗ 发现 %d 处文字重叠：' % len(hits))
    for r, a, b in hits:
        print('  %.0f%%  "%s"(%d,%d,%d,%d)  ×  "%s"(%d,%d,%d,%d)'
              % (r * 100, a[4] or '?', a[0], a[1], a[2], a[3],
                 b[4] or '?', b[0], b[1], b[2], b[3]))
    return 1


if __name__ == '__main__':
    sys.exit(main())
