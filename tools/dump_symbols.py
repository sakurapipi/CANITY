#!/usr/bin/env python3
"""把 exe 的 COFF 符号表导成「变量名 -> 地址/大小」的清单。

用途：
  · 外部作弊/调试工具（Cheat Engine、x64dbg、自写注入器）按名字找变量，
    不用每次重编译后手动重新扫地址
  · 我们自己的 tools/inject.py 也是靠符号找地址的，这份表让它和外部工具共用同一份数据

用法：
  python tools/dump_symbols.py                    # 默认找 canity_2.0.1.exe
  python tools/dump_symbols.py -e xxx.exe         # 指定 exe
  python tools/dump_symbols.py --json out.json    # 指定输出

输出：
  dist/canity_symbols.json   { "version": "...", "imageBase": "...", "symbols": {name: {rva, size, kind}} }
  dist/canity_symbols.txt    人读的清单
  dist/canity_signatures.txt 每个关键变量的「值特征」（bool/int 用），方便扫描

注意：exe 必须**不 strip**（COFF 符号还在），否则这张表是空的。
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W64 = os.environ.get("CANITY_TOOLCHAIN", "")   # 留空则用 PATH 里的 nm

# 想重点标出来的变量（作弊/调试最常用）
INTERESTING = [
    "sanity", "battery", "stamina", "shards", "cores", "fusesHeld",
    "currentLevel", "runTime", "bestLevel", "isEndlessRun", "trainingMode",
    "dailyMode", "campaignMode", "campaignStage", "bossLevel", "bossMaxHp",
    "relicTotal", "totalKills", "curWeapon", "weaponCd", "weaponAtkCharge",
    "flashlightOn", "breakerOpen", "isCharging", "throwSelected",
    "posX", "posY", "dirX", "dirY", "planeX", "planeY",
    "monster", "lightBeast", "leeches", "extras", "containers", "breaker",
    "safeRoom", "shots", "shards_debris", "thrown", "smokes",
    "g_save", "g_achievements", "skillRank", "relicOwned", "attachOwned",
    "weaponRank", "weaponXp", "weaponAttach",
    "upgBattery", "upgSpeed", "upgQuiet", "upgSanity", "upgEco", "upgRest",
]


def run_nm(exe):
    nm = os.path.join(W64, "nm.exe")
    if not os.path.exists(nm):
        nm = "nm"
    out = subprocess.run([nm, "-S", "--defined-only", exe],
                         capture_output=True, text=True, errors="replace")
    return out.stdout.splitlines()


def parse(lines):
    syms = {}
    pat = re.compile(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$")
    for ln in lines:
        m = pat.match(ln.strip())
        if not m:
            continue
        addr, size, kind, name = m.group(1), m.group(2), m.group(3), m.group(4)
        # 只要数据符号（B=bss D=data R=rodata）和函数（T=t）
        syms[name] = {
            "rva": int(addr, 16),
            "size": int(size, 16),
            "kind": kind,
        }
    return syms


def parse_coff_symbols(path):
    """直接读取 PE 中的 COFF 符号表。

    w64devkit 的 nm 在 PE 可执行文件上有时只输出重复的 `.bss` 行，
    但链接器保留的 COFF 表仍然完整。调试注入工具本来就按这个格式
    读取，这里复用同一套 RVA 计算，避免把未 strip 的 exe 误判为空。
    """
    try:
        data = open(path, "rb").read()
        if data[:2] != b"MZ":
            return {}
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            return {}
        _, nsec, _, symptr, nsyms, optsz, _ = struct.unpack_from(
            "<HHIIIHH", data, pe + 4)
        if not symptr or not nsyms:
            return {}
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] not in (0x10B, 0x20B):
            return {}

        section_rvas = []
        section_base = opt + optsz
        for i in range(nsec):
            off = section_base + i * 40
            _, vaddr = struct.unpack_from("<II", data, off + 8)
            section_rvas.append(vaddr)

        string_table = symptr + nsyms * 18
        symbols = {}
        index = 0
        while index < nsyms:
            off = symptr + index * 18
            raw_name = data[off:off + 8]
            value, section, _, _, aux_count = struct.unpack_from(
                "<IhHBB", data, off + 8)
            if raw_name[:4] == b"\0\0\0\0":
                name_off = struct.unpack_from("<I", raw_name, 4)[0]
                start = string_table + name_off
                end = data.find(b"\0", start)
                name = data[start:end].decode("latin-1", "replace") if end > start else ""
            else:
                name = raw_name.rstrip(b"\0").decode("latin-1", "replace")
            if 0 < section <= nsec and name and not name.startswith(".refptr"):
                symbols[name] = {
                    "rva": section_rvas[section - 1] + value,
                    "size": 0,
                    "kind": "?",
                }
            index += 1 + aux_count
        return symbols
    except (OSError, IndexError, struct.error, UnicodeError):
        return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-e", "--exe", default=None)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    exe = args.exe
    if not exe:
        cands = sorted([f for f in os.listdir(ROOT) if f.startswith("canity_") and f.endswith(".exe")])
        if not cands:
            cands = sorted([f for f in os.listdir(ROOT) if f.endswith(".exe")])
        if not cands:
            print("没找到 exe")
            return 1
        exe = os.path.join(ROOT, cands[-1])
    exe = os.path.abspath(exe)
    if not os.path.exists(exe):
        print("不存在:", exe)
        return 1

    syms = parse(run_nm(exe))
    if not syms:
        syms = parse_coff_symbols(exe)
    if not syms:
        print("符号表是空的 —— exe 被 strip 过了？")
        return 2

    version = "unknown"
    cf = os.path.join(ROOT, "include", "common.h")
    if os.path.exists(cf):
        m = re.search(r'CANITY_VERSION_STR\s+"([^"]+)"', open(cf, encoding="utf-8").read())
        if m:
            version = m.group(1)

    outdir = os.path.join(ROOT, "dist")
    os.makedirs(outdir, exist_ok=True)

    data = {
        "version": version,
        "exe": os.path.basename(exe),
        "imageBase": "0x140000000",     # x64 PE 默认基址；开了 ASLR 时按模块基址加 rva
        "note": "地址是 RVA。实际虚拟地址 = 模块基址 + rva。exe 不能 strip。",
        "symbols": syms,
    }
    jpath = args.json or os.path.join(outdir, "canity_symbols.json")
    json.dump(data, open(jpath, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    print("JSON ->", jpath, "(%d 个符号)" % len(syms))

    tpath = os.path.join(outdir, "canity_symbols.txt")
    with open(tpath, "w", encoding="utf-8") as f:
        f.write("# Canity %s 符号表（RVA）\n" % version)
        f.write("# 虚拟地址 = 模块基址 + RVA；模块基址在 CE 里看 canity.exe 的 base\n")
        f.write("#\n# ==== 常用变量 ====\n")
        for n in INTERESTING:
            if n in syms:
                s = syms[n]
                f.write("%-24s RVA=0x%08X  size=%-6d %s\n" % (n, s["rva"], s["size"], s["kind"]))
        f.write("\n# ==== 全部 ====\n")
        for n in sorted(syms):
            s = syms[n]
            f.write("%-40s RVA=0x%08X  size=%-6d %s\n" % (n, s["rva"], s["size"], s["kind"]))
    print("TXT  ->", tpath)

    # 扫描用特征：关键变量当前的常见初值（重启后一般还是这个值）
    spath = os.path.join(outdir, "canity_signatures.txt")
    sigs = {
        "sanity (float 100.0)": "0000C842",
        "battery (float 100.0)": "0000C842",
        "currentLevel (int 1)": "01000000",
        "MONSTER_HP 之类不用扫，直接按 RVA 读更快": "",
    }
    with open(spath, "w", encoding="utf-8") as f:
        f.write("# 建议：优先用 canity_symbols.txt 里的 RVA 直接读，比扫描快且不会误命中\n")
        for k, v in sigs.items():
            f.write("%s  %s\n" % (k, v))
    print("SIG  ->", spath)
    return 0


if __name__ == "__main__":
    sys.exit(main())
