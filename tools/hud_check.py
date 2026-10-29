"""HUD 重叠自检 —— 用真实矩形（含文字宽度）判断，不是只看 y。

用法：python tools/hud_check.py
改 HUD 布局时：先在下面 BANDS 里占一条带，再改 ui.c 的坐标，最后跑一遍这个脚本。

坐标系：render 空间（800x600 下约 610x458）。字宽按 11px/字符估算（TH=24 的点阵字）。
"""
import sys

RENDER_W = 610
RENDER_H = 458
TH = 24        # 文字高度
CHW = 11       # 单字符宽度（估算）


def w_of(s):
    return len(s) * CHW


# (名字, y, 文本（用来算宽度）, 对齐 left/center/right, 高度)
BANDS = [
    ("LEVEL",   18,  "LEVEL 24",                     "left",   TH),
    ("SANITY",  52,  "SANITY: 100%",                 "left",   TH),
    ("FUSES",   86,  "FUSES 4/4  SAFE ZONE-HEALING", "left",   TH),
    ("FX1",    116,  "DRAINING 100%",                "left",   TH),
    ("FX2",    142,  "BLINDED 60%",                  "left",   TH),
    ("FX3",    168,  "SLOWED 40%",                   "left",   TH),
    ("ITEMS",   44,  "1 FIREPOT x99",                "right",  TH * 3 + 16),
    ("MOD",    204,  "CRAMPED",                      "left",   TH),
    ("THEME",  230,  "BRICK",                        "left",   TH),
    ("CALHINT", 118, "SPACE: LOCK CALIBRATION",      "center", TH),
    ("CALBAR",  146, "",                             "center", 22),
    ("PICKUP",  278, "PERFECT  +ENERGY +SANITY",     "center", TH),
    ("SAFEHINT", 424, "T: SHOP    SPACE: CALIBRATE", "left", TH),
    ("WEAPON",  310, "CRANKWRENCH  ENE -14  LV0",    "left",   TH),
    ("ENERGY",  334, "ENERGY [##########] 120",      "left",   16),
    ("LIGHT",   366, "LIGHT  [##########]  ON",      "left",   16),
    ("RUN",     398, "RUN    [##########]",          "left",   16),
]

BAR_W = RENDER_W - 320   # 校准条宽度（居中）

print("=" * 78)
print("HUD 元素矩形（render 空间 %dx%d，字高 %d，字宽 %d）" % (RENDER_W, RENDER_H, TH, CHW))
print("=" * 78)

rects = []
for name, y, text, align, h in BANDS:
    w = BAR_W if name == "CALBAR" else max(w_of(text), 40)
    if align == "left":
        x0 = 20
    elif align == "right":
        x0 = RENDER_W - 20 - w
    else:
        x0 = (RENDER_W - w) // 2
    x1 = x0 + w
    y1 = y + h
    rects.append((name, x0, y, x1, y1))
    print("  %-8s x=%3d..%3d  y=%3d..%3d" % (name, x0, x1, y, y1))

print()
print("=" * 78)
print("矩形重叠检查（两个轴都相交才算重叠）")
print("=" * 78)

bad = 0
for i in range(len(rects)):
    n1, x0, y0, x1, y1 = rects[i]
    for j in range(i + 1, len(rects)):
        n2, a0, b0, a1, b1 = rects[j]
        if x0 < a1 and a0 < x1 and y0 < b1 and b0 < y1:
            ox = min(x1, a1) - max(x0, a0)
            oy = min(y1, b1) - max(y0, b0)
            print("  [X] %-8s 与 %-8s 重叠  %dx%d px" % (n1, n2, ox, oy))
            bad += 1

print()
print("越界检查")
for name, x0, y0, x1, y1 in rects:
    if x1 > RENDER_W + 2:
        print("  [X] %s 右边越界：%d > %d" % (name, x1, RENDER_W))
        bad += 1
    if y1 > RENDER_H:
        print("  [X] %s 下边越界：%d > %d" % (name, y1, RENDER_H))
        bad += 1

print()
if bad == 0:
    print("结果：%d 个元素，0 处重叠，0 处越界 OK" % len(rects))
else:
    print("结果：%d 处问题，需要调整" % bad)
sys.exit(1 if bad else 0)
