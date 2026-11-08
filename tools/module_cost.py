"""module_cost.py —— 量"改一个域要读多少行"。

为什么需要它
    架构改得漂不漂亮是主观的，但"改一次要读多少行"是客观的。
    这个脚本把每个域的**必读量**算出来（实现 + 契约头 + 卡片），
    目标写在 docs/架构重构方案_v1.md：单域 ≤ 2,500 行（≈35k token）。

用法
    python tools/module_cost.py                 # 全部域
    python tools/module_cost.py --budget 2500   # 超预算的标红
    python tools/module_cost.py --json          # 给 CI 用

判定
    cost = 实现文件行数 + 它 include 的自家契约头行数 + 卡片行数
    （不递归第三方头，因为那些进任何任务都要读，是常量成本）
"""
import argparse
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
INC = os.path.join(ROOT, 'include')
DOCS = os.path.join(ROOT, 'docs', 'modules')

# 域定义：域 -> (实现文件, 契约头, 卡片)
DOMAINS = {
    'core':     (['core/tick.c', 'core/rng.c'], [], None),
    'phase':    (['domain/phase/phase.c'], ['phase.h'], 'phase.md'),
    'hatch':    (['hatch.c', 'domain/hatch/hatch_place.c'], ['hatch.h'], 'hatch.md'),
    'collapse': (['domain/collapse/collapse.c'], ['collapse.h'], 'collapse.md'),
    'throwing': (['throwing.c'], ['throwing.h'], 'throwing.md'),
    'skills':   (['skills.c'], ['skills.h'], 'skills.md'),
    'lockbox':  (['lockbox.c'], ['lockbox.h'], 'lockbox.md'),
    'warden':   (['warden.c'], ['warden.h'], 'warden.md'),
    'shop':     (['shop.c'], ['shop.h'], 'shop.md'),
    'map_rooms': (['map_rooms.c'], ['map_rooms.h'], 'map_rooms.md'),
    'ui':       (['ui.c', 'domain/ui/ui_text.c'], ['ui.h', 'ui_text.h'], 'ui.md'),
    'render':   (['render.c'], ['render.h'], 'render.md'),
}


def count_lines(path):
    if not os.path.exists(path):
        return 0
    return sum(1 for _ in io.open(path, encoding='utf-8', errors='replace'))


def find_c(name):
    direct = os.path.join(SRC, name.replace('/', os.sep))
    if os.path.exists(direct):
        return direct
    for root, _, files in os.walk(SRC):
        if os.path.basename(name) in files:
            return os.path.join(root, os.path.basename(name))
    return direct


def extern_count(path):
    if not os.path.exists(path):
        return 0
    t = io.open(path, encoding='utf-8', errors='replace').read()
    return len(re.findall(r'^\s*extern\s+', t, re.M))


TESTS = os.path.join(ROOT, 'tests', 'domains')


def measure():
    out = []
    for domain, (cs, hs, card) in sorted(DOMAINS.items()):
        impl = sum(count_lines(find_c(c)) for c in cs)
        hdr = sum(count_lines(os.path.join(INC, h)) for h in hs)
        card_lines = count_lines(os.path.join(DOCS, card)) if card else 0
        ext = sum(extern_count(find_c(c)) for c in cs)
        # 第 2 轮起：域专属测试也算"必读量" —— 改这个域要先看它的用例
        test_lines = count_lines(os.path.join(TESTS, 'test_%s.c' % domain))
        out.append({
            'domain': domain,
            'impl': impl,
            'header': hdr,
            'card': card_lines,
            'test': test_lines,
            'extern': ext,
            'cost': impl + hdr + card_lines + test_lines,
            'missing': [p for p in
                        ([find_c(c) for c in cs] +
                         [os.path.join(INC, h) for h in hs] +
                         ([os.path.join(DOCS, card)] if card else []))
                        if not os.path.exists(p)],
        })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--budget', type=int, default=2500)
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()

    rows = measure()
    if args.json:
        print(json.dumps(rows, ensure_ascii=False, indent=2))
        return 0

    print('  %-11s %7s %7s %6s %6s %7s %8s' %
          ('域', '实现', '契约头', '卡片', '测试', 'extern', '必读合计'))
    print('  ' + '-' * 64)
    over = 0
    for r in rows:
        flag = ''
        if r['cost'] > args.budget:
            flag = '  ✗ 超预算'
            over += 1
        elif r['impl'] == 0 and r['header'] == 0:
            flag = '  （未拆分）'
        print('  %-11s %7d %7d %6d %6d %7d %8d%s' %
              (r['domain'], r['impl'], r['header'], r['card'],
               r.get('test', 0), r['extern'], r['cost'], flag))
    print('')
    print('  预算 %d 行；超预算 %d 个域' % (args.budget, over))
    print('  注：game.c 9,557 行不在表里 —— 它就是要被拆掉的那一个。')
    return 1 if over else 0


if __name__ == '__main__':
    sys.exit(main())
