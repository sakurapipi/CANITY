#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CANITY 内容检查器 —— 统一入口（2026-11-09）。

## 为什么要有这个东西

这个项目里同一个内容的"事实"经常散在好几处：

    技能：skillDefs[] + kShortNames[] + kNodeNX/NY[] + kSkillList[]
    图鉴：bestiary[] + bestiaryKnowledge[]
    设置：settings[] + g_bindings[]

全靠人同步 → 漏一处就出现"加了 10 个节点界面一个不画"这种事。

## 设计：注册制

    tools/checks/check_*.py      每个文件一个检查器
    每个检查器暴露：
        NAME = "短名字"
        DESC = "一句话说明查什么"
        def run() -> list[tuple[bool, str]]     (通过?, 说明)

以后要加检查器：**新建一个文件就行** —— 不用改这个 runner，也不用改 CI。

## 用法

    python tools/content_check.py              # 跑全部
    python tools/content_check.py skill best   # 只跑名字含这些关键字的
    python tools/content_check.py --list       # 列出有哪些检查器
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKS_DIR = os.path.join(HERE, 'checks')


def discover():
    """按文件名排序加载 checks/check_*.py。"""
    if not os.path.isdir(CHECKS_DIR):
        return []
    mods = []
    for fn in sorted(os.listdir(CHECKS_DIR)):
        if not (fn.startswith('check_') and fn.endswith('.py')):
            continue
        path = os.path.join(CHECKS_DIR, fn)
        spec = importlib.util.spec_from_file_location(fn[:-3], path)
        mod = importlib.util.module_from_spec(spec)
        try:
            spec.loader.exec_module(mod)
        except Exception as e:                      # 检查器自身写坏了
            print('  ✗ %-28s 加载失败: %s' % (fn, e))
            continue
        mods.append((fn[:-3], mod))
    return mods


def main(argv):
    mods = discover()
    if not mods:
        print('  ✗ 没找到任何检查器（tools/checks/check_*.py）')
        return 1

    if '--list' in argv:
        print('  检查器 %d 个：' % len(mods))
        for name, m in mods:
            print('    %-24s %s' % (name, getattr(m, 'DESC', getattr(m, 'NAME', ''))))
        return 0

    # 关键字过滤
    picks = [a for a in argv if not a.startswith('-')]
    if picks:
        mods = [(n, m) for n, m in mods
                if any(p.lower() in n.lower() for p in picks)]
        if not mods:
            print('  ✗ 没有匹配关键字的检查器')
            return 1

    print('=' * 72)
    print(' CANITY 内容检查（%d 个检查器）' % len(mods))
    print('=' * 72)

    total = failed = 0
    for name, m in mods:
        print()
        print(' [%s] %s' % (name, getattr(m, 'DESC', '')))
        try:
            results = m.run()
        except Exception as e:
            print('   ✗ 运行异常: %s' % e)
            failed += 1
            total += 1
            continue
        for ok, msg in results:
            total += 1
            if not ok:
                failed += 1
            print('   %s %s' % ('✓' if ok else '✗', msg))

    print()
    print('=' * 72)
    if failed:
        print(' ✗ %d/%d 条不通过' % (failed, total))
        print('=' * 72)
        return 1
    print(' ✓ 全部 %d 条通过' % total)
    print('=' * 72)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
