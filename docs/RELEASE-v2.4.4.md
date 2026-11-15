# CANITY v2.4.4 发布说明

日期：2026-11-16　｜　存档版本：**v45（与 2.4.3 兼容，可直接升级）**

---

## 一、版本来源

v2.4.4 是对**两条分支合并后的第一次正式发布**：

```
main 线（作者版 2.4.3：触控、存档迁移、美术资产）
   +
日常优化线（精灵/渲染/打包/体验修复）
   ↓  合并提交 cc6b2c1（5 处冲突，全部按"语义优先"手工解决）
v2.4.4
```

合并报告见 `docs/合并_v2.4.3到main_20261116.md`。

---

## 二、玩家能看到的变化

### 1. 扳手手感重做（用户验收通过）

```
旧：5 帧旋转挥砍，只有两帧动画，看起来在"抖动"
新：12 帧前刺（0.266s），角度 ±9°，位移朝准星推出
    命中帧与逻辑对齐（0.056s 出招即判定）
    剥出"出招姿态"变体：钳口张开、头略放大、颚面亮边、前方冲击线
```

修改依据是用户三次实测反馈：「太生硬」→「像帕金森」→「要直线往前 P 一下」。

### 2. 设置里新增「Vertical Look」（上下视角开关）

默认**关**。开启后可用上下视角，范围 ±26°。

### 3. 小游戏与藏身点七项修复

| 项 | 修法 |
|---|---|
| K 热键误触发 | 移除（现在只能用交互键） |
| 藏身点太密 | 最小间距 10 格（放宽 7 格）+ 每象限最多 2 个 |
| 判定区间太小 | 基础判定窗口 160ms（高关卡自动收紧） |
| 音符越来越快 | 速度固定，不再随时间加速 |
| 失败被踢出藏身点 | 失败只结束小游戏，人留在藏身点 |
| 提示字幕被遮罩吃掉 | 删除无意义的三条提示（外面安静了/开始小游戏/打开商店） |
| UI 元素重叠 | 重排：COMBO/恢复数/按键提示/退出提示各归其位 |

### 4. 暂停页配色修正

```
问题：满强度档下，二级/三级暂停页是红色，一级暂停页却永远是蓝色
原因：① 一级暂停页为了能看见场景，完全没有铺背景色 → 透出蓝色走廊
      ② 暂停页与设置页用了两套不同的档位公式
修法：① 铺 72% 主题底色（场景仍可见）
      ② 档位统一取 max(局内压力, 银行强度档)
```

---

## 三、代码层体检（自动化扫描）

```
gcc -fanalyzer（106 个 .c）      2 条诊断，逐条判定均为误报
  help_book.c:48  未初始化值      → codepoint>=32 保证 n>=1，四分支写满
  save.c:1365     .bak 截断      → 上一行 strlen 检查已保证装得下
-Wall -Wextra -Werror            全绿
partition_check                  OK（S1~S5）
stub_scan                        0 unmarked
hud_check / ui_layout_check      0 重叠 0 越界
build_validate                   140 passed / 0 failed
```

**结论：没有待修的确定性缺陷。**

---

## 四、架构解耦（本轮完成的部分）

### 已达标

```
域 render：4312 → 2420 行  ✅（预算 2500）
render.c：4202 → 2279 行
```

新增 6 个域文件（全部"状态随簇走"，S3 合规）：

```
src/domain/render/render_map.c          地图与图例
src/domain/render/render_decoy.c        诱饵玩偶模型
src/domain/render/render_spritequeue.c  精灵绘制队列收集
src/domain/render/render_viewmodel.c    第一人称手持模型
src/domain/render/render_props.c        投影/染色怪/投掷物/胶水
src/domain/render/render_surface.c      像素缓冲初始化与画面边框
src/domain/ui/ui_hud.c                  局内 HUD（15 个函数 / 1406 行）
```

### 新增治理工具

```
tools/coupling_gate.py        解耦闸门（域预算/单文件/函数上限/登记完整性）
docs/架构规则_解耦闸门.md      五条硬规则 + 拆的判据 + 新功能流程
```

**用户要求**：「以后进这个游戏的代码，优先级都必须过一次防耦合和多模块化」——
闸门就是这个规则的执行体。

---

## 五、仍未完成（诚实记账）

闸门当前 FAIL 5 项：

```
域 ui 超预算       3050 > 2500（ui.c 2530）
文件 sprites.c     3863 > 2600
函数 loop_desktop_input.c:1292  507 行 > 300
函数 main.c:86（WinMain）       314 行 > 300
函数 throwing.c:379             316 行 > 300
```

**两次拆分尝试失败并已完整回退**（工作区干净，无半成品）：

```
① 技能树（992 行）：与 ui.c 深度纠缠
   kSkillList / skVX.. 共享静态表 + SkillTreeDrawContext + 按钮矩形双向依赖
② 帧末叠加层（366 行）：两条提示条共享的错误状态声明跨行，
   切割后 ui.c 出现结构性损坏
```

**下一阶段的正确顺序**（先解耦状态、再拆文件）：
```
1. 把 kSkillList / skVX / skVY / skVK / skDrag* 收进一个 SkillTree 结构体 + 访问器
2. 再拆 ui_skilltree.c（那时是纯函数搬迁，零共享状态）
3. sprites.c 按"怪物烘焙 / 道具烘焙"拆两块
4. 三个超大函数按状态机阶段拆
```

---

## 六、升级说明

```
存档：SAVE_VERSION 45 未变，2.4.3 的存档可直接读
配置：设置项从 44 增至 45（新增 Vertical Look），旧配置按默认值补齐
网页端：需重新执行 python -X utf8 tools/build_web.py 生成 index.wasm
```

---

## 七、提交记录（本轮）

```
52d2caa  refactor: 拆出 ui_hud.c（HUD 15 函数/1406 行），ui.c 3948→2530
6b10661  refactor: 拆出 render_surface.c，render 域达标
9530ee2  refactor: 拆出 render_props.c
2434684  refactor: 拆出 render_spritequeue.c 与 render_viewmodel.c
f94028f  refactor: render.c 3284→2718，拆出 render_decoy.c
628fd6a  refactor: 拆出 render_map.c + 新增解耦闸门工具与架构规则
eccc479  fix: 画面档位统一取 max(局内压力, 银行强度档)
f5b1dbe  fix: 暂停页铺 72% 主题底色
d450417  fix: 重新设计扳手出招款
```
