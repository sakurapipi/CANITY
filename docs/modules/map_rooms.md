# map_rooms（主题房间与房间家具）

## 职责
关卡里的 7 种主题房间：模板放置、每格 `cellMeta`（roomId/themeId/flags）、
房间家具（货架/控制台/箱堆）的生成与查询。

## 契约
`include/map_rooms.h` —— 10 个函数 + 3 个全局。
被全库依赖的关键一个：`PropBlocksAt(cx,cy)` —— `Blocked()` 会调它，
所以**这里的判据变了，全游戏的碰撞都会变**。

## 绝不做
- 不改 `cellMeta` 的字段顺序（存档/重放都依赖它）
- 不在渲染里直接读 `roomProps[]`（走 `RoomPropLayers`）
- 不让家具参与"房间可达性判定"（房间墙的 roomId 必须是 0）

## 测试
`python tools/build_validate.py` → PASS
关键行为：房间不越界 / 不放死口袋 / `PropBlocksAt` 与实际家具一致。

## 常见改动
| 想改什么 | 动哪里 |
|---|---|
| 加主题房间 | `room_template.h` 加一行 + 一张 tile 图 |
| 改房间数量 | `GenerateThemeRooms` 的预算函数 |
| 改家具种类 | `RoomPropAdd` 的 kind + `RoomPropLayers` |

## 历史坑
- 家具曾经**不参与碰撞**，玩家能穿过去；后来改成"挡路"，又引发"相位信标落在家具上"
  → 判据必须和传送判据一致（AD-68）。
- 每个家具只画 1 个精灵（层数在 `RoomPropLayers` 里合成），多了会掉帧。
