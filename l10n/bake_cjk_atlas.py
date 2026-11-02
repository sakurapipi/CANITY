#!/usr/bin/env python3
"""Bake the CJK glyph atlases used by Canity.

Source font: Noto Sans SC (SIL Open Font License 1.1), installed on the
build machine. Only the glyphs that actually appear in Canity's C string
literals are rasterised. The atlases are pre-baked at 12/24/36/48 px
so the game can pick a 1:1 bitmap for every UI scale instead of upscaling a
12px source and smearing dense characters.

The repository carries no font file and the game does not depend on a system
font at runtime.

Usage:  python l10n/bake_cjk_atlas.py
Output: src/cjk_glyphs.c, l10n/cjk_preview.png
"""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT_PATH = pathlib.Path(r"C:\Windows\Fonts\NotoSansSC-VF.ttf")
BASE = 12
SCALES = (1, 2, 3, 4)
THRESHOLD = 96
WEIGHT = 500                  # Medium
EXTRA = "，。！？；：、（）【】「」《》“”‘’…—·×÷±℃°※→←↑↓★☆■□●○◆◇％＋－＝"


def strip_comments_and_collect(text: str) -> set[str]:
    """Return every non-ASCII codepoint that appears inside a C string literal."""
    chars: set[str] = set()
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        elif c == '"':
            i += 1
            buf = bytearray()
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    esc = text[i + 1]
                    mapping = {"n": b"\n", "t": b"\t", "r": b"\r",
                               '"': b'"', "\\": b"\\", "'": b"'" }
                    if esc in mapping:
                        buf += mapping[esc]
                        i += 2
                        continue
                    if esc == "x" and i + 3 < n:
                        try:
                            buf.append(int(text[i + 2:i + 4], 16))
                            i += 4
                            continue
                        except ValueError:
                            pass
                    i += 1
                    continue
                buf += text[i].encode("utf-8")
                i += 1
            i += 1
            try:
                s = buf.decode("utf-8")
            except UnicodeDecodeError:
                continue
            chars |= {ch for ch in s if ord(ch) >= 0x80}
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return chars


def collect_chars() -> set[str]:
    chars: set[str] = set()
    for pattern in ("src/**/*.c", "src/**/*.h", "src/**/*.inc", "include/**/*.h", "include/**/*.inc"):
        for path in ROOT.glob(pattern):
            chars |= strip_comments_and_collect(path.read_text(encoding="utf-8-sig"))
    chars |= set(EXTRA)
    chars.discard(" ")
    return chars


def make_font(size: int):
    font = ImageFont.truetype(str(FONT_PATH), size)
    try:
        font.set_variation_by_axes([WEIGHT])
    except Exception:
        pass
    return font


def bake(chars: set[str]):
    """Return list of (codepoint, {scale: [rows]})."""
    fonts = {s: make_font(BASE * s) for s in SCALES}
    glyphs = []
    for cp in sorted(ord(c) for c in chars):
        ch = chr(cp)
        per_scale = {}
        for s in SCALES:
            cell = BASE * s
            font = fonts[s]
            bbox = font.getbbox(ch)
            img = Image.new("L", (cell, cell), 0)
            ImageDraw.Draw(img).text((-bbox[0], cell - 1 - bbox[3]), ch, font=font, fill=255)
            rows = []
            for r in range(cell):
                bits = 0
                for c in range(cell):
                    if img.getpixel((c, r)) >= THRESHOLD:
                        bits |= 1 << (cell - 1 - c)
                rows.append(bits)
            per_scale[s] = rows
        glyphs.append((cp, per_scale))
    return glyphs


