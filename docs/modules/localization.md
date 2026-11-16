# localization（本地化）

负责统一词条选择、中文回退、UTF-8 解码及预烘焙字形查询。UI 使用本域结果，几何布局仍归 UI 所有。

- 实现：src/lang.c、src/cjktext.c；生成数据：src/lang_table.inc、src/cjk_glyphs.c。
- 契约：include/lang.h；字形查询沿用 include/render.h 的 CjkRows 声明。
- 词条源：l10n/zh.tsv；生成工具：l10n/apply_tsv.py、l10n/bake_cjk_atlas.py。
- 回归：tests/domains/test_localization.c，通过 test_runtime --suite localization 独立运行，完整验证脚本强制包含此域。
- 验证词条优先级、回退、英文模式、格式占位符及四档字号字形。与 UI 测试分离，不删减断言。
- 自动生成的字形数据不作为手写逻辑预算；词条/字体覆盖由本地化清单与运行域检查。
