#!/usr/bin/env python3
"""Regenerate src/lang_table.inc from l10n/zh.tsv.

Run after editing the TSV:
    python l10n/apply_tsv.py
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TSV = ROOT / "l10n" / "zh.tsv"
OUT = ROOT / "src" / "lang_table.inc"


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    if not TSV.exists():
        print("missing", TSV, file=sys.stderr)
        return 1
    rows = []
    for line in TSV.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 2:
            continue
        en, zh = parts[0], parts[1]
        rows.append((en, zh))
    out = [
        "/* 由 l10n/apply_tsv.py 从 l10n/zh.tsv 生成，请不要手改。 */",
        "static const TrEntry kTrTable[] = {",
    ]
    for en, zh in rows:
        out.append('    { "%s", "%s" },' % (esc(en), esc(zh)))
    out.append("};")
    out.append("")
    OUT.write_text("\n".join(out), encoding="utf-8")
    print("wrote %s: %d entries" % (OUT.relative_to(ROOT), len(rows)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
