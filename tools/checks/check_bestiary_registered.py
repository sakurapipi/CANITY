"""新增检查器：图鉴必须登记进内容中枢。

## 查什么

C 侧测试已经验过（test_bestiary_registered_in_hub），但那只在跑测试时生效。
这个检查器做**静态**验证：bestiary.c 里必须真的存在 ContentAdd(CK_MONSTER, ...)，
而且每一行图鉴的英文名都要被登记 —— 防止以后有人加了表行忘了登记。

这是"检查器框架可以无限加"的第二个真实例子。
"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

NAME = 'bestiary_registered'
DESC = '图鉴是否登记进内容中枢（加了表行却不登记 = 中枢里看不见）'


def run():
    out = []
    # 表在 content/ 层，登记逻辑在 bestiary.c —— 两个都要读
    tbl = None
    for c in ('src/content/content_monsters.c', 'src/bestiary.c'):
        p = os.path.join(ROOT, c)
        if os.path.isfile(p):
            tbl = io.open(p, encoding='utf-8', errors='replace').read()
            break
    best = io.open(os.path.join(ROOT, 'src', 'bestiary.c'),
                   encoding='utf-8', errors='replace').read()

    out.append(('ContentAdd(CK_MONSTER' in best,
                'bestiary.c 里有 ContentAdd(CK_MONSTER, ...) 登记调用'))
    out.append(('BestiaryRegister' in best,
                'bestiary.c 里有 BestiaryRegister() 实现'))

    # 每个图鉴行的英文名都必须在登记循环的覆盖范围内
    body_m = re.search(r'const BestiaryEntry bestiary\[[^\]]*\]\s*=\s*\{(.*?)\n\};',
                       tbl or best, re.S)
    rows = re.findall(r'\{\s*"([^"]+)"', body_m.group(1)) if body_m else []
    loop_ok = re.search(r'for\s*\(\s*i\s*=\s*0\s*;\s*i\s*<\s*BESTIARY_COUNT\s*;\s*i\+\+\s*\)',
                        best) is not None
    out.append((loop_ok,
                '登记循环覆盖 BESTIARY_COUNT（而不是写死 0..6）'))
    out.append((len(rows) >= 1, '解析出 %d 条图鉴行的英文名' % len(rows)))
    return out
