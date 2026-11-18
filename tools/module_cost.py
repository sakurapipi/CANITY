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
    'evolution': (['domain/evolution/threat_tier.c', 'domain/evolution/monster_evolution.c',
                   'domain/evolution/evolution_hunters.c', 'domain/evolution/evolution_light.c',
                   'domain/evolution/evolution_mimic.c', 'domain/evolution/evolution_art.c',
                   'domain/evolution/tool_catalog.c', 'domain/evolution/tool_impact.c', 'domain/evolution/progression.c'],
                  ['threat_tier.h', 'monster_evolution.h', 'tactical_tools.h', 'evolution_art.h', 'progression.h'], 'evolution.md'),
    'evolution_save': (['save/evolution_wire.c'], ['evolution_save.h'], 'evolution.md'),
    'core':     (['core/tick.c', 'core/rng.c'], [], None),
    'localization': (['lang.c', 'cjktext.c'], ['lang.h'], 'localization.md'),
    'phase':    (['domain/phase/phase.c'], ['phase.h'], 'phase.md'),
    'hatch':    (['hatch.c', 'domain/hatch/hatch_place.c'], ['hatch.h'], 'hatch.md'),
    'collapse': (['domain/collapse/collapse.c'], ['collapse.h'], 'collapse.md'),
    'throwing': (['throwing.c', 'domain/throwing/shot_impact.c', 'domain/throwing/flare_heal.c'],
                 ['throwing.h', 'throwing_runtime.h', 'shot_spec.h'], 'throwing.md'),
    'item_effects': (['loop_items.c', 'domain/throwing/decoy_runtime.c'],
                     ['item_effects.h', 'decoy_runtime.h'], None),
    'character_story': (['domain/character/character_runtime.c', 'domain/story/story_runtime.c',
                         'domain/story/story_layout.c'],
                        ['protocol.h', 'src/domain/story/story_layout.h'], None),
    'input_settings': (['loop_input.c', 'loop_settings.c', 'loop_desktop_input.c'],
                       ['present_api.h'], None),
    'skills':   (['skills.c'], ['skills.h'], 'skills.md'),
    'lockbox':  (['lockbox.c'], ['lockbox.h'], 'lockbox.md'),
    'warden':   (['warden.c'], ['warden.h'], 'warden.md'),
    'shop':     (['shop.c'], ['shop.h'], 'shop.md'),
    'rooms': (['room_theme.c', 'domain/rooms/room_archetype.c',
               'domain/rooms/room_actions.c', 'domain/rooms/room_labels.c', 'domain/rooms/room_vault.c'],
               ['room_theme.h', 'room_archetype.h', 'room_template.h'], 'rooms.md'),
    'map_rooms': (['map_rooms.c', 'domain/rooms/floor_routes.c'], ['map_rooms.h', 'floor_routes.h'], 'map_rooms.md'),
    'ui':       (['ui.c', 'domain/ui/ui_text.c', 'domain/ui/ui_pages.c',
                 'domain/ui/ui_menu.c', 'domain/ui/ui_hud.c',
                 'domain/ui/ui_skilltree.c', 'domain/ui/ui_overlay.c', 'domain/ui/ui_archive.c', 'domain/ui/ui_calibration.c', 'domain/ui/ui_tactical.c', 'domain/ui/ui_frame_stats.c'],
                 ['ui.h', 'ui_text.h', 'ui_pages.h', 'ui_menu.h',
                  'src/domain/ui/ui_overlay.h', 'ui_archive.h', 'tactical_tools.h', 'ui_frame_stats.h'], 'ui.md'),
    'sprites': (['sprites.c', 'domain/sprites/sprite_canvas.c',
                 'domain/sprites/sprite_actors.c', 'domain/sprites/sprite_props.c',
                 'domain/sprites/sprite_room_devices.c', 'domain/sprites/sprite_theme_props.c', 'domain/sprites/sprite_evolution.c', 'domain/sprites/sprite_evolved_models.c'],
                 ['sprites.h', 'src/domain/sprites/sprite_bake.h'], 'sprites.md'),
    'render':   (['render.c', 'domain/render/render_map.c', 'domain/render/map_markers.c',
                 'domain/render/render_decoy.c',
                 'domain/render/render_spritequeue.c',
                 'domain/render/render_viewmodel.c',
                 'domain/render/render_props.c',
                 'domain/render/render_surface.c', 'domain/render/render_evolution.c'], ['render.h', 'src/domain/render/map_markers.h'], 'render.md'),
}


def contract_path(name):
    # Private source-directory contracts count toward the same domain budget.
    return os.path.join(ROOT if name.startswith('src/') else INC,
                        name.replace('/', os.sep))


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
        sizes = [count_lines(find_c(c)) for c in cs]
        impl = sum(sizes)                       # 合计（信息列）
        biggest = max(sizes) if sizes else 0    # 单文件最大值（判预算用）
        hdr = sum(count_lines(contract_path(h)) for h in hs)
        card_lines = count_lines(os.path.join(DOCS, card)) if card else 0
        ext = sum(extern_count(find_c(c)) for c in cs)
        # 第 2 轮起：域专属测试也算"必读量" —— 改这个域要先看它的用例
        test_lines = count_lines(os.path.join(TESTS, 'test_%s.c' % domain))
        out.append({
            'domain': domain,
            'impl': impl,
            'biggest': biggest,
            'header': hdr,
            'card': card_lines,
            'test': test_lines,
            'extern': ext,
            # 判预算用"最大单文件"：改一个域只需要读它所在的那个文件
            'cost': biggest + hdr + card_lines + test_lines,
            'missing': [p for p in
                        ([find_c(c) for c in cs] +
                         [contract_path(h) for h in hs] +
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

    print('  %-11s %7s %7s %7s %6s %6s %7s %8s' %
          ('域', '最大文件', '全部合计', '契约头', '卡片', '测试', 'extern', '必读'))
    print('  ' + '-' * 64)
    over = 0
    for r in rows:
        flag = ''
        if r['cost'] > args.budget:
            flag = '  ✗ 超预算'
            over += 1
        elif r['impl'] == 0 and r['header'] == 0:
            flag = '  （未拆分）'
        print('  %-11s %7d %7d %7d %6d %6d %7d %8d%s' %
              (r['domain'], r['biggest'], r['impl'], r['header'], r['card'],
               r.get('test', 0), r['extern'], r['cost'], flag))
    print('')
    print('  预算 %d 行；超预算 %d 个域' % (args.budget, over))
    print('  注：game.c 为调度入口；本表按当前模块统计，不再引用历史行数。')
    return 1 if over else 0


if __name__ == '__main__':
    sys.exit(main())
