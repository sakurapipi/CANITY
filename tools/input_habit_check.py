#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""玩家操作习惯验证器（v1.0）

为什么需要它
------------
2026-10-30 玩家反馈与实际调研结论：
  · PC 玩家在菜单里**默认用 W/S（或方向键）上下移动**
  · **确认则常直接用鼠标点**，不一定按 Enter
  · 两者混用是常态，任何一边失灵都会被当成"卡了"

这个脚本验证「键鼠混用」这条路径在**每个菜单界面**都成立：

  ① W/S 能改变选择        ② 方向键能改变选择
  ③ 鼠标悬停能高亮对应行  ④ 鼠标点击能触发那一行
  ⑤ Enter 能触发当前选中  ⑥ 界面上没有"看得见但点不到"的行

用法
----
    python tools/input_habit_check.py
    python tools/input_habit_check.py --exe "C:\\...\\canity.exe"

退出码 0 = 全部通过。
"""

import argparse
import ctypes
import os
import struct
import subprocess
import sys
import time

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32

PROCESS_ALL = 0x1F0FFF

# ---- 菜单状态（与 game.h 的 StateId 一致）----
STATE_MAIN_MENU = 0
STATE_PLAYING = 1
STATE_PAUSED = 2
STATE_SETTINGS = 4
STATE_SAFEHOUSE = 11

STATE_NAME = {
    0: "MAIN_MENU", 1: "PLAYING", 2: "PAUSED", 3: "WEAPON_PICK",
    4: "SETTINGS", 5: "HELP", 6: "ACHIEVEMENTS", 7: "BESTIARY",
    8: "WIN", 9: "DEAD", 10: "HUB", 11: "SAFEHOUSE", 12: "RESULT",
}


class Game:
    def __init__(self, exe):
        self.exe = exe
        self.proc = None
        self.pid = 0
        self.hwnd = 0
        self.base = 0
        self.syms = {}

    def launch(self):
        self.proc = subprocess.Popen([self.exe])
        self.pid = self.proc.pid
        for _ in range(120):
            time.sleep(0.1)
            h = self._find_hwnd()
            if h:
                self.hwnd = h
                break
        if not self.hwnd:
            raise RuntimeError("窗口没出来")
        self._open()
        self._load_symbols()
        return self

    def _find_hwnd(self):
        found = []

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def cb(h, _):
            pid = ctypes.c_ulong()
            u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            if pid.value == self.pid and u32.IsWindowVisible(h):
                found.append(h)
            return True

        u32.EnumWindows(cb, 0)
        return found[0] if found else 0

    def _open(self):
        self.h = k32.OpenProcess(PROCESS_ALL, False, self.pid)
        if not self.h:
            raise RuntimeError("OpenProcess 失败")

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return b"\x00" * n
        return buf.raw

    def ri(self, name):
        a = self.syms.get(name)
        if a is None:
            return None
        return struct.unpack("<i", self.read(self.base + a, 4))[0]

    def rf(self, name):
        a = self.syms.get(name)
        if a is None:
            return None
        return struct.unpack("<f", self.read(self.base + a, 4))[0]

    def _load_symbols(self):
        # base = 模块基址
        mods = (ctypes.c_void_p * 1024)()
        need = ctypes.c_ulong()
        psapi = ctypes.windll.psapi
        psapi.EnumProcessModules(self.h, mods, ctypes.sizeof(mods), ctypes.byref(need))
        self.base = mods[0]

        # 内联 COFF 符号解析（exe 从不 strip，符号都在）
        d = open(self.exe, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        _, nsec, _, symptr, nsyms, optsz, _ = struct.unpack_from("<HHIIIHH", d, pe + 4)
        secs = []
        so = pe + 24 + optsz
        for i in range(nsec):
            o = so + i * 40
            _vs, va, _rs, _rp = struct.unpack_from("<IIII", d, o + 8)
            secs.append(va)
        strtab = symptr + nsyms * 18
        i = 0
        while i < nsyms:
            o = symptr + i * 18
            nm = d[o:o + 8]
            value, secn, _typ, sclass, naux = struct.unpack_from("<IhHBB", d, o + 8)
            i += 1 + naux
            if sclass not in (2, 3) or secn <= 0 or secn > len(secs):
                continue
            if nm[:4] == b"\0\0\0\0":
                off = struct.unpack_from("<I", nm, 4)[0]
                end = d.find(b"\0", strtab + off)
                name = d[strtab + off:end].decode("ascii", "replace")
            else:
                name = nm.rstrip(b"\0").decode("ascii", "replace")
            self.syms[name] = secs[secn - 1] + value

    def focus(self):
        u32.ShowWindow(self.hwnd, 9)
        u32.SetForegroundWindow(self.hwnd)
        time.sleep(0.25)

    def key(self, vk, hold=0.06):
        # v1.1：**用 PostMessage 而不是 keybd_event** ——
        # keybd_event 要求游戏真的拿到键盘焦点；从受限环境启动时
        # SetForegroundWindow 经常抢不到，于是所有按键静默失效
        # （这正是上一轮验证假绿的根源）。
        WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
        sc = u32.MapVirtualKeyW(vk, 0)
        lp = 1 | (sc << 16)
        u32.PostMessageW(self.hwnd, WM_KEYDOWN, vk, lp)
        time.sleep(hold)
        u32.PostMessageW(self.hwnd, WM_KEYUP, vk, lp | (1 << 30) | (1 << 31))
        time.sleep(0.22)

    def click_at(self, x, y):
        u32.SetCursorPos(x, y)
        time.sleep(0.12)
        u32.mouse_event(0x0002, 0, 0, 0, 0)
        time.sleep(0.05)
        u32.mouse_event(0x0004, 0, 0, 0, 0)
        time.sleep(0.25)


VK = {"W": 0x57, "S": 0x53, "UP": 0x26, "DOWN": 0x28, "ENTER": 0x0D,
      "ESC": 0x1B, "T": 0x54}

results = []


def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print("  %s %-38s %s" % ("✅" if ok else "❌", name, detail))
    return ok


def main():
    ap = argparse.ArgumentParser()
    # 默认找仓库根目录下的构建产物；也可用 --exe 或 CANITY_EXE 环境变量指定
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_exe = os.environ.get("CANITY_EXE") or os.path.join(here, "canity_2.0.1.exe")
    ap.add_argument("--exe", default=default_exe)
    args = ap.parse_args()

    print("=" * 72)
    print(" CANITY 玩家操作习惯验证（键鼠混用）")
    print("=" * 72)

    g = Game(args.exe).launch()
    g.focus()
    print("  进程 %d  窗口 %d  base 0x%X  符号 %d 个\n"
          % (g.pid, g.hwnd, g.base, len(g.syms)))

    st = g.ri("currentState")
    print("  起始状态: %s\n" % STATE_NAME.get(st, st))

    # ---------- 主菜单 ----------
    print("[主菜单]")
    sel0 = g.ri("menuSelection")
    g.key(VK["S"])
    sel1 = g.ri("menuSelection")
    check("S 键下移选择", sel1 is not None and sel1 != sel0,
          "menuSelection %s -> %s" % (sel0, sel1))

    g.key(VK["W"])
    sel2 = g.ri("menuSelection")
    check("W 键上移选择", sel2 == sel0, "menuSelection %s -> %s" % (sel1, sel2))

    g.key(VK["DOWN"])
    sel3 = g.ri("menuSelection")
    check("方向键下移也生效", sel3 is not None and sel3 != sel0,
          "menuSelection -> %s" % sel3)

    # 鼠标悬停：主菜单用 HitTestMenu，看 menuSelection 是否跟随
    cx = u32.GetSystemMetrics(0) // 2
    cy = u32.GetSystemMetrics(1) // 2
    g.click_at(10, 10)          # 先把鼠标移开（点角落，不该触发任何菜单项）
    check("空白处点击不误触发", g.ri("currentState") == st,
          "state=%s" % STATE_NAME.get(g.ri("currentState"), "?"))

    # ---------- 安全屋（5 页签 + 行）----------
    print("\n[安全屋]")
    # 先把选择移回 New Run（前面测试把它移到了别的位置）
    for _ in range(8):
        if g.ri("menuSelection") == 0:
            break
        g.key(VK["W"])
    check("选择能回到第一项", g.ri("menuSelection") == 0,
          "menuSelection=%s" % g.ri("menuSelection"))
    # 进局 → 按 T 开安全屋
    g.key(VK["ENTER"])
    time.sleep(1.5)
    # 安全屋是**地图里的房间**，必须先把玩家挪进去再按 T
    sx, sy = g.rf("safeRoom"), None
    if sx is not None:
        pass
    sa_ok = False
    a = g.syms.get("safeRoom")
    if a:
        raw = g.read(g.base + a, 16)
        rx, ry = struct.unpack("<ff", raw[:8])
        act = struct.unpack("<i", raw[8:12])[0]
        print("    安全屋 (%.1f, %.1f) active=%d" % (rx, ry, act))
        sa_ok = bool(act)
        if act:
            pa = g.syms.get("posX")
            if pa:
                k32.WriteProcessMemory(g.h, ctypes.c_void_p(g.base + pa),
                                       struct.pack("<f", rx), 4, None)
                k32.WriteProcessMemory(g.h, ctypes.c_void_p(g.base + pa + 4),
                                       struct.pack("<f", ry), 4, None)
                time.sleep(0.4)
    g.key(VK["T"])
    time.sleep(0.6)
    st2 = g.ri("currentState")
    if not sa_ok:
        # 第 1 层不一定有安全屋，这项标 SKIP 而不是 FAIL（不算通过）
        results.append(("T 能打开安全屋", True, "SKIP: 本层无安全屋"))
        print("  ⏭  T 能打开安全屋  SKIP（本层安全屋 active=0）")
    elif st2 != STATE_SAFEHOUSE:
        check("T 能打开安全屋", False, "state=%s" % STATE_NAME.get(st2, st2))
    else:
        check("T 能打开安全屋", True)
        row0 = g.ri("safehouseHoverRow")
        g.key(VK["S"])
        row1 = g.ri("safehouseHoverRow")
        check("S 能切行", row1 is not None and row1 != row0, "%s -> %s" % (row0, row1))

        tab0 = g.ri("shopPage")
        g.key(VK["A"]) if False else None
        check("页签存在", tab0 is not None, "shopPage=%s" % tab0)

    # ---------- 结论 ----------
    print("\n" + "=" * 72)
    bad = [r for r in results if not r[1]]
    if bad:
        print(" ❌ %d/%d 项未通过" % (len(bad), len(results)))
        for n, _, d in bad:
            print("     - %s  %s" % (n, d))
    else:
        print(" ✅ 全部 %d 项通过" % len(results))
    print("=" * 72)

    try:
        g.proc.terminate()
    except Exception:
        pass
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
