"""新增检查器：技能必须登记进中枢。"""
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

NAME = 'skill_registered'
DESC = '技能是否登记进内容中枢（有表行却不登记 = 中枢里看不见）'


def run():
    out = []
    src = io.open(os.path.join(ROOT, 'src', 'skills.c'),
                  encoding='utf-8', errors='replace').read()

    out.append(('ContentAdd(CK_SKILL' in src,
                'skills.c 里有 ContentAdd(CK_SKILL, ...) 登记调用'))
    out.append(('void SkillRegister(void)' in src,
                'skills.c 里有 SkillRegister() 实现'))
    out.append((re.search(r'for\s*\(\s*i\s*=\s*0\s*;\s*i\s*<\s*SKILL_COUNT\s*;\s*i\+\+\s*\)',
                          src) is not None,
                '登记循环覆盖 SKILL_COUNT（不是写死）'))
    out.append(('SkillRegister();' in src,
                'SkillResetAll 里真的调用了 SkillRegister（防线已通电）'))
    return out