def write_c(glyphs):
    out = ROOT / "src" / "cjk_glyphs.c"
    total = sum(BASE * s for s in SCALES)
    lines = [
        "/*",
        " * 本文件由 l10n/bake_cjk_atlas.py 自动生成，请不要手改。",
        " *",
        " * 字形来源：Noto Sans SC（SIL Open Font License 1.1），",
        " * 只取 Canity 源码里实际用到的字符，按 UI 缩放预先烘成",
        " * 12/24/36/48px 单色点阵，运行时 1:1 取图、不再放大。",
        " * 许可证全文见 l10n/OFL-NotoSansSC.txt；发布时需随游戏保留该文件。",
        " */",
        '#include "common.h"',
        '#include "render.h"',
        "",
        "typedef struct { unsigned int cp; unsigned short glyph; } CjkIndexEntry;",
        "",
        "#define CJK_GLYPH_COUNT %d" % len(glyphs),
        "",
    ]
    # 每个缩放一份扁平数组，行数 = 12*scale
    for s in SCALES:
        cell = BASE * s
        lines.append("static const unsigned long long kCjkRows%d[CJK_GLYPH_COUNT * %d] = {" % (s, cell))
        for cp, per in glyphs:
            rows = per[s]
            for r in range(0, len(rows), 12):
                chunk = rows[r:r + 12]
                lines.append("    %s,  /* U+%04X %s %d/%d */" % (
                    ", ".join("0x%016X" % v for v in chunk), cp, chr(cp), r // 12 + 1, cell))
        lines.append("};")
        lines.append("")
    lines += [
        "static const CjkIndexEntry kCjkIndex[CJK_GLYPH_COUNT] = {",
    ]
    for i, (cp, _) in enumerate(glyphs):
        lines.append("    { 0x%04X, %d }," % (cp, i))
    lines += [
        "};",
        "",
        "/* 二分查找；找不到返回 NULL，上层画占位框。 */",
        "const unsigned long long* CjkRows(unsigned int cp, int scale) {",
        "    int lo = 0, hi = CJK_GLYPH_COUNT - 1;",
        "    while (lo <= hi) {",
        "        int mid = (lo + hi) / 2;",
        "        if (kCjkIndex[mid].cp == cp) {",
        "            int i = kCjkIndex[mid].glyph;",
        "            switch (scale) {",
    ]
    for s in SCALES:
        cell = BASE * s
        lines.append("                case %d: return &kCjkRows%d[i * %d];" % (s, s, cell))
    lines += [
        "                default: return NULL;",
        "            }",
        "        }",
        "        if (kCjkIndex[mid].cp < cp) lo = mid + 1;",
        "        else hi = mid - 1;",
        "    }",
        "    return NULL;",
        "}",
        "",
    ]
    out.write_text("\n".join(lines), encoding="utf-8")
    return out


def preview(out_png):
    sample = "设置手电保险丝配电箱藏身点摇电金币每日战役成就图鉴背包"
    cell = BASE * 2
    font = make_font(cell)
    canvas = Image.new("L", (len(sample) * cell, cell), 0)
    for i, ch in enumerate(sample):
        bbox = font.getbbox(ch)
        img = Image.new("L", (cell, cell), 0)
        ImageDraw.Draw(img).text((-bbox[0], cell - 1 - bbox[3]), ch, font=font, fill=255)
        img = img.point(lambda v: 255 if v >= THRESHOLD else 0)
        canvas.paste(img, (i * cell, 0))
    canvas = canvas.resize((canvas.width * 2, canvas.height * 2), Image.NEAREST)
    canvas.save(out_png)


def main():
    if not FONT_PATH.exists():
        print("missing font:", FONT_PATH, file=sys.stderr)
        return 1
    chars = collect_chars()
    glyphs = bake(chars)
    out = write_c(glyphs)
    preview(ROOT / "l10n" / "cjk_preview.png")
    empty = [cp for cp, per in glyphs if not any(per[1])]
    print("glyphs: %d  -> %s" % (len(glyphs), out.relative_to(ROOT)))
    print("empty 12px glyphs:", ["U+%04X" % cp for cp in empty][:20], "total", len(empty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
