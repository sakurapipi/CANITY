"""check_scope.py —— 范围锁：这次改动到底碰了哪些目录。

为什么需要它
    "只改 phase 域"这种约定，光靠嘴说守不住；AI 顺手改别处是常态。
    这个脚本把"改动范围"变成可检查的东西：
      git diff --name-only 里每一个路径，必须落在 --allow 白名单里，否则退出码 1。

用法
    # 检查工作区未提交的改动
    python tools/check_scope.py --allow domain/phase --allow tests/test_phase.c

    # 检查某个提交区间（CI 用）
    python tools/check_scope.py --range origin/main..HEAD --allow domain/phase

    # 允许"只读文件"被碰（契约头、文档不算越界）
    python tools/check_scope.py --allow domain/phase --allow-doc

退出码
    0 = 全部在白名单内（或只有允许的文档）
    1 = 有越界文件，逐条打印
    2 = git 调用失败
"""
import argparse
import fnmatch
import subprocess
import sys


def git_names(args):
    p = subprocess.run(['git'] + args, capture_output=True, text=True)
    if p.returncode != 0:
        print('  git 失败: %s' % (p.stderr.strip() or p.returncode))
        return None
    return [ln.strip() for ln in p.stdout.splitlines() if ln.strip()]


def changed_files(range_spec):
    if range_spec:
        files = git_names(['diff', '--name-only', range_spec])
        if files is None:
            return None
        return files
    out = []
    for args in (['diff', '--name-only', 'HEAD'],
                 ['diff', '--name-only', '--cached'],
                 ['ls-files', '--others', '--exclude-standard']):
        got = git_names(args)
        if got is None:
            return None
        out.extend(got)
    seen, uniq = set(), []
    for f in out:
        if f not in seen:
            seen.add(f)
            uniq.append(f)
    return uniq


DOC_PATTERNS = ('docs/*', '*.md', 'README*')


def matches(path, patterns):
    for p in patterns:
        if fnmatch.fnmatch(path, p) or path.startswith(p.rstrip('/') + '/'):
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--allow', action='append', default=[],
                    help='允许改动的路径或目录（可多次）')
    ap.add_argument('--allow-doc', action='store_true',
                    help='允许碰文档（*.md / docs/*）')
    ap.add_argument('--range', dest='range_spec', default=None,
                    help='检查某个区间，如 origin/main..HEAD')
    args = ap.parse_args()

    if not args.allow:
        print('  至少给一个 --allow')
        return 2

    files = changed_files(args.range_spec)
    if files is None:
        return 2
    if not files:
        print('  没有改动（工作区干净）')
        return 0

    bad = []
    for f in files:
        if matches(f, args.allow):
            continue
        if args.allow_doc and matches(f, DOC_PATTERNS):
            continue
        bad.append(f)

    print('  改动文件 %d 个，白名单 %s' % (len(files), args.allow))
    for f in files:
        print('    %s %s' % ('OK ' if f not in bad else '✗  ', f))
    if bad:
        print('')
        print('  ✗ 越界 %d 个：' % len(bad))
        for f in bad:
            print('    ' + f)
        print('  把改动挪回白名单目录，或显式加 --allow 并说明理由。')
        return 1
    print('')
    print('  ✓ 全部在白名单内')
    return 0


if __name__ == '__main__':
    sys.exit(main())
