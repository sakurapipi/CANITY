#!/usr/bin/env python3
"""Export Canity's current translations to l10n/zh.tsv.

The TSV is the editable single source of truth:
    en <TAB> zh <TAB> source

`apply_tsv.py` regenerates src/lang_table.inc from it. Inline Tr("en","zh")
calls remain as fallbacks, but LangTr() prefers the generated table, so the
TSV always wins at runtime.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "l10n" / "zh.tsv"
PAIR = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')


def readsrc():
    pairs = []
    lang = (ROOT / "src" / "lang.c").read_text(encoding="utf-8-sig")
    m = re.search(r"static const TrEntry kTrTable\[\] = \{(.*?)\n\};", lang, re.S)
    if m:
        for en, zh in PAIR.findall(m.group(1)):
            pairs.append((en, zh, "table"))
    else:
        # 已经切换到 #include "lang_table.inc" 之后，从 TSV 读
        if OUT.exists():
            for line in OUT.read_text(encoding="utf-8").splitlines():
                if not line or line.startswith("#"):
                    continue
                en, zh, src = line.split("\t")
                pairs.append((en, zh, src))
    for path in list((ROOT / "src").rglob("*.c")):
        text = path.read_text(encoding="utf-8-sig")
        for en, zh in re.findall(r'Tr\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)', text):
            pairs.append((en, zh, "tr"))
    return pairs


def merge(pairs):
    chosen = {}
    conflicts = []
    order = []
    for en, zh, src in pairs:
        if en not in chosen:
            chosen[en] = (zh, src)
            order.append(en)
        else:
            old_zh, old_src = chosen[en]
            if old_zh != zh:
                # 表优先；表内部重复时保留第一次，和 Transl() 的旧行为一致
                conflicts.append((en, old_zh, zh, old_src, src))
    return chosen, order, conflicts


def main():
    chosen, order, conflicts = merge(readsrc())
    lines = ["# Canity 中文词条表（单一来源）",
             "# 改这里，然后运行： python l10n/apply_tsv.py",
             "# 列：英文<TAB>中文<TAB>来源；# 开头是注释",
             ]
    for en in sorted(order):
        zh, src = chosen[en]
        lines.append("%s\t%s\t%s" % (en, zh, "table" if src == "table" else "tr"))
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("wrote %s: %d entries" % (OUT.relative_to(ROOT), len(chosen)))
    if conflicts:
        print("conflicts (kept first): %d" % len(conflicts), file=sys.stderr)
        for en, a, b, sa, sb in conflicts[:20]:
            print("  %-40s %s(%s) vs %s(%s)" % (en, a, sa, b, sb), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
