# CANITY 画风：手工布偶（Coraline 式）

2026-11-11。基于 **2.4.2-RC4（31ccaa4）** 重落。默认开启，`CANITY_STYLE=classic` 可完整回退。

这是"先把方向做出来看一眼"的版本，不是完成品。没做的部分在最后一节列清楚。

## 目标：玩家应该看见什么

1. **材料**：墙、地板、怪物都像**布料/纸板**做的，而不是贴了图的平面。近看有织纹，远看颜色干净。
2. **布景**：整个画面像"一个被台灯照着的模型箱"——四周压暗，边缘有一圈缝过线的包边。
3. **被拼装感**：守门人追击态是样张——布缝的袍子、铜扣眼睛、钉书钉和补丁（在 `art/doll-beta` 分支上，尚未搬到 RC4）。

## 开关

```
CANITY_STYLE=classic   # 回到改造前（贴图、精灵、后处理逐像素不变）
不设 / 其它值            # 手工布偶（默认）
```

判定只有一处：`art.c` 的 `ArtStyleDoll()`，读一次环境变量后缓存。
渲染与烘焙读同一个判定，不会出现"精灵换了、整屏没换"的半套状态。

## 这一版改了什么

### 1. 材质分级：`ArtDollGrade()`（`art.c`）

去饱和 38%，高光偏琥珀、阴影偏冷青。作用对象是**贴图与精灵的烘焙期**：

- `render.c GenerateThemeTextures()`：把 `RoomMaterialSample()` 的三张图过一遍；
- `sprites.c cvFinish()`：每张精灵降采样后过一遍。

为什么不在每帧后处理里做：beta 上实测后处理版每帧多 5.3ms，烘焙版只多 2.3ms。
而且分级落在反照率上，手电的冷光仍然能照出冷暖对比。

### 2. 整屏布景层（`render.c`）

RC4 的 `RenderPostBand()` 已经是一整套 perception 后处理（模糊、失真、颗粒、血丝、边缘红青错位）。
布偶层**没有另起一遍全屏 pass**，而是接在它同一个像素循环的末尾：

- `craftTex`：2×2 织纹（±8%）+ 固定相位纤维噪点 + 四周压暗，预生成为乘数表；
- `CraftFrameRow()`：屏幕最外 4px 压暗，第 4px 上每 7px 一段亚麻色线，**直接写像素**而不是乘系数。

只处理世界，不处理 UI：HUD / 菜单在这之后绘制，颜色和对比度保持原样。

### 3. 精灵剪边（`sprites.c cvFinish()`）

RC4 的精灵是 **512×512**（`g_canvasScale`），所以剪边带宽按 `ow/128` 缩放——
否则同一条 1px 边在 512 上只有原来的 1/4 宽，等于没做。

- 剪边：轮廓向内 `S=ow/128` 像素提亮成亚麻纤维色；
- 接触阴影：再往外一圈压暗 12%；
- 织物：`S×S` 棋盘（4×4 @512）压暗 3%。

实现上只处理包围盒，AO 用"从轮廓膨胀"而不是逐像素查 5×5；
69 张 512×512 全扫会多等一秒以上。现在整次烘焙 **2.11s → 1.74s 基线**，只多 0.37s。

跳过 `SPR_UI_PANEL` ~ `SPR_HATCH_0` 之间的 UI 精灵。

## 验证证据

- `test_canity.exe` → `CANITY offline self-check: PASS`
- `test_runtime.exe` → `CANITY runtime integration: PASS (17034 checks)`
- `tools/partition_check.py` → `partition: OK`
- A/B 截图：`work/shot/rc4_ab.png`（左 classic / 右布偶，同一帧）
- 主题走查：`work/shot/rc4_themes.png`（锈工厂 / 血肉洞窟）
- 精灵总表：`work/shot/sprites_rc4.png`；单张 512 原图：`work/shot/one_hunter.png`
- 渲染耗时（800×600、无窗口、20 帧平均）：classic 23.2ms / 布偶 23.1ms —— 在噪声范围内
- 启动烘焙：classic 1.74s / 布偶 2.11s（+0.37s，一次性）

> 说明：`build_validate.py` 这次在最后清理临时目录时被 Windows 文件权限挡住
> （`work/codex-build/runtime-*`，连读取都拒绝；ACL 诊断判定不属于可自动修复的类）。
> 三套检查是分别单跑的，结果如上；这不是代码失败。

## 未验证 / 未做

- **没有在真实窗口里跑过**：截图来自 `tools/world_shot.c`（同一条 `RenderFrame()`，但不开窗口、不走呈现与输入）。
  DPI 缩放、最大化、网页版 wasm、手机端都没走查。
- **布偶版守门人没有搬过来**：RC4 把 `BakeMonster` 重构成了 `MonsterBakeContext` + 分部件函数，
  beta 上那 284 行缝合怪改动不能直接 cherry-pick。它完整保存在 `art/doll-beta` 分支。
- **守门人追击态的呼吸帧**（`SPR_MON_WARDEN_BREATH_A/B`）同样只在 beta 分支上，RC4 没有。
- **7 套主题只看了 4 套**（砖墙 / 锈工厂 / 血肉洞窟 / 监狱），冰窖、病房、地狱没看。
- **墙面材质没有重做**：现在的"布偶感"来自分级 + 织纹，墙本身还是 `RoomMaterialSample()` 的砖/铁/瓷砖。
- **UI 没动**：RC4 的菜单已经换成新布局，但仍是原来的配色，和新画风有割裂。

## 建议的下一步（按性价比）

1. 把布偶版守门人搬到 RC4 的 `BakeHumanoidWardenDetails()`（缝合线、钮扣眼、补丁、钉书钉）；
2. UI 纸标签化（打字机正文 + 纸条面板）；
3. 7 套主题的墙面拼布重做（缝线、补丁、胶带贴角）；
4. 理智越低、织纹错位越明显（把画风绑到 `sanity`，几乎零成本）；
5. 实机窗口截图与网页端回归。
