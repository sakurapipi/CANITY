#!/usr/bin/env python3
"""Audit Canity's Chinese localisation.

Checks:
  · duplicate keys in l10n/zh.tsv
  · inline Tr("en","zh") whose zh differs from the TSV (TSV wins at runtime)
  · draw-call string literals that have no TSV entry
  · source CJK characters missing from the baked atlas

Run:
    python l10n/audit_l10n.py
Exit code 0 = no blocking errors (warnings are printed).
"""
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "l10n"))
from bake_cjk_atlas import strip_comments_and_collect
TSV = ROOT / "l10n" / "zh.tsv"
ATLAS = ROOT / "src" / "cjk_glyphs.c"


def load_tsv():
    rows = {}
    dups = []
    order = []
    for line in TSV.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        en, zh = parts[0], parts[1]
        if en in rows:
            dups.append(en)
        else:
            rows[en] = zh
            order.append(en)
    return rows, dups


def inline_pairs():
    pairs = []
    for path in (ROOT / "src").rglob("*.c"):
        text = path.read_text(encoding="utf-8-sig")
        for en, zh in re.findall(r'Tr\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)', text):
            pairs.append((path.name, en, zh))
    return pairs


def collect_strings():
    """All non-ASCII chars and display-looking literals inside C string literals."""
    chars = set()
    literals = {}
    for path in list((ROOT / "src").rglob("*.c")) + list((ROOT / "src").rglob("*.h")) + \
            list((ROOT / "src").rglob("*.inc")) + list((ROOT / "include").rglob("*.h")) + \
            list((ROOT / "include").rglob("*.inc")):
        text = path.read_text(encoding="utf-8-sig")
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
            s = m.group(1)
            chars |= strip_comments_and_collect(text)
            if re.search(r"[A-Za-z]{2}", s) and not s.startswith("%"):
                literals.setdefault(s, path.name)
    return chars, literals


def atlas_chars():
    if not ATLAS.exists():
        return set()
    text = ATLAS.read_text(encoding="utf-8-sig")
    return {chr(int(cp, 16)) for cp in re.findall(r"/\* U\+([0-9A-F]{4,6})", text)}


DRAW = re.compile(r'(?:DrawText(?:Win|CenterWin|SmallWin)|PutTextRaw)\s*\(([^;]*)\)', re.S)
STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def draw_literals():
    out = {}
    for path in (ROOT / "src").rglob("*.c"):
        text = path.read_text(encoding="utf-8-sig")
        for m in DRAW.finditer(text):
            for s in STR.findall(m.group(1)):
                if re.search(r"[A-Za-z]{2}", s):
                    out.setdefault(s, path.name)
    return out


def main():
    rows, dups = load_tsv()
    warnings = 0

    if dups:
        print("ERROR duplicate TSV keys:", ", ".join(sorted(set(dups))[:20]))
        return 1

    print("TSV entries:", len(rows))

    stale = []
    for fname, en, zh in inline_pairs():
        if en in rows and rows[en] != zh:
            stale.append((fname, en, zh, rows[en]))
    if stale:
        print("WARN inline Tr() disagrees with TSV (TSV wins):", len(stale))
        for fname, en, a, b in stale[:15]:
            print("  %-12s %-34s inline=%s  tsv=%s" % (fname, en[:34], a, b))
        warnings += 1

    missing = {s: f for s, f in draw_literals().items() if s not in rows}
    # 纯中文/格式串不算“缺翻译”；只报英文界面文本
    missing = {s: f for s, f in missing.items() if re.search(r"[A-Za-z]", s)}
    if missing:
        print("WARN draw-call literals without TSV entry:", len(missing))
        for s, f in list(missing.items())[:25]:
            print("  %-12s %s" % (f, s[:70]))
        warnings += 1

    need = collect_strings()[0]
    have = atlas_chars()
    absent = sorted(need - have)
    if absent:
        print("ERROR CJK atlas is stale. Re-run: python l10n/bake_cjk_atlas.py")
        print("  missing:", "".join(absent[:80]))
        return 1

    print("atlas glyphs:", len(have), " source CJK chars:", len(need))
    print("audit:", "PASS" if warnings == 0 else "PASS with warnings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
