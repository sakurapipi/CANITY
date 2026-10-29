"""热键检查器 —— 找出「在某个界面里按了没反应」和「按键串到别的界面」的问题。

用法：
    python tools/hotkey_check.py

原理（近似静态分析，不是运行时）：
  1. 扫 src/game.c 的 WindowProc 里所有 `wParam == 'X'` / `wParam == VK_*` 判定
  2. 用最近的 `currentState == STATE_xxx` 判断它属于哪个界面
  3. 对每个界面列出「消费了哪些键」
  4. 报告两类问题：
     · LEAK  同一个键在一个界面里既是导航又是移动 —— 会把人带出界面
     · DEAD  某个界面里出现的键，在别的界面完全没有处理（可能是忘了接）

局限：
  · 只看 WindowProc 里的分支，不看 Update 里的 keyPressed 读取
  · 嵌套 if 的归属是按"最近的状态判断"猜的，复杂嵌套可能误判
  · 所以它报出来的要人工复核；但漏掉的很少
"""
import io
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, "src", "game.c")

src = io.open(GAME, encoding="utf-8").read()
lines = src.split("\n")

# ---- 抓所有按键判定 + 它所在的状态 ----
KEY_PATTERNS = [
    (re.compile(r"wParam\s*==\s*'(\w)'"), "char"),
    (re.compile(r"wParam\s*==\s*(VK_\w+)"), "vk"),
    (re.compile(r"\b(menuUp|menuDown|menuLeft|menuRight|menuConfirm|menuCancel)\b"), "menu"),
]

STATE_PAT = re.compile(r"currentState\s*==\s*(STATE_\w+)")

entries = []          # (state, key, kind, lineno)
cur_state = "STATE_PLAYING"   # WindowProc 默认是游戏中
depth_state = []              # 记录进入的分支状态，用于近似还原

for i, ln in enumerate(lines):
    m = STATE_PAT.search(ln)
    if m:
        cur_state = m.group(1)
    for pat, kind in KEY_PATTERNS:
        for mm in pat.finditer(ln):
            key = mm.group(1)
            if kind == "menu":
                continue      # menuXxx 是聚合量，单独统计
            entries.append((cur_state, key, kind, i + 1))

by_state = defaultdict(set)
for st, key, kind, ln in entries:
    by_state[st].add(key if kind == "char" else key.replace("VK_", ""))

print("=" * 74)
print("热键检查器 —— WindowProc 里的按键归属")
print("=" * 74)
print()

# 统一的键名（char 和 VK 都映射成同一个可读名）
def norm(k):
    mp = {"SPACE": "Space", "RETURN": "Enter", "ESCAPE": "Esc", "TAB": "Tab",
          "UP": "Up", "DOWN": "Down", "LEFT": "Left", "RIGHT": "Right"}
    return mp.get(k, k)

for st in sorted(by_state):
    keys = sorted({norm(k) for k in by_state[st]})
    print("  %-22s %s" % (st.replace("STATE_", "") + ":", ", ".join(keys)))

print()
print("=" * 74)
print("泄漏检查：同一个键既被菜单消费、又被游戏内的移动读取")
print("=" * 74)

# 游戏内的移动键（Update 里读 keys[]）
MOVE_KEYS = {"W", "A", "S", "D", "SHIFT"}
leaks = 0
for st, keys in by_state.items():
    if st == "STATE_PLAYING":
        continue
    nav = {norm(k) for k in keys} & MOVE_KEYS
    if nav:
        print("  [LEAK] %s 里消费了移动键 %s" % (st.replace("STATE_", ""), sorted(nav)))
        print("         如果这个界面的分支**先于**游戏内移动处理，就没问题；")
        print("         如果晚了，玩家按这个键会边操作边走出去。")
        leaks += 1

if leaks == 0:
    print("  没有发现菜单界面直接消费移动键。")

print()
print("=" * 74)
print("覆盖检查：这些键在源码里完全没有任何 wParam 判定")
print("=" * 74)

ALL_EXPECTED = ["Space", "Enter", "Esc", "Tab", "Up", "Down", "Left", "Right"]
allkeys = {norm(k) for keys in by_state.values() for k in keys}

# v2.8：导航键大多是通过 menuUp/menuDown/... 这些**聚合量**处理的，
# 不是字面量 wParam 比较。聚合量出现就等于覆盖了对应方向键。
agg = set()
for pat, kind in KEY_PATTERNS:
    if kind != "menu":
        continue
    for mm in pat.finditer(src):
        agg.add(mm.group(1))
AGG_COVERS = {
    "menuUp": ["Up", "W"], "menuDown": ["Down", "S"],
    "menuLeft": ["Left", "A"], "menuRight": ["Right", "D"],
    "menuConfirm": ["Enter", "Space"], "menuCancel": ["Esc"],
}
covered = set(allkeys)
for a, ks in AGG_COVERS.items():
    if a in agg:
        covered |= set(ks)

missing = [k for k in ALL_EXPECTED if k not in covered]
if missing:
    for k in missing:
        print("  [DEAD] %s 既没有字面量判定，也没有聚合量覆盖" % k)
else:
    print("  8 个基础导航键都有处理（字面量或聚合量）。")
    print("  跟踪到的聚合量: %s" % ", ".join(sorted(agg)))

print()
print("=" * 74)
print("状态总数：%d 个界面，%d 条按键判定" % (len(by_state), len(entries)))
print("=" * 74)

sys.exit(1 if (leaks or missing) else 0)
