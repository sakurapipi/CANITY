"""解耦闸门：任何进入项目的代码都必须先过这一关。

规则见 docs/架构规则_解耦闸门.md：
  1. 域预算   每个域"最大单文件 + 头 + 卡片 + 域测试" ≤ BUDGET（默认 2500）
  2. 单文件   任何 .c ≤ FILE_LIMIT（默认 2600 行）
  3. 函数     任何函数 ≤ FUNC_HARD（默认 300 行）；>200 行软上限 10 个
  4. 登记      src/ 下每个 .c 必须被 tools/build_sources.py 收录
  5. 新域      新增 src/domain/<x>/ 必须在 module_cost.py 的 DOMAINS 注册

用法：
    python -X utf8 tools/coupling_gate.py           # 报告 + 退出码
    python -X utf8 tools/coupling_gate.py --quiet    # 只在失败时打印
"""
import importlib.util
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET = 2500
FILE_LIMIT = 2600
FUNC_HARD = 300
SOFT_200 = 10

# 生成数据/外部引入的豁免（不是手写逻辑，不参与行数与函数上限判定）。
EXEMPT_FILES = {
    "src/cjk_glyphs.c",       # 中文点阵字库表，由工具生成
}

# 仅网页构建使用的实现（tools/build_web.py 自己维护列表），不参与 Windows 源清单登记。
WEB_ONLY = {
    "audio_web.c", "gamepad_web.c", "music_web.c",
    "platform/main_web.c", "platform/plat_web.c", "steam/steam_zone_web.c",
}


def count_lines(path):
    try:
        return sum(1 for _ in io.open(path, encoding='utf-8', errors='replace'))
    except OSError:
        return 0


def module_cost_module():
    path = os.path.join(ROOT, 'tools', 'module_cost.py')
    spec = importlib.util.spec_from_file_location('module_cost', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def function_sizes():
    out = []
    for root, _d, fs in os.walk(os.path.join(ROOT, 'src')):
        for f in sorted(fs):
            if not f.endswith('.c'):
                continue
            p = os.path.join(root, f)
            if os.path.relpath(p, ROOT).replace('\\', '/') in EXEMPT_FILES:
                continue
            lines = io.open(p, encoding='utf-8', errors='replace').read().split('\n')
            starts = []
            for i, l in enumerate(lines):
                if re.match(r'^(?:static\s+)?(?:void|int|float|unsigned|double|char)\s+\**\w+\s*\(', l):
                    starts.append(i)
            for k, s in enumerate(starts):
                e = starts[k + 1] - 1 if k + 1 < len(starts) else len(lines)
                out.append((os.path.relpath(p, ROOT).replace('\\', '/'), s + 1, e - s + 1))
    return out


def registered_sources():
    r = subprocess.run([sys.executable, '-X', 'utf8', 'tools/build_sources.py'],
                       cwd=ROOT, capture_output=True, text=True,
                       encoding='utf-8', errors='replace')
    out = set()
    for l in r.stdout.split('\n'):
        l = l.strip().replace('\\', '/')
        if not l.endswith('.c'):
            continue
        if l.startswith('src/'):
            l = l[4:]
        out.add(l)
    return out


def main():
    quiet = '--quiet' in sys.argv
    fails = []
    mc = module_cost_module()

    if not quiet:
        print('=== 域预算（最大单文件 + 卡片 + 域测试）===')
    for r in mc.measure():
        cost = r['cost']
        flag = 'OK ' if cost <= BUDGET else 'X  '
        if cost > BUDGET:
            fails.append('域 %s 超预算：%d > %d（最大文件 %d）'
                         % (r['domain'], cost, BUDGET, r['biggest']))
        if not quiet:
            print('  %s %-10s cost=%4d  biggest=%4d  header=%d card=%d test=%d'
                  % (flag, r['domain'], cost, r['biggest'],
                     r['header'], r['card'], r.get('test', 0)))

    if not quiet:
        print('=== 单文件上限 %d 行 ===' % FILE_LIMIT)
    for root, _d, fs in os.walk(os.path.join(ROOT, 'src')):
        for f in sorted(fs):
            if not f.endswith('.c'):
                continue
            p = os.path.join(root, f)
            rel0 = os.path.relpath(p, ROOT).replace('\\', '/')
            if rel0 in EXEMPT_FILES:
                continue
            n = count_lines(p)
            if n > FILE_LIMIT:
                rel = os.path.relpath(p, ROOT).replace('\\', '/')
                fails.append('文件 %s 有 %d 行 > %d' % (rel, n, FILE_LIMIT))
                if not quiet:
                    print('  X  %s %d 行' % (rel, n))

    fns = function_sizes()
    big = [x for x in fns if x[2] > FUNC_HARD]
    over200 = [x for x in fns if x[2] > 200]
    if not quiet:
        print('=== 函数上限 ===')
        print('  >%d 行：%d 个（必须 0）' % (FUNC_HARD, len(big)))
        print('  >200 行：%d 个（软上限 %d）' % (len(over200), SOFT_200))
    for rel, line, size in big:
        fails.append('函数过大：%s:%d 有 %d 行 > %d' % (rel, line, size, FUNC_HARD))

    reg = registered_sources()
    for root, _d, fs in os.walk(os.path.join(ROOT, 'src')):
        for f in sorted(fs):
            if not f.endswith('.c'):
                continue
            rel = os.path.relpath(os.path.join(root, f),
                                  os.path.join(ROOT, 'src')).replace('\\', '/')
            if rel in WEB_ONLY:
                continue
            if rel not in reg:
                fails.append('未登记进 build_sources：src/%s' % rel)

    print()
    if fails:
        print('解耦闸门：FAIL（%d 项）' % len(fails))
        for f in fails:
            print('  - ' + f)
        return 1
    print('解耦闸门：PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
