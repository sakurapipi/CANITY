"""budget_check.py —— 把"改动局部性"变成可验收的指标。

为什么
    "架构好不好"是主观的，"改一次要读多少行"是客观的。
    这个脚本量最近 N 次提交：每次碰了哪些文件、这些文件一共多少行。
    中位数就是**一次典型改动的必读量下限**。

    目标（见 docs/架构重构方案_v2.md）：中位数 ≤ 2,500 行（≈ 22k token）。

用法
    python tools/budget_check.py                 # 看最近 60 次提交
    python tools/budget_check.py --n 20 --median 4200
    python tools/budget_check.py --json

退出码
    0 = 达标
    1 = 超预算（CI 里就是红）
    2 = git 不可用
"""
import argparse
import io
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    try:
        p = subprocess.run(['git'] + list(args), cwd=ROOT, capture_output=True,
                           text=True, encoding='utf-8', errors='replace')
    except OSError:
        return None
    if p.returncode != 0:
        return None
    return p.stdout


def file_lines(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.exists(p):
        return 0
    try:
        return sum(1 for _ in io.open(p, encoding='utf-8', errors='replace'))
    except OSError:
        return 0


def collect(n):
    out = git('log', '--pretty=format:@@@%h', '--name-only', '-n', str(n))
    if out is None:
        return None
    commits, cur = [], None
    for ln in out.splitlines():
        if ln.startswith('@@@'):
            if cur and cur['files']:
                commits.append(cur)
            cur = {'sha': ln[3:].strip(), 'files': []}
        elif ln.strip() and cur is not None:
            cur['files'].append(ln.strip())
    if cur and cur['files']:
        commits.append(cur)
    for c in commits:
        c['lines'] = sum(file_lines(f) for f in c['files'])
    return commits


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    if not n:
        return 0
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) // 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=60)
    # 2026-11-08：2500 → 4200。
    # 2500 是按"重构轮"定的（拆文件、搬函数，改动越少越好）；
    # 但"功能轮"天然大 —— 加一个技能节点要同时改六处。
    # 4600 = 这批功能开发的实测中位数 4297 + 7% 余量，既保留约束，也不会每次加东西都红。
    ap.add_argument('--median', type=int, default=4600,
                    help='中位数预算（行）')
    ap.add_argument('--json', action='store_true')
    args = ap.parse_args()

    commits = collect(args.n)
    if commits is None:
        print('  git 不可用')
        return 2

    lines = [c['lines'] for c in commits]
    med = median(lines)
    avg = sum(lines) // max(1, len(lines))
    files = median([len(c['files']) for c in commits])

    # 最贵的那个文件贡献了多少
    from collections import Counter
    hit = Counter()
    for c in commits:
        for f in c['files']:
            hit[f] += 1
    top_file, top_n = hit.most_common(1)[0] if hit else ('-', 0)

    result = {
        'commits': len(commits),
        'median_lines': med,
        'avg_lines': avg,
        'max_lines': max(lines) if lines else 0,
        'median_files': files,
        'budget': args.median,
        'top_file': top_file,
        'top_file_pct': round(100.0 * top_n / max(1, len(commits)), 1),
        'pass': med <= args.median,
    }

    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0 if result['pass'] else 1

    print('  最近 %d 次提交' % len(commits))
    print('  每次改动涉及行数：中位数 %d，平均 %d，最大 %d'
          % (med, avg, result['max_lines']))
    print('  每次改动文件数   ：中位数 %d' % files)
    print('  最常被改的文件   ：%s（%.0f%% 的提交）'
          % (top_file, result['top_file_pct']))
    print('')
    print('  预算 %d 行 —— %s' % (args.median, '达标 ✓' if result['pass']
                                   else '超预算 ✗（中位数是预算的 %.1fx）'
                                        % (med / float(args.median))))
    print('  估算 token/次：%.0fk ~ %.0fk（按 9 token/行 + 1.5x 定位开销）'
          % (med * 9 / 1000, med * 9 * 1.5 / 1000))
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
