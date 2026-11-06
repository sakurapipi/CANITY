# -*- coding: utf-8 -*-
"""
把技能树的辐射布局**离线画出来**，不用进游戏就能看几何是否合理。

数据全部从 src/skills.c 读：
  · kSkillX[] / kSkillY[]  归一化坐标
  · kSkillEdges[]          邻接表
  · skillDefs[]            名字

输出 PNG，用 read_image 看。
"""
import io
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SK = os.path.join(ROOT, 'src', 'skills.c')

W, H = 1000, 1000
MARGIN = 110


def main():
    s = io.open(SK, encoding='utf-8').read()

    # ---- 坐标 ----
    mx = re.search(r'static const float kSkillX\[SKILL_COUNT\] = \{(.*?)\};', s, re.S)
    my = re.search(r'static const float kSkillY\[SKILL_COUNT\] = \{(.*?)\};', s, re.S)
    if not mx or not my:
        print('没找到坐标数组')
        return 1

    def nums(blob):
        blob = re.sub(r'/\*.*?\*/', '', blob, flags=re.S)
        return [float(x) for x in re.findall(r'(\d+\.\d+)f?', blob)]

    xs = nums(mx.group(1))
    ys = nums(my.group(1))
    print('坐标数: x=%d y=%d' % (len(xs), len(ys)))
    if len(xs) != len(ys):
        print('**坐标数量不一致，布局会错位**')
        return 1

    # ---- 邻接表 ----
    me = re.search(r'static const SkillEdge kSkillEdges\[SKILL_COUNT\] = \{(.*?)\n\};', s, re.S)
    edges = []
    if me:
        for m in re.finditer(r'\{\{\s*(-?\d+),\s*(-?\d+)\},\s*(\d+),\s*(\d+)\}', me.group(1)):
            a, b, cnt, all_ = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))
            nb = []
            if cnt >= 1 and a >= 0:
                nb.append(a)
            if cnt >= 2 and b >= 0:
                nb.append(b)
            edges.append((nb, all_))
    print('边数: %d' % len(edges))

    # ---- 名字 ----
    names = re.findall(r'\{"([A-Z][A-Z0-9 \'\-]+)"', s)
    names = [n for n in names if len(n) < 24][:len(xs)]
    print('名字数: %d' % len(names))

    # ---- 画 ----
    img = Image.new('RGB', (W, H), (10, 13, 18))
    d = ImageDraw.Draw(img)

    def px(x):
        return MARGIN + x * (W - 2 * MARGIN)

    def py(y):
        return MARGIN + y * (H - 2 * MARGIN)

    # 网格参考
    for i in range(1, 10):
        v = MARGIN + i * (W - 2 * MARGIN) / 10.0
        d.line([(v, MARGIN), (v, H - MARGIN)], fill=(22, 28, 36))
        d.line([(MARGIN, v), (W - MARGIN, v)], fill=(22, 28, 36))

    # 边（先画）
    for i, (nb, all_) in enumerate(edges):
        if i >= len(xs):
            break
        for n in nb:
            if n < 0 or n >= len(xs):
                continue
            col = (80, 190, 255) if all_ else (45, 90, 120)
            d.line([(px(xs[i]), py(ys[i])), (px(xs[n]), py(ys[n]))],
                   fill=col, width=3 if all_ else 2)

    # 节点
    R = 22
    for i in range(len(xs)):
        x, y = px(xs[i]), py(ys[i])
        # 联动节点用六边形，中心用大圆
        cross = (i < len(edges)) and edges[i][1]
        if i == len(xs) - 1:
            fill, outline, rr = (60, 40, 20), (255, 200, 80), 34
        elif cross:
            fill, outline, rr = (60, 20, 40), (255, 80, 200), R
        elif i >= 16 and i <= 19:
            fill, outline, rr = (50, 40, 20), (255, 200, 80), R + 4
        else:
            fill, outline, rr = (18, 36, 48), (90, 200, 240), R

        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=fill, outline=outline, width=3)

    # 标签
    for i in range(len(xs)):
        x, y = px(xs[i]), py(ys[i])
        nm = names[i] if i < len(names) else str(i)
        d.text((x - len(nm) * 3, y + 30), nm, fill=(200, 210, 220))
        d.text((x - 6, y - 6), str(i), fill=(255, 255, 255))

    # v6 第 42 轮：**不再往项目根目录丢临时 PNG**
    # 用户明确要求「跑起来不会产生垃圾文件」。
    # 默认写到 %TEMP%，需要长期保留时用 --out 指定。
    out = os.environ.get('CANITY_PREVIEW_OUT')
    if not out:
        out = os.path.join(os.environ.get('TEMP', '.'), 'canity_layout_preview.png')
    out = os.path.abspath(out)
    img.save(out)
    print('已生成: %s' % out)

    # ---- 重叠检查 ----
    overlaps = []
    for i in range(len(xs)):
        for j in range(i + 1, len(xs)):
            dx = (xs[i] - xs[j]) * (W - 2 * MARGIN)
            dy = (ys[i] - ys[j]) * (H - 2 * MARGIN)
            if (dx * dx + dy * dy) ** 0.5 < 56:
                overlaps.append((i, j))
    if overlaps:
        print('**重叠节点对**: %s' % overlaps[:8])
    else:
        print('重叠检查: 无')
    return 0


if __name__ == '__main__':
    sys.exit(main())
