# shop（商店 + 局内改造树）

## 职责
安全屋里的商店货架、价格与折扣、改造树（Bench）的购买与选中。
**不负责**背包与金币的增减（那在 wallet/game）。

## 契约
`include/shop.h` —— 19 个函数 + 10 个全局（`shopPage`、`shopRow`、`shopOffer[]`、`shopPrice[]`…）。
`ShopActivate()` 是唯一的"确认购买/进入"入口。

## 绝不做
- 不在 UI 里算价格（调 `ShopPriceAt` / `ShopIsDiscounted`）
- 不在 shop 里直接扣钱（走 wallet 的接口，便于统一打折/退款）
- 不加新的"页面"而不更新 `ShopPageCount`（否则 TAB 循环会越界）

## 测试
`python tools/build_validate.py` → PASS
关键行为：价格随层数/折扣变化正确 / 补货倒计时 / 树页与货架页切换不串状态。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加一件商品 | `ShopRefresh` 的发牌表 + `ShopPriceAt` 的定价 |
| 改折扣规则 | `shopDiscountPct` 的赋值处 |
| 改改造树节点 | `bench.c`（树数据在那边，本域只管买） |

## 历史坑
- 空格在安全屋被"校准"吃掉，导致树页买不了东西 → 树页不再吃空格（AD-63 同类：硬编码按键会吃掉新动作）。
- `BenchFocusNode(shopNodeSel)` 曾在每次按键时无条件居中，导致"左右移动像没动"。
